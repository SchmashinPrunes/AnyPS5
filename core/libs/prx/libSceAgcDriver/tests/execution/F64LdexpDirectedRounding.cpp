#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
using ShaderRecompiler::ShaderStage;

constexpr std::uint32_t Threads = 32;
constexpr std::uint32_t Inputs = 4;
constexpr std::uint32_t Results = 2;
alignas(256) std::array<std::uint32_t, Threads * Inputs> Input{};
alignas(256) std::array<std::uint32_t, Threads * Results> Output{};

alignas(256) constexpr std::array<std::uint32_t, 10> Code{
    0x34020084u, 0x34060083u, 0xe0381000u, 0x80000401u, 0xbf8c3f70u, 0xd568000au, 0x00020d04u, 0xe0741000u,
    0x80010a03u, 0xbf810000u,
};

constexpr std::uint64_t Source[Threads] = {
    0x3ff0000000000000ull, 0xbff0000000000000ull, 0x7fefffffffffffffull, 0xffefffffffffffffull,
    0x3ff8000000000000ull, 0xc008000000000000ull, 0x4000000000000000ull, 0xc000000000000000ull,
    0x3ff0000000000000ull, 0xbff0000000000000ull, 0x3ff0000000000000ull, 0xbff0000000000000ull,
    0x3ff8000000000000ull, 0xbff8000000000000ull, 0x3ff0000000000001ull, 0xbff0000000000001ull,
    0x3fffffffffffffffull, 0xbfffffffffffffffull, 0x3ff0000000000000ull, 0x3ff0000000000000ull,
    0xbff0000000000000ull, 0x7fefffffffffffffull, 0x3ffaaaaaaaaaaaabull, 0xbffaaaaaaaaaaaabull,
    0x3ff0000000000000ull, 0x400921fb54442d18ull, 0xc00921fb54442d18ull, 0x0000000000000000ull,
    0x8000000000000000ull, 0x7ff0000000000000ull, 0xfff0000000000000ull, 0x7ff8000000012345ull,
};

constexpr std::int32_t Exponent[Threads] = {
    1024, 1024, 1, 1, 2000, 1500, 2147483647, 2200,
    -1074, -1074, -1075, -1075, -1075, -1075, -1060, -1060,
    -1023, -1023, -1060, -2200, -2147483648, -2100, -1050, -1050,
    -1022, 10, -30, -1080, 1200, -2000, 5, -1075,
};

