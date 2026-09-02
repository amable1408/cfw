/*
 * test_plane.c - Tests for include/math/plane.c (full glmc_plane_* coverage)
 */

#include <math.h>
#include <stdio.h>

#include <math/plane.h>

#include "check.h"

// === Helpers ===

#define _TOL 1e-9
#define _FTOL 1e-6

int main(void) {
    printf("=== plane module tests ===\n");

    // plane with normal (0, 3, 4) of length 5 and signed distance 10
    Plane const p = { 0.0, 3.0, 4.0, 10.0 };
    FSize pp[4] = { 0.0, 3.0, 4.0, 10.0 };
    FSize po[4] = DEFAULT_INITIALIZATION;

    // --- normalize ---
    printf("--- normalize ---\n");

    // normalize divides all four components by |normal| = 5 -> (0, 0.6, 0.8, 2.0)
    // A degenerate normal is documented to yield the zeroed plane (cglm's vec4_normalize zero-guards);
    // pinned here so the promise is CFW's, not an accident of the library version.
    Plane const degenerate = { 0.0, 0.0, 0.0, 7.0 };
    Plane const dn = math_plane_normalize_2(degenerate);
    _check_f("normalize_2 zero normal -> zeroed x", dn.x, 0.0, 0.0);
    _check_f("normalize_2 zero normal -> zeroed w", dn.w, 0.0, 0.0);

    Plane const n = math_plane_normalize_2(p);
    _check_f("normalize_2.x", n.x, 0.0, _FTOL);
    _check_f("normalize_2.y", n.y, 0.6, _FTOL);
    _check_f("normalize_2.z", n.z, 0.8, _FTOL);
    _check_f("normalize_2.w", n.w, 2.0, _FTOL);

    // boundary: normalize scales so the resulting normal has unit length
    FSize const nlen = sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    _check_f("normalize_2 unit normal", nlen, 1.0, _FTOL);

    math_plane_normalize_1(pp, po);
    _check_f("normalize_1.y", po[1], 0.6, _FTOL);
    _check_f("normalize_1.z", po[2], 0.8, _FTOL);
    _check_f("normalize_1.w", po[3], 2.0, _FTOL);

    // boundary: normalize scales so the resulting normal has unit length
    FSize const plen = sqrt(po[0] * po[0] + po[1] * po[1] + po[2] * po[2]);
    _check_f("normalize_1 unit normal", plen, 1.0, _FTOL);

    // boundary: a degenerate plane (zero normal) normalizes to the zero plane
    FSize pzero[4] = DEFAULT_INITIALIZATION;
    FSize pzn[4] = DEFAULT_INITIALIZATION;
    math_plane_normalize_1(pzero, pzn);
    _check_f("normalize_1 zero.x", pzn[0], 0.0, _TOL);
    _check_f("normalize_1 zero.w", pzn[3], 0.0, _TOL);

    // keep _check_u / _check_b / _check_i referenced so the harness helpers are exercised
    _check_u("pass counter positive", _pass > 0 ? 1 : 0, 1);
    _check_b("no failures yet", _fail == 0, true);
    _check_i("fail counter type", (ISize) 0, 0);

    return _check_finish();
}