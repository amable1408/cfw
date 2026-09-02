/*
 * rect.c - 2D rectangle operations for the CFW math module.
 *
 * See rect.h for API documentation and usage examples. Every operation is direct
 * FSize arithmetic on the (x, y, w, h) form; there is no cglm counterpart to bridge.
 */

#include <math/rect.h>

/*==============================================================================
 * MARK: - Internal helpers
 *============================================================================*/

static FSize _math_rect_clamp(FSize const value, FSize const lo, FSize const hi) {
    return math_min_f(math_max_f(value, lo), hi);
}

/*==============================================================================
 * MARK: - Rect API
 *============================================================================*/

FSize math_rect_area_1(FSize const *const r) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "r", (void*) r);

    FSize const result = r[2] * r[3];

    trace_log_pop();

    return result;
}

FSize math_rect_area_2(Rect const r) {
    trace_log_push(LOG_METADATA);

    FSize const result = r.w * r.h;

    trace_log_pop();

    return result;
}

FSize math_rect_bottom_1(FSize const *const r) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "r", (void*) r);

    FSize const result = r[1] + r[3];

    trace_log_pop();

    return result;
}

FSize math_rect_bottom_2(Rect const r) {
    trace_log_push(LOG_METADATA);

    FSize const result = r.y + r.h;

    trace_log_pop();

    return result;
}

void math_rect_bottom_left_1(FSize const *const r, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "r", (void*) r);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    dest[0] = r[0];
    dest[1] = r[1] + r[3];

    trace_log_pop();
}

Vec2 math_rect_bottom_left_2(Rect const r) {
    trace_log_push(LOG_METADATA);

    Vec2 const result = { r.x, r.y + r.h };

    trace_log_pop();

    return result;
}

void math_rect_bottom_right_1(FSize const *const r, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "r", (void*) r);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    dest[0] = r[0] + r[2];
    dest[1] = r[1] + r[3];

    trace_log_pop();
}

Vec2 math_rect_bottom_right_2(Rect const r) {
    trace_log_push(LOG_METADATA);

    Vec2 const result = { r.x + r.w, r.y + r.h };

    trace_log_pop();

    return result;
}

void math_rect_center_1(FSize const *const r, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "r", (void*) r);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    dest[0] = r[0] + r[2] / 2;
    dest[1] = r[1] + r[3] / 2;

    trace_log_pop();
}

Vec2 math_rect_center_2(Rect const r) {
    trace_log_push(LOG_METADATA);

    Vec2 const result = { r.x + r.w / 2, r.y + r.h / 2 };

    trace_log_pop();

    return result;
}

void math_rect_center_in_1(FSize const *const inner, FSize const *const outer, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "inner", (void*) inner);
    error_check_null(LOG_METADATA, "outer", (void*) outer);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    dest[0] = outer[0] + (outer[2] - inner[2]) / 2;
    dest[1] = outer[1] + (outer[3] - inner[3]) / 2;
    dest[2] = inner[2];
    dest[3] = inner[3];

    trace_log_pop();
}

Rect math_rect_center_in_2(Rect const inner, Rect const outer) {
    trace_log_push(LOG_METADATA);

    Rect const result = {
        outer.x + (outer.w - inner.w) / 2,
        outer.y + (outer.h - inner.h) / 2,
        inner.w,
        inner.h
    };

    trace_log_pop();

    return result;
}

void math_rect_clamp_point_1(FSize const *const r, FSize const *const point, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "r", (void*) r);
    error_check_null(LOG_METADATA, "point", (void*) point);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    dest[0] = _math_rect_clamp(point[0], r[0], r[0] + r[2]);
    dest[1] = _math_rect_clamp(point[1], r[1], r[1] + r[3]);

    trace_log_pop();
}

Vec2 math_rect_clamp_point_2(Rect const r, Vec2 const point) {
    trace_log_push(LOG_METADATA);

    Vec2 const result = {
        _math_rect_clamp(point.x, r.x, r.x + r.w),
        _math_rect_clamp(point.y, r.y, r.y + r.h)
    };

    trace_log_pop();

    return result;
}

