#include "VectorExecution.hpp"
#include <codegen/x86/F16cLowering.hpp>
#include <codegen/x86/IAmd64OnlyInstructionMatcher.hpp>
#include <codegen/CodegenException.hpp>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using LoweringExecution::Bytes;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

Bytes halfToFloat(std::uint8_t dst, std::uint8_t src, bool wide256, bool wideW = false, std::uint8_t vvvv = 0) {
    return {0xC4, static_cast<std::uint8_t>(((dst & 8) ? 0 : 0x80) | 0x40 | ((src & 8) ? 0 : 0x20) | 2),
        static_cast<std::uint8_t>((wideW ? 0x80 : 0) | ((~vvvv & 15) << 3) | (wide256 ? 4 : 0) | 1), 0x13,
        static_cast<std::uint8_t>(0xC0 | ((dst & 7) << 3) | (src & 7))};
}

Bytes halfToFloatFromRax(std::uint8_t dst, bool wide256) {
    return {0xC4, static_cast<std::uint8_t>(((dst & 8) ? 0 : 0x80) | 0x60 | 2), static_cast<std::uint8_t>(0x78 | (wide256 ? 4 : 0) | 1), 0x13,
        static_cast<std::uint8_t>((dst & 7) << 3)};
}

bool rejects(const Bytes& bytes) {
    try { (void)Codegen::DecodeHalfToFloat(bytes.data(), bytes.size()); }
    catch (const Codegen::CodegenException&) { return true; }
    return false;
}

void decoding() {
    for (const bool wide : {false, true}) {
        const auto bytes = halfToFloat(9, 12, wide);
        const auto operands = Codegen::DecodeHalfToFloat(bytes.data(), bytes.size());
        require(operands && operands->Wide == wide && operands->Destination == 9 && operands->Source == 12 && !operands->Memory, "VCVTPH2PS lost an operand");
        const auto loaded = halfToFloatFromRax(3, wide);
        const auto memory = Codegen::DecodeHalfToFloat(loaded.data(), loaded.size());
        require(memory && memory->Wide == wide && memory->Destination == 3 && memory->Memory, "a memory VCVTPH2PS lost its operand");
        require(rejects(halfToFloat(1, 2, wide, true)), "VCVTPH2PS with VEX.W1 was accepted");
        require(rejects(halfToFloat(1, 2, wide, false, 5)), "VCVTPH2PS with VEX.vvvv other than 1111b was accepted");
        auto trailing = bytes;
        trailing.push_back(0x90);
        require(rejects(trailing), "VCVTPH2PS with trailing bytes was accepted");
    }
    const Bytes prefixed{0x65, 0xC4, 0x62, 0x7D, 0x13, 0x05, 0x10, 0, 0, 0};
    const auto rip = Codegen::DecodeHalfToFloat(prefixed.data(), prefixed.size());
    require(rip && rip->Wide && rip->Destination == 8 && rip->Memory && rip->Memory->RipRelative && rip->Memory->Prefixes == Bytes{0x65}, "a RIP-relative VCVTPH2PS lost its prefix or operand");
    for (const Bytes bytes : {Bytes{0xC5, 0xF9, 0x13, 0x00}, Bytes{0xC4, 0xE2, 0x78, 0x13, 0xC1}, Bytes{0xC4, 0xE2, 0x79, 0x14, 0xC1}, Bytes{0xC4, 0xE3, 0x79, 0x13, 0xC1, 0}, Bytes{0x90}})
        require(!Codegen::DecodeHalfToFloat(bytes.data(), bytes.size()), "an instruction that is not VCVTPH2PS was decoded as one");
}