constexpr std::uint64_t Expected[7][Threads] = {
    {
        0x7ff0000000000000ull, 0xffefffffffffffffull, 0x7ff0000000000000ull, 0xffefffffffffffffull,
        0x7ff0000000000000ull, 0xffefffffffffffffull, 0x7ff0000000000000ull, 0xffefffffffffffffull,
        0x0000000000000001ull, 0x8000000000000001ull, 0x0000000000000001ull, 0x8000000000000000ull,
        0x0000000000000001ull, 0x8000000000000000ull, 0x0000000000004001ull, 0x8000000000004000ull,
        0x0010000000000000ull, 0x800fffffffffffffull, 0x0000000000004000ull, 0x0000000000000001ull,
        0x8000000000000000ull, 0x0000000000000001ull, 0x0000000001aaaaabull, 0x8000000001aaaaaaull,
        0x0010000000000000ull, 0x40a921fb54442d18ull, 0xbe2921fb54442d18ull, 0x0000000000000000ull,
        0x8000000000000000ull, 0x7ff0000000000000ull, 0xfff0000000000000ull, 0x7ff8000000012345ull,
    },
    {
        0x7fefffffffffffffull, 0xfff0000000000000ull, 0x7fefffffffffffffull, 0xfff0000000000000ull,
        0x7fefffffffffffffull, 0xfff0000000000000ull, 0x7fefffffffffffffull, 0xfff0000000000000ull,
        0x0000000000000001ull, 0x8000000000000001ull, 0x0000000000000000ull, 0x8000000000000001ull,
        0x0000000000000000ull, 0x8000000000000001ull, 0x0000000000004000ull, 0x8000000000004001ull,
        0x000fffffffffffffull, 0x8010000000000000ull, 0x0000000000004000ull, 0x0000000000000000ull,
        0x8000000000000001ull, 0x0000000000000000ull, 0x0000000001aaaaaaull, 0x8000000001aaaaabull,
        0x0010000000000000ull, 0x40a921fb54442d18ull, 0xbe2921fb54442d18ull, 0x0000000000000000ull,
        0x8000000000000000ull, 0x7ff0000000000000ull, 0xfff0000000000000ull, 0x7ff8000000012345ull,
    },
    {
        0x7fefffffffffffffull, 0xffefffffffffffffull, 0x7fefffffffffffffull, 0xffefffffffffffffull,
        0x7fefffffffffffffull, 0xffefffffffffffffull, 0x7fefffffffffffffull, 0xffefffffffffffffull,
        0x0000000000000001ull, 0x8000000000000001ull, 0x0000000000000000ull, 0x8000000000000000ull,
        0x0000000000000000ull, 0x8000000000000000ull, 0x0000000000004000ull, 0x8000000000004000ull,
        0x000fffffffffffffull, 0x800fffffffffffffull, 0x0000000000004000ull, 0x0000000000000000ull,
        0x8000000000000000ull, 0x0000000000000000ull, 0x0000000001aaaaaaull, 0x8000000001aaaaaaull,
        0x0010000000000000ull, 0x40a921fb54442d18ull, 0xbe2921fb54442d18ull, 0x0000000000000000ull,
        0x8000000000000000ull, 0x7ff0000000000000ull, 0xfff0000000000000ull, 0x7ff8000000012345ull,
    },
    {
        0x7ff0000000000000ull, 0xffefffffffffffffull, 0x7ff0000000000000ull, 0xffefffffffffffffull,
        0x7ff0000000000000ull, 0xffefffffffffffffull, 0x7ff0000000000000ull, 0xffefffffffffffffull,
        0x0000000000000000ull, 0x8000000000000000ull, 0x0000000000000000ull, 0x8000000000000000ull,
        0x0000000000000000ull, 0x8000000000000000ull, 0x0000000000000000ull, 0x8000000000000000ull,
        0x0000000000000000ull, 0x8000000000000000ull, 0x0000000000000000ull, 0x0000000000000000ull,
        0x8000000000000000ull, 0x0000000000000000ull, 0x0000000000000000ull, 0x8000000000000000ull,
        0x0010000000000000ull, 0x40a921fb54442d18ull, 0xbe2921fb54442d18ull, 0x0000000000000000ull,
        0x8000000000000000ull, 0x7ff0000000000000ull, 0xfff0000000000000ull, 0x7ff8000000012345ull,
    },
    {
        0x7fefffffffffffffull, 0xfff0000000000000ull, 0x7fefffffffffffffull, 0xfff0000000000000ull,
        0x7fefffffffffffffull, 0xfff0000000000000ull, 0x7fefffffffffffffull, 0xfff0000000000000ull,
        0x0000000000000000ull, 0x8000000000000000ull, 0x0000000000000000ull, 0x8000000000000000ull,
        0x0000000000000000ull, 0x8000000000000000ull, 0x0000000000000000ull, 0x8000000000000000ull,
        0x0000000000000000ull, 0x8000000000000000ull, 0x0000000000000000ull, 0x0000000000000000ull,
        0x8000000000000000ull, 0x0000000000000000ull, 0x0000000000000000ull, 0x8000000000000000ull,
        0x0010000000000000ull, 0x40a921fb54442d18ull, 0xbe2921fb54442d18ull, 0x0000000000000000ull,
        0x8000000000000000ull, 0x7ff0000000000000ull, 0xfff0000000000000ull, 0x7ff8000000012345ull,
    },
    {
        0x7fefffffffffffffull, 0xffefffffffffffffull, 0x7fefffffffffffffull, 0xffefffffffffffffull,
        0x7fefffffffffffffull, 0xffefffffffffffffull, 0x7fefffffffffffffull, 0xffefffffffffffffull,
        0x0000000000000000ull, 0x8000000000000000ull, 0x0000000000000000ull, 0x8000000000000000ull,
        0x0000000000000000ull, 0x8000000000000000ull, 0x0000000000000000ull, 0x8000000000000000ull,
        0x0000000000000000ull, 0x8000000000000000ull, 0x0000000000000000ull, 0x0000000000000000ull,
        0x8000000000000000ull, 0x0000000000000000ull, 0x0000000000000000ull, 0x8000000000000000ull,
        0x0010000000000000ull, 0x40a921fb54442d18ull, 0xbe2921fb54442d18ull, 0x0000000000000000ull,
        0x8000000000000000ull, 0x7ff0000000000000ull, 0xfff0000000000000ull, 0x7ff8000000012345ull,
    },
    {
        0x7fefffffffffffffull, 0xffefffffffffffffull, 0x7fefffffffffffffull, 0xffefffffffffffffull,
        0x7fefffffffffffffull, 0xffefffffffffffffull, 0x7fefffffffffffffull, 0xffefffffffffffffull,
        0x0000000000000001ull, 0x8000000000000001ull, 0x0000000000000000ull, 0x8000000000000000ull,
        0x0000000000000000ull, 0x8000000000000000ull, 0x0000000000004000ull, 0x8000000000004000ull,
        0x000fffffffffffffull, 0x800fffffffffffffull, 0x0000000000004000ull, 0x0000000000000000ull,
        0x8000000000000000ull, 0x0000000000000000ull, 0x0000000001aaaaaaull, 0x8000000001aaaaaaull,
        0x0010000000000000ull, 0x40a921fb54442d18ull, 0xbe2921fb54442d18ull, 0x0000000000000000ull,
        0x8000000000000000ull, 0x7ff0000000000000ull, 0xfff0000000000000ull, 0x7ff8000000012345ull,
    }
};

