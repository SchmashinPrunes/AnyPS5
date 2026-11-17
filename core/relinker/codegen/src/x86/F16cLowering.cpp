#include <codegen/x86/F16cLowering.hpp>
#include <codegen/x86/VexEncoding.hpp>
#include <codegen/x86/VexStub.hpp>
#include <array>
#include <string>
#include <vector>

namespace Codegen {

using namespace ScalarLowering;
using namespace VexEncoding;
using namespace VexStub;

namespace {

constexpr const char* kHalfToFloatName = "VCVTPH2PS";
constexpr const char* kFloatToHalfName = "VCVTPS2PH";
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
constexpr Form kLoadVector{Map0F, PrefixF3, false, false, 0x6F};
constexpr Form kStoreVector{Map0F, PrefixF3, false, false, 0x7F};
constexpr Form kStoreQword{Map0F, Prefix66, false, false, 0xD6};
constexpr Form kAddFloats{Map0F, kNoPrefix, false, false, 0x58};
constexpr Form kSubtractFloats{Map0F, kNoPrefix, false, false, 0x5C};
constexpr Form kCompareFloats{Map0F, kNoPrefix, false, false, 0xC2};
constexpr Form kPackDwords{Map0F38, Prefix66, false, false, 0x2B};
constexpr std::uint8_t kShiftRight = 2;
constexpr std::uint8_t kShiftArithmetic = 4;
constexpr std::uint8_t kNotEqualOrdered = 0x0C;
constexpr std::uint32_t kDenormalFlag = 2;
constexpr std::uint32_t kOverflowFlag = 8;
constexpr std::uint32_t kUnderflowFlag = 0x10;
constexpr std::uint32_t kPrecisionFlag = 0x20;
constexpr std::size_t kFloatToHalfScratch = 12;
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

void _halfsToFloats(StubBodyBuilder& body, const Registers& r, const std::uint8_t source, const std::uint8_t destination) {
    Register(body, kZeroExtendWords, r.Halfs, 0, source);
    Constant(body, kAnd, r.Sign, r.Halfs, 0x8000);
    Register(body, kShiftDwords, kShiftLeft, r.Sign, r.Sign, std::uint8_t{16});
    Constant(body, kAnd, r.Halfs, r.Halfs, 0x7FFF);
    Register(body, kShiftDwords, kShiftLeft, r.Result, r.Halfs, std::uint8_t{13});
    Constant(body, kAddDwords, r.Result, r.Result, 0x38000000);
    Constant(body, kGreaterDwords, r.Mask, r.Halfs, 0x7BFF);
    Constant(body, kAnd, r.Mask, r.Mask, 0x38000000);
    Register(body, kAddDwords, r.Result, r.Result, r.Mask);
    Constant(body, kGreaterDwords, r.Mask, r.Halfs, 0x7C00);
    Constant(body, kAnd, r.Small, r.Mask, 0x00400000);
    Register(body, kOr, r.Result, r.Result, r.Small);
    Constant(body, kAnd, r.Small, r.Halfs, 0x0200);
    Constant(body, kEqualDwords, r.Small, r.Small, 0x0200);
    Register(body, kAndNot, r.Small, r.Small, r.Mask);
    Register(body, kOr, r.Invalid, r.Invalid, r.Small);
    Register(body, kIntegerToFloat, r.Small, 0, r.Halfs);
    Constant(body, kSubtractDwords, r.Small, r.Small, 0x0C000000);
    Constant(body, kEqualDwords, r.Temporary, r.Halfs, 0);
    Register(body, kAndNot, r.Small, r.Temporary, r.Small);
    Constant(body, kGreaterDwords, r.Mask, r.Halfs, 0x03FF);
    Register(body, kBlendBytes, r.Result, r.Small, r.Result, static_cast<std::uint8_t>(r.Mask << 4));
    Register(body, kOr, destination, r.Result, r.Sign);
}

void _raiseInvalid(StubBodyBuilder& body, const Registers& r, const std::int32_t control) {
    OrReduce(body, r.Invalid, r.Temporary);
    Constant(body, kAnd, r.Invalid, r.Invalid, kInvalidFlag);
    VexEncoding::StackOperand(body, kControl, kStoreControl, 0, control);
    RaiseFlags(body, r.Invalid, r.Temporary, control, control);
}

struct ModeSlots {
    std::int32_t Near;
    std::int32_t Up;
    std::int32_t Down;
    std::int32_t Flush;
};

std::uint8_t _floatsToHalfs(StubBodyBuilder& body, ScratchPool& pool, const ModeSlots& slots, const std::uint8_t input, const bool ownsInput, const std::uint8_t flags) {
    const auto sign = pool.Take();
    Constant(body, kAnd, sign, input, 0x80000000);
    const auto magnitude = pool.Take();
    Constant(body, kAnd, magnitude, input, 0x7FFFFFFF);
    if (ownsInput) pool.Give(input);

    const auto flushed = pool.Take();
    Constant(body, kGreaterDwords, flushed, magnitude, 0x007FFFFF);
    const auto denormal = pool.Take();
    Constant(body, kGreaterDwords, denormal, magnitude, 0);
    Register(body, kAndNot, denormal, flushed, denormal);
    VexEncoding::StackOperand(body, kAnd, flushed, denormal, slots.Flush);
    Register(body, kAndNot, magnitude, flushed, magnitude);
    Register(body, kAndNot, denormal, flushed, denormal);
    pool.Give(flushed);
    const auto bits = pool.Take();
    Constant(body, kAnd, bits, denormal, kDenormalFlag);
    pool.Give(denormal);

    const auto negative = pool.Take();
    Register(body, kShiftDwords, kShiftArithmetic, negative, sign, std::uint8_t{31});
    const auto away = pool.Take();
    VexEncoding::StackOperand(body, kAndNot, away, negative, slots.Up);
    VexEncoding::StackOperand(body, kAnd, negative, negative, slots.Down);
    Register(body, kOr, away, away, negative);
    pool.Give(negative);

    const auto special = pool.Take();
    Constant(body, kGreaterDwords, special, magnitude, 0x7F7FFFFF);
    const auto normal = pool.Take();
    Constant(body, kGreaterDwords, normal, magnitude, 0x387FFFFF);

    const auto half = pool.Take();
    Constant(body, kOr, half, sign, 0x3F000000);
    const auto value = pool.Take();
    Register(body, kOr, value, sign, magnitude);
    const auto rounded = pool.Take();
    Register(body, kAddFloats, rounded, value, half);
    const auto inexact = pool.Take();
    Register(body, kSubtractFloats, inexact, rounded, half);
    pool.Give(half);
    Register(body, kCompareFloats, inexact, inexact, value, kNotEqualOrdered);
    pool.Give(value);
    Constant(body, kAnd, rounded, rounded, 0x7FFFFFFF);
    Constant(body, kSubtractDwords, rounded, rounded, 0x3F000000);

    const auto first = pool.Take();
    const auto second = pool.Take();
    VexEncoding::StackOperand(body, kOr, first, away, slots.Near);
    Constant(body, kAndNot, first, first, 0x38800000);
    VexEncoding::StackOperand(body, kLoadVector, second, 0, slots.Near);
    Constant(body, kAnd, second, second, 0x387FF000);
    Register(body, kOr, first, first, second);
    Constant(body, kAnd, second, away, 0x387FE001);
    Register(body, kOr, first, first, second);
    Register(body, kGreaterDwords, first, first, magnitude);
    Register(body, kAnd, first, first, inexact);
    Register(body, kAndNot, first, normal, first);
    Constant(body, kAnd, first, first, kUnderflowFlag);
    Register(body, kOr, bits, bits, first);

    Register(body, kShiftDwords, kShiftRight, first, magnitude, std::uint8_t{13});
    Constant(body, kAnd, first, first, 1);
    Constant(body, kAddDwords, first, first, 0x0FFF);
    VexEncoding::StackOperand(body, kAnd, first, first, slots.Near);
    Constant(body, kAnd, second, away, 0x1FFF);
    Register(body, kOr, first, first, second);
    Constant(body, kSubtractDwords, second, magnitude, 0x38000000);
    Register(body, kAddDwords, second, second, first);
    Register(body, kShiftDwords, kShiftRight, second, second, std::uint8_t{13});
    Constant(body, kGreaterDwords, first, second, 0x7BFF);
    const auto limit = pool.Take();
    VexEncoding::StackOperand(body, kOr, limit, away, slots.Near);
    pool.Give(away);
    Constant(body, kAnd, limit, limit, 1);
    Constant(body, kAddDwords, limit, limit, 0x7BFF);
    Register(body, kBlendBytes, second, second, limit, static_cast<std::uint8_t>(first << 4));
    Constant(body, kAnd, limit, magnitude, 0x1FFF);
    Constant(body, kEqualDwords, limit, limit, 0);
    Constant(body, kAndNot, limit, limit, 0xFFFFFFFF);
    Register(body, kOr, limit, limit, first);
    Register(body, kAnd, first, first, normal);
    Register(body, kAndNot, first, special, first);
    Constant(body, kAnd, first, first, kOverflowFlag);
    Register(body, kOr, bits, bits, first);

    Register(body, kBlendBytes, rounded, rounded, second, static_cast<std::uint8_t>(normal << 4));
    Register(body, kBlendBytes, inexact, inexact, limit, static_cast<std::uint8_t>(normal << 4));
    pool.Give(limit);
    pool.Give(normal);
    Register(body, kAndNot, inexact, special, inexact);
    Constant(body, kAnd, inexact, inexact, kPrecisionFlag);
    Register(body, kOr, bits, bits, inexact);
    pool.Give(inexact);

    Constant(body, kGreaterDwords, first, magnitude, 0x7F800000);
    Constant(body, kAnd, second, magnitude, 0x00400000);
    Constant(body, kEqualDwords, second, second, 0x00400000);
    Register(body, kAndNot, second, second, first);
    Constant(body, kAnd, second, second, kInvalidFlag);
    Register(body, kOr, bits, bits, second);
    Register(body, kShiftDwords, kShiftRight, second, magnitude, std::uint8_t{13});
    pool.Give(magnitude);
    Constant(body, kAnd, second, second, 0x01FF);
    Constant(body, kOr, second, second, 0x7E00);
    Register(body, kAnd, second, second, first);
    Constant(body, kAndNot, first, first, 0x7C00);
    Register(body, kOr, second, second, first);
    pool.Give(first);
    Register(body, kBlendBytes, rounded, rounded, second, static_cast<std::uint8_t>(special << 4));
    pool.Give(second);
    pool.Give(special);

    Register(body, kShiftDwords, kShiftRight, sign, sign, std::uint8_t{16});
    Register(body, kOr, rounded, rounded, sign);
    pool.Give(sign);
    Register(body, kOr, flags, flags, bits);
    pool.Give(bits);
    return rounded;
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

std::optional<FloatToHalfOperands> DecodeFloatToHalf(const std::uint8_t* data, const std::size_t length) {
    const auto instruction = Decode(data, length);
    if (!instruction || instruction->Map != Map0F3A || instruction->Prefix != Prefix66 || instruction->Opcode != 0x1D)
        return std::nullopt;
    if (instruction->Wide) throw CodegenException("VCVTPS2PH with VEX.W1 is undefined");
    if (instruction->Vvvv != 0) throw CodegenException("VCVTPS2PH with VEX.vvvv other than 1111b is undefined");
    if (instruction->End >= length) throw CodegenException("VCVTPS2PH is missing its immediate");
    if (instruction->End + 1 != length) throw CodegenException("VCVTPS2PH has trailing bytes");
    return FloatToHalfOperands{instruction->Vector256, instruction->Reg, instruction->Rm, data[instruction->End], instruction->Memory};
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

void EmitFloatToHalf(StubBodyBuilder& body, const FloatToHalfOperands& operands) {
    std::array<bool, 16> used{};
    used[operands.Source] = true;
    if (!operands.Memory) used[operands.Destination] = true;
    std::array<std::uint8_t, 16> scratch{};
    std::size_t taken = 0;
    for (std::uint8_t reg = 15; taken < kFloatToHalfScratch; --reg)
        if (!used[reg]) scratch[taken++] = reg;
    const auto base = static_cast<std::int32_t>(32 * kFloatToHalfScratch);
    const ModeSlots slots{base, base + 16, base + 32, base + 48};
    const auto upperSlot = base + 64;
    const auto saved = base + 80;
    const auto work = base + 84;
    const auto frame = kRedZone + base + 96;
    StackSlot(body, 0x8D, true, Stack, -frame);
    for (std::size_t index = 0; index < kFloatToHalfScratch; ++index)
        VexEncoding::StackOperand(body, MoveUnaligned256Store, scratch[index], 0, static_cast<std::int32_t>(index * 32));
    ScratchPool pool(scratch, kFloatToHalfScratch, kFloatToHalfName);

    VexEncoding::StackOperand(body, kControl, kStoreControl, 0, saved);
    const auto mode = pool.Take();
    if ((operands.Immediate & 4) != 0) {
        VexEncoding::StackOperand(body, kLoadDword, mode, 0, saved);
        Register(body, kShiftDwords, kShiftRight, mode, mode, std::uint8_t{13});
        Constant(body, kAnd, mode, mode, 3);
        Register(body, kShuffleDwords, mode, 0, mode, std::uint8_t{0});
    } else {
        Constant(body, kLoadVector, mode, 0, operands.Immediate & 3u);
    }
    const auto mask = pool.Take();
    Constant(body, kEqualDwords, mask, mode, 0);
    VexEncoding::StackOperand(body, kStoreVector, mask, 0, slots.Near);
    Constant(body, kEqualDwords, mask, mode, 2);
    VexEncoding::StackOperand(body, kStoreVector, mask, 0, slots.Up);
    Constant(body, kEqualDwords, mask, mode, 1);
    VexEncoding::StackOperand(body, kStoreVector, mask, 0, slots.Down);
    Register(body, kShiftDwords, kShiftLeft, mode, mode, std::uint8_t{13});
    Constant(body, kOr, mode, mode, 0x1F80);
    VexEncoding::StackOperand(body, kStoreDword, mode, 0, work);
    VexEncoding::StackOperand(body, kLoadDword, mask, 0, saved);
    Constant(body, kAnd, mask, mask, 0x40);
    Constant(body, kEqualDwords, mask, mask, 0x40);
    Register(body, kShuffleDwords, mask, 0, mask, std::uint8_t{0});
    VexEncoding::StackOperand(body, kStoreVector, mask, 0, slots.Flush);
    pool.Give(mask);
    pool.Give(mode);
    VexEncoding::StackOperand(body, kControl, kLoadControl, 0, work);

    const auto flags = pool.Take();
    Register(body, kExclusiveOr, flags, flags, flags);
    std::uint8_t lower = 0;
    std::uint8_t upper = 0;
    if (operands.Wide) {
        const auto extracted = pool.Take();
        ExtractUpper(body, extracted, operands.Source);
        const auto halfs = _floatsToHalfs(body, pool, slots, extracted, true, flags);
        VexEncoding::StackOperand(body, kStoreVector, halfs, 0, upperSlot);
        pool.Give(halfs);
        lower = _floatsToHalfs(body, pool, slots, operands.Source, false, flags);
        upper = pool.Take();
        VexEncoding::StackOperand(body, kLoadVector, upper, 0, upperSlot);
    } else {
        lower = _floatsToHalfs(body, pool, slots, operands.Source, false, flags);
        upper = pool.Take();
        Register(body, kExclusiveOr, upper, upper, upper);
    }
    if (operands.Memory) {
        Register(body, kPackDwords, lower, lower, upper);
        VexEncoding::Memory(body, operands.Wide ? kStoreVector : kStoreQword, lower, 0, *operands.Memory, frame, kFloatToHalfName);
    } else {
        Register(body, kPackDwords, operands.Destination, lower, upper);
    }
    pool.Give(lower);
    OrReduce(body, flags, upper);
    RaiseFlags(body, flags, upper, saved, work);
    pool.Give(upper);
    pool.Give(flags);

    for (std::size_t index = 0; index < kFloatToHalfScratch; ++index)
        VexEncoding::StackOperand(body, MoveUnaligned256Load, scratch[index], 0, static_cast<std::int32_t>(index * 32));
    StackSlot(body, 0x8D, true, Stack, frame);
}

}