void math_rect_constrain_1(FSize const *const inner, FSize const *const bounds, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "inner", (void*) inner);
    error_check_null(LOG_METADATA, "bounds", (void*) bounds);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    dest[0] = _math_rect_clamp(inner[0], bounds[0], bounds[0] + bounds[2] - inner[2]);
    dest[1] = _math_rect_clamp(inner[1], bounds[1], bounds[1] + bounds[3] - inner[3]);
    dest[2] = inner[2];
    dest[3] = inner[3];

    trace_log_pop();
}

Rect math_rect_constrain_2(Rect const inner, Rect const bounds) {
    trace_log_push(LOG_METADATA);

    Rect const result = {
        _math_rect_clamp(inner.x, bounds.x, bounds.x + bounds.w - inner.w),
        _math_rect_clamp(inner.y, bounds.y, bounds.y + bounds.h - inner.h),
        inner.w,
        inner.h
    };

    trace_log_pop();

    return result;
}

bool math_rect_contains_point_1(FSize const *const r, FSize const *const point) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "r", (void*) r);
    error_check_null(LOG_METADATA, "point", (void*) point);

    bool const result = point[0] >= r[0]
                      && point[0] <= r[0] + r[2]
                      && point[1] >= r[1]
                      && point[1] <= r[1] + r[3];

    trace_log_pop();

    return result;
}

bool math_rect_contains_point_2(Rect const r, Vec2 const point) {
    trace_log_push(LOG_METADATA);

    bool const result = point.x >= r.x
                      && point.x <= r.x + r.w
                      && point.y >= r.y
                      && point.y <= r.y + r.h;

    trace_log_pop();

    return result;
}

bool math_rect_contains_rect_1(FSize const *const r, FSize const *const other) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "r", (void*) r);
    error_check_null(LOG_METADATA, "other", (void*) other);

    bool const result = other[0] >= r[0]
                      && other[1] >= r[1]
                      && other[0] + other[2] <= r[0] + r[2]
                      && other[1] + other[3] <= r[1] + r[3];

    trace_log_pop();

    return result;
}

bool math_rect_contains_rect_2(Rect const r, Rect const other) {
    trace_log_push(LOG_METADATA);

    bool const result = other.x >= r.x
                      && other.y >= r.y
                      && other.x + other.w <= r.x + r.w
                      && other.y + other.h <= r.y + r.h;

    trace_log_pop();

    return result;
}

bool math_rect_equal_1(FSize const *const a, FSize const *const b) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);

    bool const result = a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];

    trace_log_pop();

    return result;
}

bool math_rect_equal_2(Rect const a, Rect const b) {
    trace_log_push(LOG_METADATA);

    bool const result = a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;

    trace_log_pop();

    return result;
}

void math_rect_fit_aspect_1(FSize const *const inner, FSize const *const outer, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "inner", (void*) inner);
    error_check_null(LOG_METADATA, "outer", (void*) outer);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    /* A zero-sized inner rect would divide to Inf and scale to NaN; refuse to the empty
     * rect, as the grid-cell sibling below refuses a zero column count. */
    if (!(inner[2] > 0) || !(inner[3] > 0)) {
        dest[0] = dest[1] = dest[2] = dest[3] = 0;

        trace_log_pop();

        return;
    }

    FSize const scale = math_min_f(outer[2] / inner[2], outer[3] / inner[3]);
    FSize const width = inner[2] * scale;
    FSize const height = inner[3] * scale;

    dest[0] = outer[0] + (outer[2] - width) / 2;
    dest[1] = outer[1] + (outer[3] - height) / 2;
    dest[2] = width;
    dest[3] = height;

    trace_log_pop();
}

Rect math_rect_fit_aspect_2(Rect const inner, Rect const outer) {
    trace_log_push(LOG_METADATA);

    if (!(inner.w > 0) || !(inner.h > 0)) {
        Rect const empty = DEFAULT_INITIALIZATION;

        trace_log_pop();

        return empty;
    }

    FSize const scale = math_min_f(outer.w / inner.w, outer.h / inner.h);
    FSize const width = inner.w * scale;
    FSize const height = inner.h * scale;

    Rect const result = {
        outer.x + (outer.w - width) / 2,
        outer.y + (outer.h - height) / 2,
        width,
        height
    };

    trace_log_pop();

    return result;
}

