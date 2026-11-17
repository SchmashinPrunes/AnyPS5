#include "VectorExecution.hpp"
#include <codegen/x86/F16cLowering.hpp>
#include <codegen/x86/IAmd64OnlyInstructionMatcher.hpp>
#include <codegen/CodegenException.hpp>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <utility>
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

Bytes floatToHalf(std::uint8_t dst, std::uint8_t src, bool wide256, std::uint8_t immediate, bool wideW = false, std::uint8_t vvvv = 0) {
    return {0xC4, static_cast<std::uint8_t>(((src & 8) ? 0 : 0x80) | 0x40 | ((dst & 8) ? 0 : 0x20) | 3),
        static_cast<std::uint8_t>((wideW ? 0x80 : 0) | ((~vvvv & 15) << 3) | (wide256 ? 4 : 0) | 1), 0x1D,
        static_cast<std::uint8_t>(0xC0 | ((src & 7) << 3) | (dst & 7)), immediate};
}

Bytes floatToHalfToRax(std::uint8_t src, bool wide256, std::uint8_t immediate) {
    return {0xC4, static_cast<std::uint8_t>(((src & 8) ? 0 : 0x80) | 0x60 | 3), static_cast<std::uint8_t>(0x78 | (wide256 ? 4 : 0) | 1), 0x1D,
        static_cast<std::uint8_t>((src & 7) << 3), immediate};
}

bool rejects(const Bytes& bytes) {
    try { (void)Codegen::DecodeHalfToFloat(bytes.data(), bytes.size()); }
    catch (const Codegen::CodegenException&) { return true; }
    return false;
}

bool rejectsFloatToHalf(const Bytes& bytes) {
    try { (void)Codegen::DecodeFloatToHalf(bytes.data(), bytes.size()); }
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
    for (const bool wide : {false, true}) {
        const auto bytes = floatToHalf(9, 12, wide, 0xFE);
        const auto operands = Codegen::DecodeFloatToHalf(bytes.data(), bytes.size());
        require(operands && operands->Wide == wide && operands->Destination == 9 && operands->Source == 12 && operands->Immediate == 0xFE && !operands->Memory, "VCVTPS2PH lost an operand");
        const auto stored = floatToHalfToRax(3, wide, 2);
        const auto memory = Codegen::DecodeFloatToHalf(stored.data(), stored.size());
        require(memory && memory->Wide == wide && memory->Source == 3 && memory->Immediate == 2 && memory->Memory, "a memory VCVTPS2PH lost its operand");
        require(rejectsFloatToHalf(floatToHalf(1, 2, wide, 0, true)), "VCVTPS2PH with VEX.W1 was accepted");
        require(rejectsFloatToHalf(floatToHalf(1, 2, wide, 0, false, 5)), "VCVTPS2PH with VEX.vvvv other than 1111b was accepted");
        require(rejectsFloatToHalf(Bytes(bytes.begin(), bytes.end() - 1)), "VCVTPS2PH without its immediate was accepted");
        auto trailing = bytes;
        trailing.push_back(0x90);
        require(rejectsFloatToHalf(trailing), "VCVTPS2PH with trailing bytes was accepted");
    }
    const Bytes ripStore{0x64, 0xC4, 0x63, 0x7D, 0x1D, 0x05, 0x10, 0, 0, 0, 0x04};
    const auto ripOperands = Codegen::DecodeFloatToHalf(ripStore.data(), ripStore.size());
    require(ripOperands && ripOperands->Wide && ripOperands->Source == 8 && ripOperands->Immediate == 4 && ripOperands->Memory && ripOperands->Memory->RipRelative && ripOperands->Memory->Prefixes == Bytes{0x64},
        "a RIP-relative VCVTPS2PH lost its prefix or operand");
    for (const Bytes bytes : {Bytes{0xC4, 0xE3, 0x78, 0x1D, 0xC1, 0}, Bytes{0xC4, 0xE2, 0x79, 0x1D, 0xC1}, Bytes{0xC4, 0xE3, 0x79, 0x1C, 0xC1, 0}, Bytes{0x90}})
        require(!Codegen::DecodeFloatToHalf(bytes.data(), bytes.size()), "an instruction that is not VCVTPS2PH was decoded as one");
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
        const auto store = floatToHalf(1, 0, wide, 0);
        require(!intel->Match(store.data(), store.size()) && !rosetta->Match(store.data(), store.size()), "Intel or Rosetta mode lowered VCVTPS2PH");
        const auto lowered = avx->Match(store.data(), store.size());
        require(lowered && lowered->InstructionName == "VCVTPS2PH" && lowered->Lowering == Codegen::Amd64OnlyLowering::Trampoline && lowered->RelocatableWindow && !lowered->Optional,
            "AVX mode did not lower VCVTPS2PH through a movable stub");
    }
}