struct Mode {
    const char* name;
    ShaderRecompiler::ShaderFloatMode value;
};

std::array<std::uint32_t, 4> BufferDescriptor(const void* data, std::uint32_t bytes) {
    const auto address = reinterpret_cast<std::uintptr_t>(data);
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu), bytes, 0x31016facu};
}

std::string Hex(std::uint64_t value) {
    char text[24];
    std::snprintf(text, sizeof(text), "0x%016llx", static_cast<unsigned long long>(value));
    return text;
}

void Run(AgcDriver::VulkanDevice& device, const Mode& mode, const std::uint64_t (&expected)[Threads]) {
    for (std::uint32_t tid = 0; tid < Threads; ++tid) {
        Input[tid * Inputs] = static_cast<std::uint32_t>(Source[tid]);
        Input[tid * Inputs + 1u] = static_cast<std::uint32_t>(Source[tid] >> 32u);
        Input[tid * Inputs + 2u] = static_cast<std::uint32_t>(Exponent[tid]);
        Input[tid * Inputs + 3u] = 0u;
    }
    Output.fill(0xdeadbeefu);
    std::vector<std::uint32_t> userData(8, 0u);
    const auto input = BufferDescriptor(Input.data(), static_cast<std::uint32_t>(Input.size() * 4u));
    const auto output = BufferDescriptor(Output.data(), static_cast<std::uint32_t>(Output.size() * 4u));
    std::copy(input.begin(), input.end(), userData.begin());
    std::copy(output.begin(), output.end(), userData.begin() + 4);
    const std::span<const std::uint32_t> code(Code);
    const std::array<ShaderRecompiler::MemoryRegion, 1> memory{{{reinterpret_cast<std::uintptr_t>(code.data()), std::as_bytes(code)}}};
    const ShaderRecompiler::ShaderComputeStageInfo compute{{Threads, 1, 1}, 0u, {false, false, false}, false, 1};
    ShaderRecompiler::RecompileRequest request{
        {ShaderStage::Compute, reinterpret_cast<std::uintptr_t>(code.data()), code, 0, {}},
        {32, 0, userData, compute, std::nullopt, std::nullopt, memory},
        device.Target(),
        {0, 0, 0, 128}
    };
    request.useCache = false;
    request.context.floatMode = mode.value;
    const auto result = ShaderRecompiler::Recompile(request);
    device.Dispatch(result, 1, 1, 1, {}, reinterpret_cast<std::uintptr_t>(code.data()));
    device.WaitIdle();
    for (std::uint32_t tid = 0; tid < Threads; ++tid) {
        const std::uint64_t actual = Output[tid * Results] | (static_cast<std::uint64_t>(Output[tid * Results + 1u]) << 32u);
        Require(actual == expected[tid], std::string("v_ldexp_f64 ") + mode.name + " of " + Hex(Source[tid]) + " by " + std::to_string(Exponent[tid]) + " is " + Hex(actual) + ", expected " + Hex(expected[tid]));
    }
}

}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        if (!TargetHasCapability(device->Target(), spv::CapabilityFloat64)) {
            std::printf("skipped f64 ldexp directed rounding tests, the device lacks the Float64 capability\n");
            return 0;
        }
        const Mode modes[] = {
            {"toward +infinity, denormals kept", ShaderRecompiler::ShaderFloatMode{0xf4u, true, false, false}},
            {"toward -infinity, denormals kept", ShaderRecompiler::ShaderFloatMode{0xf8u, true, false, false}},
            {"toward zero, denormals kept", ShaderRecompiler::ShaderFloatMode{0xfcu, true, false, false}},
            {"toward +infinity, denormals flushed", ShaderRecompiler::ShaderFloatMode{0x34u, true, false, false}},
            {"toward -infinity, denormals flushed", ShaderRecompiler::ShaderFloatMode{0x38u, true, false, false}},
            {"toward zero, denormals flushed", ShaderRecompiler::ShaderFloatMode{0x3cu, true, false, false}},
            {"toward zero, denormals kept, IEEE mode on", ShaderRecompiler::ShaderFloatMode{0xfcu, true, true, false}},
        };
        for (std::size_t index = 0; index < std::size(modes); ++index) Run(*device, modes[index], Expected[index]);
        std::puts("f64 ldexp directed rounding tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
