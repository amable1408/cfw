/*
 * test_io.c - Smoke tests for include/math/io.c (log-based printers).
 *
 * The printers emit through the framework log; we init the log to stdout, call
 * every printer (both _1 and _2 variants), and confirm each runs without
 * crashing. The log lines themselves are visible in the run output.
 */

#include <stdio.h>

#include <math/io.h>

#include "check.h"

// === Helpers ===

int main(void) {
    printf("=== io module tests (smoke) ===\n");

    LogConfig const config = {
        .level             = LOG_LEVEL_INFO,
        .stream            = stdout,
        .timestamp_enabled = false,
        .autoflush         = true
    };

    log_init(config);

    Vec2 const v2 = { 1.0, 2.0 };
    Vec3 const v3 = { 1.0, 2.0, 3.0 };
    Vec4 const v4 = { 1.0, 2.0, 3.0, 4.0 };
    Quat const q = { 0.0, 0.0, 0.0, 1.0 };
    Mat2 const m2 = { .m = { { 1.0, 2.0 }, { 3.0, 4.0 } } };
    Mat3 const m3 = { .m = { { 1.0, 0.0, 0.0 }, { 0.0, 2.0, 0.0 }, { 0.0, 0.0, 3.0 } } };
    Mat4 const m4 = { .m = {
        { 1.0, 0.0, 0.0, 0.0 },
        { 0.0, 1.0, 0.0, 0.0 },
        { 0.0, 0.0, 1.0, 0.0 },
        { 5.0, 6.0, 7.0, 1.0 }
    } };

    FSize p2[2] = { 1.0, 2.0 };
    FSize p3[3] = { 1.0, 2.0, 3.0 };
    FSize p4[4] = { 1.0, 2.0, 3.0, 4.0 };
    FSize pq[4] = { 0.0, 0.0, 0.0, 1.0 };
    FSize pm2[4] = { 1.0, 2.0, 3.0, 4.0 };
    FSize pm3[9] = { 1.0, 0.0, 0.0, 0.0, 2.0, 0.0, 0.0, 0.0, 3.0 };
    FSize pm4[16] = {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        5.0, 6.0, 7.0, 1.0
    };

    math_io_vec2_print_2(v2);
    _check_b("vec2_print_2 ran", true, true);
    math_io_vec2_print_1(p2);
    _check_b("vec2_print_1 ran", true, true);

    math_io_vec3_print_2(v3);
    _check_b("vec3_print_2 ran", true, true);
    math_io_vec3_print_1(p3);
    _check_b("vec3_print_1 ran", true, true);

    math_io_vec4_print_2(v4);
    _check_b("vec4_print_2 ran", true, true);
    math_io_vec4_print_1(p4);
    _check_b("vec4_print_1 ran", true, true);

    math_io_quat_print_2(q);
    _check_b("quat_print_2 ran", true, true);
    math_io_quat_print_1(pq);
    _check_b("quat_print_1 ran", true, true);

    math_io_mat2_print_2(m2);
    _check_b("mat2_print_2 ran", true, true);
    math_io_mat2_print_1(pm2);
    _check_b("mat2_print_1 ran", true, true);

    math_io_mat3_print_2(m3);
    _check_b("mat3_print_2 ran", true, true);
    math_io_mat3_print_1(pm3);
    _check_b("mat3_print_1 ran", true, true);

    math_io_mat4_print_2(m4);
    _check_b("mat4_print_2 ran", true, true);
    math_io_mat4_print_1(pm4);
    _check_b("mat4_print_1 ran", true, true);

    log_uninit();

    return _check_finish();
}