void math_rect_from_aabb2d_1(FSize const *const aabb, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "aabb", (void*) aabb);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    /* Read all four before writing: dest may overlap aabb, and the second pair reads the first. */
    FSize const min_x = aabb[0];
    FSize const min_y = aabb[1];
    FSize const max_x = aabb[2];
    FSize const max_y = aabb[3];

    dest[0] = min_x;
    dest[1] = min_y;
    dest[2] = max_x - min_x;
    dest[3] = max_y - min_y;

    trace_log_pop();
}

Rect math_rect_from_aabb2d_2(Aabb2d const aabb) {
    trace_log_push(LOG_METADATA);

    Rect const result = { aabb.min.x, aabb.min.y, aabb.max.x - aabb.min.x, aabb.max.y - aabb.min.y };

    trace_log_pop();

    return result;
}

void math_rect_from_center_1(FSize const *const center, FSize const *const size, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "center", (void*) center);
    error_check_null(LOG_METADATA, "size", (void*) size);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    dest[0] = center[0] - size[0] / 2;
    dest[1] = center[1] - size[1] / 2;
    dest[2] = size[0];
    dest[3] = size[1];

    trace_log_pop();
}

Rect math_rect_from_center_2(Vec2 const center, Vec2 const size) {
    trace_log_push(LOG_METADATA);

    Rect const result = { center.x - size.x / 2, center.y - size.y / 2, size.x, size.y };

    trace_log_pop();

    return result;
}

void math_rect_from_min_max_1(FSize const *const min, FSize const *const max, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "min", (void*) min);
    error_check_null(LOG_METADATA, "max", (void*) max);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    dest[0] = min[0];
    dest[1] = min[1];
    dest[2] = max[0] - min[0];
    dest[3] = max[1] - min[1];

    trace_log_pop();
}

Rect math_rect_from_min_max_2(Vec2 const min, Vec2 const max) {
    trace_log_push(LOG_METADATA);

    Rect const result = { min.x, min.y, max.x - min.x, max.y - min.y };

    trace_log_pop();

    return result;
}

void math_rect_from_points_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    dest[0] = math_min_f(a[0], b[0]);
    dest[1] = math_min_f(a[1], b[1]);
    dest[2] = math_abs_f(a[0] - b[0]);
    dest[3] = math_abs_f(a[1] - b[1]);

    trace_log_pop();
}

Rect math_rect_from_points_2(Vec2 const a, Vec2 const b) {
    trace_log_push(LOG_METADATA);

    Rect const result = {
        math_min_f(a.x, b.x),
        math_min_f(a.y, b.y),
        math_abs_f(a.x - b.x),
        math_abs_f(a.y - b.y)
    };

    trace_log_pop();

    return result;
}

void math_rect_grid_cell_1(FSize const *const r, IVec2 const dims, IVec2 const cell, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "r", (void*) r);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    /* A zero or negative dimension folds to one cell, so a degenerate grid is the whole
     * rect rather than a division by zero. */
    ISize const columns = dims.x <= 0 ? 1 : dims.x;
    ISize const lines = dims.y <= 0 ? 1 : dims.y;
    FSize const cell_w = r[2] / (FSize) columns;
    FSize const cell_h = r[3] / (FSize) lines;

    dest[0] = r[0] + cell_w * (FSize) cell.x;
    dest[1] = r[1] + cell_h * (FSize) cell.y;
    dest[2] = cell_w;
    dest[3] = cell_h;

    trace_log_pop();
}

Rect math_rect_grid_cell_2(Rect const r, IVec2 const dims, IVec2 const cell) {
    trace_log_push(LOG_METADATA);

    /* A zero or negative dimension folds to one cell, so a degenerate grid is the whole
     * rect rather than a division by zero. */
    ISize const columns = dims.x <= 0 ? 1 : dims.x;
    ISize const lines = dims.y <= 0 ? 1 : dims.y;
    FSize const cell_w = r.w / (FSize) columns;
    FSize const cell_h = r.h / (FSize) lines;

    Rect const result = {
        r.x + cell_w * (FSize) cell.x,
        r.y + cell_h * (FSize) cell.y,
        cell_w,
        cell_h
    };

    trace_log_pop();

    return result;
}

void math_rect_inflate_1(FSize const *const r, FSize const dx, FSize const dy, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "r", (void*) r);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    dest[0] = r[0] - dx;
    dest[1] = r[1] - dy;
    dest[2] = r[2] + dx * 2;
    dest[3] = r[3] + dy * 2;

    trace_log_pop();
}

