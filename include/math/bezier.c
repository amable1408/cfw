/*
 * bezier.c - Scalar Bezier/Hermite curve wrappers for the CFW math module.
 *
 * See bezier.h for API documentation and usage examples.
 */

#include <math/bezier.h>

/*==============================================================================
 * MARK: - Bezier API
 *============================================================================*/

FSize math_bezier_bezier(FSize const s, FSize const p0, FSize const c0, FSize const c1, FSize const p1) {
    return (FSize) glmc_bezier((float) s, (float) p0, (float) c0, (float) c1, (float) p1);
}

FSize math_bezier_decasteljau(FSize const s, FSize const p0, FSize const c0, FSize const c1, FSize const p1) {
    return (FSize) glmc_decasteljau((float) s, (float) p0, (float) c0, (float) c1, (float) p1);
}

FSize math_bezier_hermite(FSize const s, FSize const p0, FSize const t0, FSize const t1, FSize const p1) {
    return (FSize) glmc_hermite((float) s, (float) p0, (float) t0, (float) t1, (float) p1);
}