void matching() {
    const auto intel = Codegen::MakeAmd64OnlyInstructionMatcher();
    const auto rosetta = Codegen::MakeAmd64OnlyInstructionMatcher(Codegen::Amd64OnlyTarget::Rosetta);
    const auto avx = Codegen::MakeAmd64OnlyInstructionMatcher(Codegen::Amd64OnlyTarget::Avx);
    for (const bool wide : {false, true}) {
        const auto bytes = halfToFloat(0, 1, wide);
        require(!intel->Match(bytes.data(), bytes.size()) && !rosetta->Match(bytes.data(), bytes.size()), "Intel or Rosetta mode lowered VCVTPH2PS");
        const auto match = avx->Match(bytes.data(), bytes.size());
        require(match && match->InstructionName == "VCVTPH2PS" && match->Lowering == Codegen::Amd64OnlyLowering::Trampoline && match->RelocatableWindow && !match->Optional,
            "AVX mode did not lower VCVTPH2PS through a movable stub");
    }
}

#if LOWERING_EXECUTION_AVAILABLE

using VectorExecution::Machine;
using VectorExecution::Runner;

constexpr std::uint32_t kInvalidFlag = 1;

Bytes lowered(const Bytes& instruction) {
    Codegen::StubBodyBuilder body;
    Codegen::EmitHalfToFloat(body, *Codegen::DecodeHalfToFloat(instruction.data(), instruction.size()));
    auto finished = body.Finish();
    auto bytes = finished.Bytes;
    const auto skip = static_cast<std::uint32_t>(bytes.size() - (finished.ReturnBranchOffset + 5));
    bytes[finished.ReturnBranchOffset] = 0xE9;
    for (unsigned index = 0; index < 4; ++index)
        bytes[finished.ReturnBranchOffset + 1 + index] = static_cast<std::uint8_t>(skip >> (8 * index));
    return bytes;
}

std::uint32_t model(std::uint16_t half) {
    const auto sign = static_cast<std::uint32_t>(half & 0x8000) << 16;
    const auto exponent = (half >> 10) & 31u;
    const auto mantissa = static_cast<std::uint32_t>(half & 0x3FF);
    if (exponent == 31) return sign | 0x7F800000u | (mantissa << 13) | (mantissa != 0 ? 0x00400000u : 0);
    if (exponent != 0) return sign | ((exponent + 112) << 23) | (mantissa << 13);
    return sign | std::bit_cast<std::uint32_t>(std::ldexp(static_cast<float>(mantissa), -24));
}

bool signaling(std::uint16_t half) {
    return (half & 0x7C00) == 0x7C00 && (half & 0x3FF) != 0 && (half & 0x200) == 0;
}

bool hostF16c() {
    static const bool available = LoweringExecution::HostExecutes(Bytes{0xC4, 0xE2, 0x79, 0x13, 0xC0});
    return available;
}

void same(const Machine& actual, const Machine& expected, const std::string& what) {
    for (std::uint8_t reg = 0; reg < 16; ++reg)
        require(actual.Ymm[reg] == expected.Ymm[reg], what + ": ymm" + std::to_string(reg) + " is wrong");
    for (std::uint8_t reg = 0; reg < 16; ++reg)
        require(actual.Gpr[reg] == expected.Gpr[reg], what + ": general-purpose register " + std::to_string(reg) + " is wrong");
    require((actual.Flags & VectorExecution::kStatusFlags) == (expected.Flags & VectorExecution::kStatusFlags), what + ": RFLAGS changed");
    require(actual.Mxcsr == expected.Mxcsr, what + ": MXCSR is " + std::to_string(actual.Mxcsr) + ", wanted " + std::to_string(expected.Mxcsr));
    for (const auto slot : actual.RedZone) require(slot == LoweringExecution::kSentinel, what + ": the red zone was overwritten");
}

struct Case {
    Bytes Instruction;
    bool Wide;
    bool FromMemory;
    std::uint8_t Destination;
    std::uint8_t Source;
};

class Checker {
public:
    explicit Checker(const Case& site) : _site(site), _lowered(lowered(site.Instruction)), _native(site.Instruction) {}