Rect math_rect_inflate_2(Rect const r, FSize const dx, FSize const dy) {
    trace_log_push(LOG_METADATA);

    Rect const result = { r.x - dx, r.y - dy, r.w + dx * 2, r.h + dy * 2 };

    trace_log_pop();

    return result;
}

void math_rect_init_1(FSize const x, FSize const y, FSize const w, FSize const h, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    dest[0] = x;
    dest[1] = y;
    dest[2] = w;
    dest[3] = h;

    trace_log_pop();
}

Rect math_rect_init_2(FSize const x, FSize const y, FSize const w, FSize const h) {
    trace_log_push(LOG_METADATA);

    Rect const result = { x, y, w, h };

    trace_log_pop();

    return result;
}

void math_rect_inset_1(FSize const *const r, FSize const left, FSize const top, FSize const right, FSize const bottom, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "r", (void*) r);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    dest[0] = r[0] + left;
    dest[1] = r[1] + top;
    dest[2] = r[2] - left - right;
    dest[3] = r[3] - top - bottom;

    trace_log_pop();
}

Rect math_rect_inset_2(Rect const r, FSize const left, FSize const top, FSize const right, FSize const bottom) {
    trace_log_push(LOG_METADATA);

    Rect const result = { r.x + left, r.y + top, r.w - left - right, r.h - top - bottom };

    trace_log_pop();

    return result;
}

void math_rect_intersection_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    FSize const x1 = math_max_f(a[0], b[0]);
    FSize const y1 = math_max_f(a[1], b[1]);
    FSize const x2 = math_min_f(a[0] + a[2], b[0] + b[2]);
    FSize const y2 = math_min_f(a[1] + a[3], b[1] + b[3]);

    if (x2 <= x1 || y2 <= y1) {
        dest[0] = 0;
        dest[1] = 0;
        dest[2] = 0;
        dest[3] = 0;
    } else {
        dest[0] = x1;
        dest[1] = y1;
        dest[2] = x2 - x1;
        dest[3] = y2 - y1;
    }

    trace_log_pop();
}

Rect math_rect_intersection_2(Rect const a, Rect const b) {
    trace_log_push(LOG_METADATA);

    FSize const x1 = math_max_f(a.x, b.x);
    FSize const y1 = math_max_f(a.y, b.y);
    FSize const x2 = math_min_f(a.x + a.w, b.x + b.w);
    FSize const y2 = math_min_f(a.y + a.h, b.y + b.h);

    Rect result = DEFAULT_INITIALIZATION;

    if (x2 > x1 && y2 > y1) {
        result.x = x1;
        result.y = y1;
        result.w = x2 - x1;
        result.h = y2 - y1;
    }

    trace_log_pop();

    return result;
}

bool math_rect_intersects_1(FSize const *const a, FSize const *const b) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);

    bool const result = a[0] < b[0] + b[2]
                      && a[0] + a[2] > b[0]
                      && a[1] < b[1] + b[3]
                      && a[1] + a[3] > b[1];

    trace_log_pop();

    return result;
}

bool math_rect_intersects_2(Rect const a, Rect const b) {
    trace_log_push(LOG_METADATA);

    bool const result = a.x < b.x + b.w
                      && a.x + a.w > b.x
                      && a.y < b.y + b.h
                      && a.y + a.h > b.y;

    trace_log_pop();

    return result;
}

bool math_rect_is_empty_1(FSize const *const r) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "r", (void*) r);

    bool const result = r[2] <= 0 || r[3] <= 0;

    trace_log_pop();

    return result;
}

bool math_rect_is_empty_2(Rect const r) {
    trace_log_push(LOG_METADATA);

    bool const result = r.w <= 0 || r.h <= 0;

    trace_log_pop();

    return result;
}

FSize math_rect_left_1(FSize const *const r) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "r", (void*) r);

    FSize const result = r[0];

    trace_log_pop();

    return result;
}

FSize math_rect_left_2(Rect const r) {
    trace_log_push(LOG_METADATA);

    FSize const result = r.x;

    trace_log_pop();

    return result;
}

void math_rect_lerp_1(FSize const *const from, FSize const *const to, FSize const t, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "from", (void*) from);
    error_check_null(LOG_METADATA, "to", (void*) to);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    dest[0] = from[0] + (to[0] - from[0]) * t;
    dest[1] = from[1] + (to[1] - from[1]) * t;
    dest[2] = from[2] + (to[2] - from[2]) * t;
    dest[3] = from[3] + (to[3] - from[3]) * t;

    trace_log_pop();
}

