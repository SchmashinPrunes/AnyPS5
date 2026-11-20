# shader

## add Functional

- [v_ldexp_f64 in directed rounding modes](../../../core/shader/recompiler/Translation/src/Float64Instructions.cpp) - the PS5's gfx1013 is not measured. When the f16/f64 round field of the MODE register at the instruction rounds toward +infinity, toward -infinity or toward zero, an overflow gives the largest finite value of its sign where that mode rounds toward it, and a result below 2^-1022 with denormal results kept is rounded to the denormal grid in that mode from the exact scaled value, computed with 64-bit integers. With denormal results flushed, the flush to a zero of the result's sign applies in every round mode. A switch into a directed mode at run time still throws, as for the other f64 arithmetic. Measured on RDNA2 (gfx1036, Linux ROCr) with the hardware oracle over 2048 inputs around the overflow and denormal boundaries in each round mode, f16/f64 denormal mode and IEEE setting (65,536 rows), and over 262,144 more rows: no mismatch.