    void Check(const std::array<std::uint16_t, 8>& halfs, std::uint32_t mxcsr) const {
        auto start = VectorExecution::Initial();
        start.Mxcsr = mxcsr;
        std::uint64_t low = 0;
        std::uint64_t high = 0;
        for (unsigned index = 0; index < 4; ++index) {
            low |= static_cast<std::uint64_t>(halfs[index]) << (16 * index);
            high |= static_cast<std::uint64_t>(halfs[index + 4]) << (16 * index);
        }
        if (_site.FromMemory) {
            start.Memory[0] = low;
            start.Memory[1] = high;
        } else {
            start.Ymm[_site.Source][0] = low;
            start.Ymm[_site.Source][1] = high;
        }
        auto machine = start;
        machine.Gpr[0] = reinterpret_cast<std::uint64_t>(machine.Memory.data());
        auto expected = machine;
        const unsigned lanes = _site.Wide ? 8 : 4;
        expected.Ymm[_site.Destination] = {0, 0, 0, 0};
        bool invalid = false;
        for (unsigned index = 0; index < lanes; ++index) {
            expected.Ymm[_site.Destination][index / 2] |= static_cast<std::uint64_t>(model(halfs[index])) << (32 * (index % 2));
            invalid = invalid || signaling(halfs[index]);
        }
        if (invalid) expected.Mxcsr |= kInvalidFlag;
        const auto what = std::string(_site.Wide ? "256-bit" : "128-bit") + " VCVTPH2PS dst " + std::to_string(_site.Destination) + " src " +
            (_site.FromMemory ? std::string("[rax]") : std::to_string(_site.Source)) + " first half " + std::to_string(halfs[0]) + " mxcsr " + std::to_string(mxcsr);
        _lowered.Run(machine);
        same(machine, expected, what);
        if (!hostF16c()) return;
        auto native = start;
        native.Gpr[0] = reinterpret_cast<std::uint64_t>(native.Memory.data());
        _native.Run(native);
        native.Gpr[0] = expected.Gpr[0];
        same(native, expected, what + " on the host");
    }

private:
    Case _site;
    Runner _lowered;
    Runner _native;
};

void exhaustive() {
    for (const bool wide : {true, false}) {
        const Checker checker(Case{halfToFloat(5, 11, wide), wide, false, 5, 11});
        const unsigned lanes = wide ? 8 : 4;
        for (std::uint32_t first = 0; first < 0x10000; first += lanes) {
            std::array<std::uint16_t, 8> halfs{};
            for (unsigned index = 0; index < 8; ++index)
                halfs[index] = static_cast<std::uint16_t>(first + index % lanes);
            checker.Check(halfs, 0x1F80);
        }
    }
}

void registersAndModes() {
    constexpr std::array<std::uint16_t, 8> mixed{0x0001, 0x7C01, 0x83FF, 0x3C00, 0xFE00, 0x0400, 0x7C00, 0x8000};
    constexpr std::array<std::uint16_t, 8> quiet{0x3555, 0xFBFF, 0x7E00, 0x0200, 0x7BFF, 0xFFFF, 0x8001, 0x0000};
    constexpr std::array<std::uint32_t, 5> modes{0x1F80, 0x9FC0, 0x1F80 | 0x6000, 0x1FBF, 0x9FC0 | 0x2000 | 0x20};
    std::vector<Case> cases;
    for (const bool wide : {false, true}) {
        for (const auto& pair : std::array<std::array<std::uint8_t, 2>, 7>{{{0, 1}, {3, 3}, {15, 14}, {8, 15}, {14, 8}, {7, 0}, {9, 9}}})
            cases.push_back(Case{halfToFloat(pair[0], pair[1], wide), wide, false, pair[0], pair[1]});
        for (const std::uint8_t dst : {0, 6, 12, 15})
            cases.push_back(Case{halfToFloatFromRax(dst, wide), wide, true, dst, 0});
    }
    for (const auto& site : cases) {
        const Checker checker(site);
        for (const auto mxcsr : modes) {
            checker.Check(mixed, mxcsr);
            checker.Check(quiet, mxcsr);
        }
    }
}

void execution() {
    exhaustive();
    registersAndModes();
    if (!hostF16c()) std::cout << "the host has no F16C: VCVTPH2PS was checked against the model only\n";
}

#else

void execution() { std::cout << "VCVTPH2PS execution checks require an x86-64 Linux, macOS or Windows build\n"; }

#endif

}

int main() {
    try {
        decoding();
        matching();
        execution();
        std::cout << "F16C lowering checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