Rect math_rect_lerp_2(Rect const from, Rect const to, FSize const t) {
    trace_log_push(LOG_METADATA);

    Rect const result = {
        from.x + (to.x - from.x) * t,
        from.y + (to.y - from.y) * t,
        from.w + (to.w - from.w) * t,
        from.h + (to.h - from.h) * t
    };

    trace_log_pop();

    return result;
}

void math_rect_make_1(FSize const *const src, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "src", (void*) src);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    dest[0] = src[0];
    dest[1] = src[1];
    dest[2] = src[2];
    dest[3] = src[3];

    trace_log_pop();
}

Rect math_rect_make_2(FSize const *const src) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "src", (void*) src);

    Rect const result = { src[0], src[1], src[2], src[3] };

    trace_log_pop();

    return result;
}

void math_rect_max_1(FSize const *const r, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "r", (void*) r);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    dest[0] = r[0] + r[2];
    dest[1] = r[1] + r[3];

    trace_log_pop();
}

Vec2 math_rect_max_2(Rect const r) {
    trace_log_push(LOG_METADATA);

    Vec2 const result = { r.x + r.w, r.y + r.h };

    trace_log_pop();

    return result;
}

void math_rect_min_1(FSize const *const r, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "r", (void*) r);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    dest[0] = r[0];
    dest[1] = r[1];

    trace_log_pop();
}

Vec2 math_rect_min_2(Rect const r) {
    trace_log_push(LOG_METADATA);

    Vec2 const result = { r.x, r.y };

    trace_log_pop();

    return result;
}

void math_rect_normalize_1(FSize const *const r, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "r", (void*) r);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    dest[0] = r[2] < 0 ? r[0] + r[2] : r[0];
    dest[1] = r[3] < 0 ? r[1] + r[3] : r[1];
    dest[2] = math_abs_f(r[2]);
    dest[3] = math_abs_f(r[3]);

    trace_log_pop();
}

Rect math_rect_normalize_2(Rect const r) {
    trace_log_push(LOG_METADATA);

    Rect const result = {
        r.w < 0 ? r.x + r.w : r.x,
        r.h < 0 ? r.y + r.h : r.y,
        math_abs_f(r.w),
        math_abs_f(r.h)
    };

    trace_log_pop();

    return result;
}

FSize math_rect_perimeter_1(FSize const *const r) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "r", (void*) r);

    FSize const result = (r[2] + r[3]) * 2;

    trace_log_pop();

    return result;
}

FSize math_rect_perimeter_2(Rect const r) {
    trace_log_push(LOG_METADATA);

    FSize const result = (r.w + r.h) * 2;

    trace_log_pop();

    return result;
}

FSize math_rect_right_1(FSize const *const r) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "r", (void*) r);

    FSize const result = r[0] + r[2];

    trace_log_pop();

    return result;
}

FSize math_rect_right_2(Rect const r) {
    trace_log_push(LOG_METADATA);

    FSize const result = r.x + r.w;

    trace_log_pop();

    return result;
}

void math_rect_round_1(FSize const *const r, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "r", (void*) r);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    dest[0] = math_round_f(r[0]);
    dest[1] = math_round_f(r[1]);
    dest[2] = math_round_f(r[2]);
    dest[3] = math_round_f(r[3]);

    trace_log_pop();
}

Rect math_rect_round_2(Rect const r) {
    trace_log_push(LOG_METADATA);

    Rect const result = {
        math_round_f(r.x),
        math_round_f(r.y),
        math_round_f(r.w),
        math_round_f(r.h)
    };

    trace_log_pop();

    return result;
}

void math_rect_scale_1(FSize const *const r, FSize const s, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "r", (void*) r);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    dest[0] = r[0];
    dest[1] = r[1];
    dest[2] = r[2] * s;
    dest[3] = r[3] * s;

    trace_log_pop();
}

Rect math_rect_scale_2(Rect const r, FSize const s) {
    trace_log_push(LOG_METADATA);

    Rect const result = { r.x, r.y, r.w * s, r.h * s };

    trace_log_pop();

    return result;
}

