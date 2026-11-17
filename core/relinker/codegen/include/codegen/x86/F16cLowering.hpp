#ifndef CODEGEN_X86_F16CLOWERING_HPP
#define CODEGEN_X86_F16CLOWERING_HPP

#include <codegen/x86/StubBodyBuilder.hpp>
#include <cstdint>
#include <optional>

namespace Codegen {

struct HalfToFloatOperands {
    bool Wide;
    std::uint8_t Destination;
    std::uint8_t Source;
    std::optional<MemoryOperand> Memory;
};

struct FloatToHalfOperands {
    bool Wide;
    std::uint8_t Source;
    std::uint8_t Destination;
    std::uint8_t Immediate;
    std::optional<MemoryOperand> Memory;
};

[[nodiscard]] std::optional<HalfToFloatOperands> DecodeHalfToFloat(const std::uint8_t* data, std::size_t length);
void EmitHalfToFloat(StubBodyBuilder& body, const HalfToFloatOperands& operands);
[[nodiscard]] std::optional<FloatToHalfOperands> DecodeFloatToHalf(const std::uint8_t* data, std::size_t length);
void EmitFloatToHalf(StubBodyBuilder& body, const FloatToHalfOperands& operands);

}

#endif
