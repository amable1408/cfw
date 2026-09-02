/*
 * io.c - Log-based printing of CFW math types.
 *
 * Every printer formats its value and emits it through log_message_1 at
 * LOG_LEVEL_INFO (reached via the <math/types.h> -> ... -> <log/log.h> chain).
 * Matrices are printed row-major even though storage is column-major, so the
 * output reads like standard mathematical notation.
 *
 * See io.h for API documentation and usage examples.
 */

#include <math/io.h>

/*==============================================================================
 * MARK: - IO API
 *============================================================================*/

void math_io_mat2_print_1(FSize const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    log_message_1(LOG_LEVEL_INFO, "mat2[\n  %.17g %.17g\n  %.17g %.17g\n]\n",
        (double) self[0], (double) self[2],
        (double) self[1], (double) self[3]);

    trace_log_pop();
}

void math_io_mat2_print_2(Mat2 const self) {
    trace_log_push(LOG_METADATA);

    log_message_1(LOG_LEVEL_INFO, "mat2[\n  %.17g %.17g\n  %.17g %.17g\n]\n",
        (double) self.m[0][0], (double) self.m[1][0],
        (double) self.m[0][1], (double) self.m[1][1]);

    trace_log_pop();
}

void math_io_mat3_print_1(FSize const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    log_message_1(LOG_LEVEL_INFO, "mat3[\n  %.17g %.17g %.17g\n  %.17g %.17g %.17g\n  %.17g %.17g %.17g\n]\n",
        (double) self[0], (double) self[3], (double) self[6],
        (double) self[1], (double) self[4], (double) self[7],
        (double) self[2], (double) self[5], (double) self[8]);

    trace_log_pop();
}

void math_io_mat3_print_2(Mat3 const self) {
    trace_log_push(LOG_METADATA);

    log_message_1(LOG_LEVEL_INFO, "mat3[\n  %.17g %.17g %.17g\n  %.17g %.17g %.17g\n  %.17g %.17g %.17g\n]\n",
        (double) self.m[0][0], (double) self.m[1][0], (double) self.m[2][0],
        (double) self.m[0][1], (double) self.m[1][1], (double) self.m[2][1],
        (double) self.m[0][2], (double) self.m[1][2], (double) self.m[2][2]);

    trace_log_pop();
}

void math_io_mat4_print_1(FSize const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    log_message_1(LOG_LEVEL_INFO,
        "mat4[\n  %.17g %.17g %.17g %.17g\n  %.17g %.17g %.17g %.17g\n  %.17g %.17g %.17g %.17g\n  %.17g %.17g %.17g %.17g\n]\n",
        (double) self[0], (double) self[4], (double) self[8],  (double) self[12],
        (double) self[1], (double) self[5], (double) self[9],  (double) self[13],
        (double) self[2], (double) self[6], (double) self[10], (double) self[14],
        (double) self[3], (double) self[7], (double) self[11], (double) self[15]);

    trace_log_pop();
}

void math_io_mat4_print_2(Mat4 const self) {
    trace_log_push(LOG_METADATA);

    log_message_1(LOG_LEVEL_INFO,
        "mat4[\n  %.17g %.17g %.17g %.17g\n  %.17g %.17g %.17g %.17g\n  %.17g %.17g %.17g %.17g\n  %.17g %.17g %.17g %.17g\n]\n",
        (double) self.m[0][0], (double) self.m[1][0], (double) self.m[2][0], (double) self.m[3][0],
        (double) self.m[0][1], (double) self.m[1][1], (double) self.m[2][1], (double) self.m[3][1],
        (double) self.m[0][2], (double) self.m[1][2], (double) self.m[2][2], (double) self.m[3][2],
        (double) self.m[0][3], (double) self.m[1][3], (double) self.m[2][3], (double) self.m[3][3]);

    trace_log_pop();
}

void math_io_quat_print_1(FSize const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    log_message_1(LOG_LEVEL_INFO, "quat(%.17g, %.17g, %.17g, %.17g)\n",
        (double) self[0], (double) self[1], (double) self[2], (double) self[3]);

    trace_log_pop();
}

void math_io_quat_print_2(Quat const self) {
    trace_log_push(LOG_METADATA);

    log_message_1(LOG_LEVEL_INFO, "quat(%.17g, %.17g, %.17g, %.17g)\n",
        (double) self.x, (double) self.y, (double) self.z, (double) self.w);

    trace_log_pop();
}

void math_io_vec2_print_1(FSize const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    log_message_1(LOG_LEVEL_INFO, "vec2(%.17g, %.17g)\n", (double) self[0], (double) self[1]);

    trace_log_pop();
}

void math_io_vec2_print_2(Vec2 const self) {
    trace_log_push(LOG_METADATA);

    log_message_1(LOG_LEVEL_INFO, "vec2(%.17g, %.17g)\n", (double) self.x, (double) self.y);

    trace_log_pop();
}

void math_io_vec3_print_1(FSize const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    log_message_1(LOG_LEVEL_INFO, "vec3(%.17g, %.17g, %.17g)\n",
        (double) self[0], (double) self[1], (double) self[2]);

    trace_log_pop();
}

void math_io_vec3_print_2(Vec3 const self) {
    trace_log_push(LOG_METADATA);

    log_message_1(LOG_LEVEL_INFO, "vec3(%.17g, %.17g, %.17g)\n",
        (double) self.x, (double) self.y, (double) self.z);

    trace_log_pop();
}

void math_io_vec4_print_1(FSize const *const self) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "self", (void*) self);

    log_message_1(LOG_LEVEL_INFO, "vec4(%.17g, %.17g, %.17g, %.17g)\n",
        (double) self[0], (double) self[1], (double) self[2], (double) self[3]);

    trace_log_pop();
}

void math_io_vec4_print_2(Vec4 const self) {
    trace_log_push(LOG_METADATA);

    log_message_1(LOG_LEVEL_INFO, "vec4(%.17g, %.17g, %.17g, %.17g)\n",
        (double) self.x, (double) self.y, (double) self.z, (double) self.w);

    trace_log_pop();
}