#if LOWERING_EXECUTION_AVAILABLE

using VectorExecution::Machine;
using VectorExecution::Runner;

constexpr std::uint32_t kInvalidFlag = 1;

Bytes runnable(Codegen::StubBodyBuilder& body) {
    auto finished = body.Finish();
    auto bytes = finished.Bytes;
    const auto skip = static_cast<std::uint32_t>(bytes.size() - (finished.ReturnBranchOffset + 5));
    bytes[finished.ReturnBranchOffset] = 0xE9;
    for (unsigned index = 0; index < 4; ++index)
        bytes[finished.ReturnBranchOffset + 1 + index] = static_cast<std::uint8_t>(skip >> (8 * index));
    return bytes;
}

Bytes lowered(const Bytes& instruction) {
    Codegen::StubBodyBuilder body;
    Codegen::EmitHalfToFloat(body, *Codegen::DecodeHalfToFloat(instruction.data(), instruction.size()));
    return runnable(body);
}

Bytes loweredFloatToHalf(const Bytes& instruction) {
    Codegen::StubBodyBuilder body;
    Codegen::EmitFloatToHalf(body, *Codegen::DecodeFloatToHalf(instruction.data(), instruction.size()));
    return runnable(body);
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

struct Converted {
    std::uint16_t Half;
    std::uint32_t Flags;
};

constexpr std::uint32_t kDenormalFlag = 2;
constexpr std::uint32_t kOverflowFlag = 8;
constexpr std::uint32_t kUnderflowFlag = 0x10;
constexpr std::uint32_t kPrecisionFlag = 0x20;

std::pair<std::uint64_t, bool> roundShifted(std::uint64_t value, int shift, unsigned mode, bool negative) {
    if (shift <= 0) return {value << -shift, false};
    const auto quotient = shift >= 64 ? 0 : value >> shift;
    const auto remainder = shift >= 64 ? value : value & ((std::uint64_t{1} << shift) - 1);
    const bool inexact = remainder != 0;
    bool up = false;
    if (mode == 0) {
        const bool above = shift < 64 && remainder > (std::uint64_t{1} << (shift - 1));
        const bool tie = shift < 64 && remainder == (std::uint64_t{1} << (shift - 1));
        up = above || (tie && (quotient & 1) != 0);
    } else if (mode == 1) {
        up = inexact && negative;
    } else if (mode == 2) {
        up = inexact && !negative;
    }
    return {quotient + (up ? 1 : 0), inexact};
}

Converted toHalf(std::uint32_t bits, unsigned mode, bool daz) {
    const auto sign = static_cast<std::uint16_t>((bits >> 16) & 0x8000);
    auto magnitude = bits & 0x7FFFFFFFu;
    if (magnitude > 0x7F800000u) return {static_cast<std::uint16_t>(sign | 0x7E00 | ((magnitude >> 13) & 0x1FF)), (magnitude & 0x00400000u) != 0 ? 0u : kInvalidFlag};
    if (magnitude == 0x7F800000u) return {static_cast<std::uint16_t>(sign | 0x7C00), 0};
    std::uint32_t flags = 0;
    if (magnitude != 0 && magnitude < 0x00800000u) {
        if (daz) magnitude = 0;
        else flags |= kDenormalFlag;
    }
    if (magnitude == 0) return {sign, flags};
    const bool negative = sign != 0;
    const std::uint64_t mantissa = magnitude < 0x00800000u ? magnitude : (magnitude & 0x007FFFFFu) | 0x00800000u;
    const int exponent = magnitude < 0x00800000u ? -149 : static_cast<int>(magnitude >> 23) - 150;
    const int length = std::bit_width(mantissa);
    const int power = length - 1 + exponent;
    if (power >= -14) {
        auto [rounded, inexact] = roundShifted(mantissa, length - 11, mode, negative);
        int biased = power;
        if (rounded == 2048) {
            rounded = 1024;
            ++biased;
        }
        if (biased > 15) {
            const bool infinite = mode == 0 || (mode == 2 && !negative) || (mode == 1 && negative);
            return {static_cast<std::uint16_t>(sign | (infinite ? 0x7C00 : 0x7BFF)), flags | kOverflowFlag | kPrecisionFlag};
        }
        return {static_cast<std::uint16_t>(sign | ((biased + 15) << 10) | (rounded - 1024)), flags | (inexact ? kPrecisionFlag : 0)};
    }
    const auto [significant, unused] = roundShifted(mantissa, length - 11, mode, negative);
    (void)unused;
    const bool tiny = !(power == -15 && significant == 2048);
    const auto [rounded, inexact] = roundShifted(mantissa, -24 - exponent, mode, negative);
    if (inexact) flags |= kPrecisionFlag | (tiny ? kUnderflowFlag : 0);
    return {static_cast<std::uint16_t>(sign | rounded), flags};
}

struct StoreCase {
    Bytes Instruction;
    bool Wide;
    bool ToMemory;
    std::uint8_t Destination;
    std::uint8_t Source;
    std::uint8_t Immediate;
};

class StoreChecker {
public:
    explicit StoreChecker(const StoreCase& site) : _site(site), _lowered(loweredFloatToHalf(site.Instruction)), _native(site.Instruction) {}

    void Check(const std::array<std::uint32_t, 8>& floats, std::uint32_t mxcsr) const {
        auto start = VectorExecution::Initial();
        start.Mxcsr = mxcsr;
        start.Ymm[_site.Source] = {floats[0] | (static_cast<std::uint64_t>(floats[1]) << 32), floats[2] | (static_cast<std::uint64_t>(floats[3]) << 32),
            floats[4] | (static_cast<std::uint64_t>(floats[5]) << 32), floats[6] | (static_cast<std::uint64_t>(floats[7]) << 32)};
        auto machine = start;
        machine.Gpr[0] = reinterpret_cast<std::uint64_t>(machine.Memory.data());
        auto expected = machine;
        const unsigned lanes = _site.Wide ? 8 : 4;
        const unsigned mode = (_site.Immediate & 4) != 0 ? (mxcsr >> 13) & 3 : _site.Immediate & 3u;
        std::array<std::uint64_t, 2> packed{};
        std::uint32_t flags = 0;
        for (unsigned index = 0; index < lanes; ++index) {
            const auto converted = toHalf(floats[index], mode, (mxcsr & 0x40) != 0);
            packed[index / 4] |= static_cast<std::uint64_t>(converted.Half) << (16 * (index % 4));
            flags |= converted.Flags;
        }
        expected.Mxcsr |= flags;
        if (_site.ToMemory) {
            expected.Memory[0] = packed[0];
            if (_site.Wide) expected.Memory[1] = packed[1];
        } else {
            expected.Ymm[_site.Destination] = {packed[0], packed[1], 0, 0};
        }
        const auto what = std::string(_site.Wide ? "256-bit" : "128-bit") + " VCVTPS2PH imm " + std::to_string(_site.Immediate) + " dst " +
            (_site.ToMemory ? std::string("[rax]") : std::to_string(_site.Destination)) + " src " + std::to_string(_site.Source) + " first float " + std::to_string(floats[0]) +
            " mxcsr " + std::to_string(mxcsr);
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
    StoreCase _site;
    Runner _lowered;
    Runner _native;
};

constexpr std::array<std::uint32_t, 44> kEdgeFloats{
    0x00000000, 0x80000000, 0x3F800000, 0x3F801000, 0x3F802000, 0x3F801001, 0x3F803000, 0xBF801000, 0x477FE000, 0x477FF000, 0x477FEFFF, 0x47800000,
    0xC77FF000, 0x7F7FFFFF, 0xFF7FFFFF, 0x33800000, 0x33000000, 0x33000001, 0x32FFFFFF, 0x387FC000, 0x38800000, 0x387FF000, 0x387FF001, 0x387FEFFF,
    0x387FE000, 0x387FE001, 0xB87FF000, 0xB87FE000, 0x00000001, 0x807FFFFF, 0x80400000, 0x00800000, 0x7F800001, 0x7FC00001, 0xFFA00000, 0x7F800000,
    0xFF800000, 0x7FFFFFFF, 0xFFC00000, 0x7FBFFFFF, 0x38000000, 0x37FFFFFF, 0x3A801000, 0xBA802000};

void floatToHalfRegistersAndModes() {
    std::vector<StoreCase> cases;
    for (const bool wide : {false, true}) {
        for (const auto& pair : std::array<std::array<std::uint8_t, 2>, 6>{{{0, 1}, {3, 3}, {15, 14}, {8, 15}, {14, 8}, {9, 9}}})
            cases.push_back(StoreCase{floatToHalf(pair[0], pair[1], wide, 0), wide, false, pair[0], pair[1], 0});
        for (const std::uint8_t src : {0, 7, 15})
            cases.push_back(StoreCase{floatToHalfToRax(src, wide, 2), wide, true, 0, src, 2});
    }
    for (const auto& site : cases) {
        const StoreChecker checker(site);
        for (std::size_t first = 0; first < kEdgeFloats.size(); first += 4) {
            std::array<std::uint32_t, 8> floats{};
            for (unsigned index = 0; index < 8; ++index) floats[index] = kEdgeFloats[(first + index) % kEdgeFloats.size()];
            for (const std::uint32_t mxcsr : {0x1F80u, 0x9FC0u | 0x20u})
                checker.Check(floats, mxcsr);
        }
    }
}

constexpr std::size_t kSingles = 2048;

void floatToHalfRounding() {
    std::mt19937 random(20261117);
    std::vector<std::uint32_t> inputs(kEdgeFloats.begin(), kEdgeFloats.end());
    for (unsigned count = 0; count < 16384; ++count) {
        const auto bits = static_cast<std::uint32_t>(random());
        inputs.push_back(count % 2 == 0 ? bits : (bits & 0x807FFFFFu) | ((96 + bits % 64) << 23));
    }
    for (const std::uint8_t immediate : {0x00, 0x01, 0x02, 0x03, 0x04, 0xF9, 0xFE}) {
        for (const bool wide : {true, false}) {
            const StoreChecker checker(StoreCase{floatToHalf(2, 13, wide, immediate), wide, false, 2, 13, immediate});
            const unsigned lanes = wide ? 8 : 4;
            for (const std::uint32_t control : {0x1F80u, 0x1FC0u, 0x9F80u}) {
                for (std::uint32_t rounding = 0; rounding < ((immediate & 4) != 0 ? 4u : 1u); ++rounding) {
                    for (std::size_t first = 0; first < inputs.size(); first += lanes) {
                        std::array<std::uint32_t, 8> floats{};
                        for (unsigned index = 0; index < 8; ++index) floats[index] = inputs[(first + index % lanes) % inputs.size()];
                        checker.Check(floats, control | (rounding << 13));
                    }
                    for (std::size_t index = 0; index < kSingles; ++index) {
                        std::array<std::uint32_t, 8> floats{};
                        floats.fill(0x3F800000);
                        floats[index % lanes] = inputs[index];
                        checker.Check(floats, control | (rounding << 13));
                    }
                }
            }
        }
    }
}

void execution() {
    exhaustive();
    registersAndModes();
    floatToHalfRegistersAndModes();
    floatToHalfRounding();
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