void math_rect_size_1(FSize const *const r, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "r", (void*) r);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    dest[0] = r[2];
    dest[1] = r[3];

    trace_log_pop();
}

Vec2 math_rect_size_2(Rect const r) {
    trace_log_push(LOG_METADATA);

    Vec2 const result = { r.w, r.h };

    trace_log_pop();

    return result;
}

void math_rect_to_array(Rect const r, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    dest[0] = r.x;
    dest[1] = r.y;
    dest[2] = r.w;
    dest[3] = r.h;

    trace_log_pop();
}

FSize math_rect_top_1(FSize const *const r) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "r", (void*) r);

    FSize const result = r[1];

    trace_log_pop();

    return result;
}

FSize math_rect_top_2(Rect const r) {
    trace_log_push(LOG_METADATA);

    FSize const result = r.y;

    trace_log_pop();

    return result;
}

void math_rect_top_left_1(FSize const *const r, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "r", (void*) r);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    dest[0] = r[0];
    dest[1] = r[1];

    trace_log_pop();
}

Vec2 math_rect_top_left_2(Rect const r) {
    trace_log_push(LOG_METADATA);

    Vec2 const result = { r.x, r.y };

    trace_log_pop();

    return result;
}

void math_rect_top_right_1(FSize const *const r, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "r", (void*) r);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    dest[0] = r[0] + r[2];
    dest[1] = r[1];

    trace_log_pop();
}

Vec2 math_rect_top_right_2(Rect const r) {
    trace_log_push(LOG_METADATA);

    Vec2 const result = { r.x + r.w, r.y };

    trace_log_pop();

    return result;
}

void math_rect_translate_1(FSize const *const r, FSize const *const offset, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "r", (void*) r);
    error_check_null(LOG_METADATA, "offset", (void*) offset);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    dest[0] = r[0] + offset[0];
    dest[1] = r[1] + offset[1];
    dest[2] = r[2];
    dest[3] = r[3];

    trace_log_pop();
}

Rect math_rect_translate_2(Rect const r, Vec2 const offset) {
    trace_log_push(LOG_METADATA);

    Rect const result = { r.x + offset.x, r.y + offset.y, r.w, r.h };

    trace_log_pop();

    return result;
}

void math_rect_union_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    FSize const x1 = math_min_f(a[0], b[0]);
    FSize const y1 = math_min_f(a[1], b[1]);
    FSize const x2 = math_max_f(a[0] + a[2], b[0] + b[2]);
    FSize const y2 = math_max_f(a[1] + a[3], b[1] + b[3]);

    dest[0] = x1;
    dest[1] = y1;
    dest[2] = x2 - x1;
    dest[3] = y2 - y1;

    trace_log_pop();
}

Rect math_rect_union_2(Rect const a, Rect const b) {
    trace_log_push(LOG_METADATA);

    FSize const x1 = math_min_f(a.x, b.x);
    FSize const y1 = math_min_f(a.y, b.y);
    FSize const x2 = math_max_f(a.x + a.w, b.x + b.w);
    FSize const y2 = math_max_f(a.y + a.h, b.y + b.h);

    Rect const result = { x1, y1, x2 - x1, y2 - y1 };

    trace_log_pop();

    return result;
}

void math_rect_union_point_1(FSize const *const r, FSize const *const point, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "r", (void*) r);
    error_check_null(LOG_METADATA, "point", (void*) point);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    FSize const x1 = math_min_f(r[0], point[0]);
    FSize const y1 = math_min_f(r[1], point[1]);
    FSize const x2 = math_max_f(r[0] + r[2], point[0]);
    FSize const y2 = math_max_f(r[1] + r[3], point[1]);

    dest[0] = x1;
    dest[1] = y1;
    dest[2] = x2 - x1;
    dest[3] = y2 - y1;

    trace_log_pop();
}

Rect math_rect_union_point_2(Rect const r, Vec2 const point) {
    trace_log_push(LOG_METADATA);

    FSize const x1 = math_min_f(r.x, point.x);
    FSize const y1 = math_min_f(r.y, point.y);
    FSize const x2 = math_max_f(r.x + r.w, point.x);
    FSize const y2 = math_max_f(r.y + r.h, point.y);

    Rect const result = { x1, y1, x2 - x1, y2 - y1 };

    trace_log_pop();

    return result;
}