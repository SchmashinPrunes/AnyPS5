#include <codegen/x86/F16cLowering.hpp>
#include <codegen/x86/VexEncoding.hpp>
#include <array>
#include <vector>

namespace Codegen {

using namespace ScalarLowering;
using namespace VexEncoding;

namespace {

constexpr const char* kHalfToFloatName = "VCVTPH2PS";
constexpr std::int32_t kRedZone = 128;
constexpr std::int32_t kControlSlot = 16;
constexpr std::uint8_t kNoPrefix = 0;
constexpr Form kZeroExtendWords{Map0F38, Prefix66, false, false, 0x33};
constexpr Form kAnd{Map0F, Prefix66, false, false, 0xDB};
constexpr Form kAndNot{Map0F, Prefix66, false, false, 0xDF};
constexpr Form kOr{Map0F, Prefix66, false, false, 0xEB};
constexpr Form kExclusiveOr{Map0F, Prefix66, false, false, 0xEF};
constexpr Form kAddDwords{Map0F, Prefix66, false, false, 0xFE};
constexpr Form kSubtractDwords{Map0F, Prefix66, false, false, 0xFA};
constexpr Form kGreaterDwords{Map0F, Prefix66, false, false, 0x66};
constexpr Form kEqualDwords{Map0F, Prefix66, false, false, 0x76};
constexpr Form kShiftDwords{Map0F, Prefix66, false, false, 0x72};
constexpr Form kShiftBytes{Map0F, Prefix66, false, false, 0x73};
constexpr Form kShuffleDwords{Map0F, Prefix66, false, false, 0x70};
constexpr Form kIntegerToFloat{Map0F, kNoPrefix, false, false, 0x5B};
constexpr Form kBlendBytes{Map0F3A, Prefix66, false, false, 0x4C};
constexpr Form kLoadQword{Map0F, PrefixF3, false, false, 0x7E};
constexpr Form kLoadDword{Map0F, Prefix66, false, false, 0x6E};
constexpr Form kStoreDword{Map0F, Prefix66, false, false, 0x7E};
constexpr Form kControl{Map0F, kNoPrefix, false, false, 0xAE};
constexpr std::uint8_t kShiftLeft = 6;
constexpr std::uint8_t kShiftBytesRight = 3;
constexpr std::uint8_t kLoadControl = 2;
constexpr std::uint8_t kStoreControl = 3;
constexpr std::uint32_t kInvalidFlag = 1;

struct Registers {
    std::uint8_t Halfs;
    std::uint8_t Sign;
    std::uint8_t Result;
    std::uint8_t Mask;
    std::uint8_t Small;
    std::uint8_t Temporary;
    std::uint8_t Invalid;
};

void _constant(StubBodyBuilder& body, const Form& form, const std::uint8_t reg, const std::uint8_t vvvv, const std::uint32_t value) {
    auto bytes = Header(form, reg, vvvv, false, false);
    bytes.push_back(static_cast<std::uint8_t>(((reg & 7) << 3) | 5));
    const auto displacement = bytes.size();
    bytes.insert(bytes.end(), 4, 0);
    StubConstant constant{};
    for (std::size_t index = 0; index < constant.size(); ++index)
        constant[index] = static_cast<std::uint8_t>(value >> (8 * (index % 4)));
    body.RipConstant(bytes, displacement, constant);
}

void _halfsToFloats(StubBodyBuilder& body, const Registers& r, const std::uint8_t source, const std::uint8_t destination) {
    Register(body, kZeroExtendWords, r.Halfs, 0, source);
    _constant(body, kAnd, r.Sign, r.Halfs, 0x8000);
    Register(body, kShiftDwords, kShiftLeft, r.Sign, r.Sign, std::uint8_t{16});
    _constant(body, kAnd, r.Halfs, r.Halfs, 0x7FFF);
    Register(body, kShiftDwords, kShiftLeft, r.Result, r.Halfs, std::uint8_t{13});
    _constant(body, kAddDwords, r.Result, r.Result, 0x38000000);
    _constant(body, kGreaterDwords, r.Mask, r.Halfs, 0x7BFF);
    _constant(body, kAnd, r.Mask, r.Mask, 0x38000000);
    Register(body, kAddDwords, r.Result, r.Result, r.Mask);
    _constant(body, kGreaterDwords, r.Mask, r.Halfs, 0x7C00);
    _constant(body, kAnd, r.Small, r.Mask, 0x00400000);
    Register(body, kOr, r.Result, r.Result, r.Small);
    _constant(body, kAnd, r.Small, r.Halfs, 0x0200);
    _constant(body, kEqualDwords, r.Small, r.Small, 0x0200);
    Register(body, kAndNot, r.Small, r.Small, r.Mask);
    Register(body, kOr, r.Invalid, r.Invalid, r.Small);
    Register(body, kIntegerToFloat, r.Small, 0, r.Halfs);
    _constant(body, kSubtractDwords, r.Small, r.Small, 0x0C000000);
    _constant(body, kEqualDwords, r.Temporary, r.Halfs, 0);
    Register(body, kAndNot, r.Small, r.Temporary, r.Small);
    _constant(body, kGreaterDwords, r.Mask, r.Halfs, 0x03FF);
    Register(body, kBlendBytes, r.Result, r.Small, r.Result, static_cast<std::uint8_t>(r.Mask << 4));
    Register(body, kOr, destination, r.Result, r.Sign);
}

void _raiseInvalid(StubBodyBuilder& body, const Registers& r, const std::int32_t control) {
    Register(body, kShuffleDwords, r.Temporary, 0, r.Invalid, std::uint8_t{0x4E});
    Register(body, kOr, r.Invalid, r.Invalid, r.Temporary);
    Register(body, kShuffleDwords, r.Temporary, 0, r.Invalid, std::uint8_t{0xB1});
    Register(body, kOr, r.Invalid, r.Invalid, r.Temporary);
    _constant(body, kAnd, r.Invalid, r.Invalid, kInvalidFlag);
    VexEncoding::StackOperand(body, kControl, kStoreControl, 0, control);
    VexEncoding::StackOperand(body, kLoadDword, r.Temporary, 0, control);
    Register(body, kOr, r.Temporary, r.Temporary, r.Invalid);
    VexEncoding::StackOperand(body, kStoreDword, r.Temporary, 0, control);
    VexEncoding::StackOperand(body, kControl, kLoadControl, 0, control);
}

}

std::optional<HalfToFloatOperands> DecodeHalfToFloat(const std::uint8_t* data, const std::size_t length) {
    const auto instruction = Decode(data, length);
    if (!instruction || instruction->Map != Map0F38 || instruction->Prefix != Prefix66 || instruction->Opcode != 0x13)
        return std::nullopt;
    if (instruction->Wide) throw CodegenException("VCVTPH2PS with VEX.W1 is undefined");
    if (instruction->Vvvv != 0) throw CodegenException("VCVTPH2PS with VEX.vvvv other than 1111b is undefined");
    if (instruction->End != length) throw CodegenException("VCVTPH2PS has trailing bytes");
    return HalfToFloatOperands{instruction->Vector256, instruction->Reg, instruction->Rm, instruction->Memory};
}

void EmitHalfToFloat(StubBodyBuilder& body, const HalfToFloatOperands& operands) {
    std::array<bool, 16> used{};
    used[operands.Destination] = true;
    if (!operands.Memory) used[operands.Source] = true;
    const std::size_t count = 7 + (operands.Wide ? 2 : 0) + (operands.Memory ? 1 : 0);
    std::array<std::uint8_t, 10> scratch{};
    std::size_t taken = 0;
    for (std::uint8_t reg = 15; taken < count; --reg)
        if (!used[reg]) scratch[taken++] = reg;
    const auto control = static_cast<std::int32_t>(32 * count);
    const auto frame = kRedZone + control + kControlSlot;
    StackSlot(body, 0x8D, true, Stack, -frame);
    for (std::size_t index = 0; index < count; ++index)
        VexEncoding::StackOperand(body, MoveUnaligned256Store, scratch[index], 0, static_cast<std::int32_t>(index * 32));
    const Registers r{scratch[0], scratch[1], scratch[2], scratch[3], scratch[4], scratch[5], scratch[6]};
    Register(body, kExclusiveOr, r.Invalid, r.Invalid, r.Invalid);
    auto source = operands.Source;
    if (operands.Memory) {
        source = scratch[count - 1];
        VexEncoding::Memory(body, operands.Wide ? MoveUnaligned128Load : kLoadQword, source, 0, *operands.Memory, frame, kHalfToFloatName);
    }
    if (operands.Wide) {
        const auto upperHalfs = scratch[7];
        const auto upperFloats = scratch[8];
        Register(body, kShiftBytes, kShiftBytesRight, upperHalfs, source, std::uint8_t{8});
        _halfsToFloats(body, r, upperHalfs, upperFloats);
        _halfsToFloats(body, r, source, operands.Destination);
        InsertUpper(body, operands.Destination, operands.Destination, upperFloats);
    } else {
        _halfsToFloats(body, r, source, operands.Destination);
    }
    _raiseInvalid(body, r, control);
    for (std::size_t index = 0; index < count; ++index)
        VexEncoding::StackOperand(body, MoveUnaligned256Load, scratch[index], 0, static_cast<std::int32_t>(index * 32));
    StackSlot(body, 0x8D, true, Stack, frame);
}

}
