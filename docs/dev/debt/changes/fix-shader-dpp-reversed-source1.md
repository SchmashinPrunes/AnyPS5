# shader

## add Unknown function info

- [DPP on reversed integer VOP2 instructions](../../../core/shader/recompiler/RdnaDecoder/src/RdnaVectorOpDecoder.cpp) (shader recompiler) - the PS5's gfx1013 is not measured. `v_subrev_nc_u32`, `v_subrev_co_ci_u32`, `v_lshlrev_b32`, `v_lshrrev_b32` and `v_ashrrev_i32` with DPP16 or DPP8 apply the lane selection, bound control and fetch-inactive to vsrc1, not to src0, so the decoder moves them there. Measured on RDNA2 (gfx1036, Linux ROCr) through the [hardware oracle](../HW_ORACLE.md) with 16 DPP controls on 64 lanes: 852 of 852 decided lanes read vsrc1 from the selected lane. `v_subrev_f32` and `v_subrev_f16` select src0, as the RDNA 2 ISA describes, with and without neg and abs. LLVM's `SIInstrInfo::isSrc1DPPRevOpcode` lists the same integer forms, and the REV shifts before GFX11.
