# relinker

## change applies the BMI1, BMI2, CLWB and RDPID lowerings

- `--to-avx` ([matcher](../../../core/relinker/codegen/src/x86/Amd64OnlyInstructionMatcher.cpp)) applies the BMI1, BMI2, CLWB and RDPID lowerings of `--to-rosetta` and adds TZCNT, LZCNT and MOVBE, for hosts with AVX but without AVX2, such as the Sandy Bridge and Ivy Bridge CPUs of 2011 and 2012 Macs. AVX2, VCVTPS2PH (F16C), FMA, PDEP, PEXT, RDRAND and RDSEED are not lowered, so a title that reaches one of them still raises an illegal instruction; Sandy Bridge has neither RDRAND nor RDSEED, so the `--to-rosetta` RDSEED-to-RDRAND rewrite would not help there. It requires `--macos`: the Linux and Windows runtimes stop at start-up when the host lacks BMI1, BMI2, LZCNT, MOVBE or AVX2 and do not yet read which of them a conversion lowered.

## add Functional

- [Half-to-float conversion](../../../core/relinker/codegen/src/x86/F16cLowering.cpp) under `--to-avx` runs VCVTPH2PS (F16C) as integer instructions and an exact VCVTDQ2PS of each denormal half's mantissa, so MXCSR.DAZ, FTZ and RC do not change the result, as on the hardware. A signaling NaN sets MXCSR.IE as the native instruction does, but an unmasked invalid exception does not trap: the stub writes the result and only sets the flag. Each conversion becomes about 50 instructions (128-bit) or 80 (256-bit), with seven to ten scratch YMM registers saved below the red zone; RFLAGS and the general-purpose registers are not written. The execution test compares all 65,536 half values with a model of the SDM and, on a host with F16C, with the native instruction; precise fault delivery inside the stub has not been verified.
