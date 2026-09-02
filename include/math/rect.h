/*
 * rect.h - 2D rectangle operations for the CFW math module
 *
 * Features:
 *   - A position+size rectangle (x, y, w, h), the x/y/w/h cousin of Aabb2d's min/max.
 *     NOT castable to SDL_FRect: Rect holds F64, SDL_FRect float (see types.h)
 *   - Construction (from scalars, min/max, center/size, two points, raw array),
 *     accessors (edges, corners, center, size, area, perimeter), predicates
 *     (containment, intersection, equality, emptiness), boolean geometry
 *     (intersection, union), transforms (translate/scale/inflate/inset/normalize/
 *     round/lerp), and layout helpers (clamp/constrain/center/fit/grid)
 *   - Root-first variants: _1 raw FSize-array underset, _2 framework-struct superset
 *   - Pure-value semantics: no wrapper mutates its argument in place; the raw (_1)
 *     variant writes a caller-supplied destination, the struct (_2) variant returns
 *     a Rect (or a Vec2/FSize/bool for vector/scalar/boolean results)
 *
 * Usage Examples:
 *   @code
 *   Rect const panel = math_rect_init_2(0.0, 0.0, 320.0, 240.0);
 *   Rect const inner = math_rect_inset_2(panel, 8.0, 8.0, 8.0, 8.0);
 *   Vec2 const c     = math_rect_center_2(inner);
 *   bool const hit   = math_rect_contains_point_2(panel, c);
 *   @endcode
 *
 * Error Handling:
 *   - Pointer (_1) variants validate every pointer with error_check_null.
 *   - Struct (_2) variants take values, so there is no pointer to validate.
 *   - Grid helpers treat a zero or negative column/row count as one cell (no divide by
 *     zero); a cell index outside dims is NOT clamped - the returned rect lies outside r.
 *   - Aspect fitting REFUSES a degenerate source: a zero, negative or NaN inner width or
 *     height returns the empty rect (0, 0, 0, 0) in every build - it is data, not a bug.
 *
 * Thread Safety:
 *   - All functions are pure and thread-safe; no shared state is touched.
 *
 * Memory Management:
 *   - No allocation is performed; every function operates on values only.
 *
 * Performance Characteristics:
 *   - Direct FSize arithmetic; no cglm round-trip and no float<->FSize boundary copy.
 *
 * Dependencies:
 *   - <math/types.h> for the Rect/Vec2 types and the error/tracing macros.
 *   - <math/scalar.h> for the FSize min/max/abs/round/clamp helpers reused here.
 *
 * See rect.c for implementation details.
 */

#ifndef MATH_RECT_H
#define MATH_RECT_H

#include <math/types.h>
#include <math/scalar.h>

/*==============================================================================
 * MARK: - Rect API
 *
 * A raw (_1) rectangle is 4 contiguous FSize: x, y, w, h. A raw point/size is 2
 * contiguous FSize. The struct (_2) variants read and return a Rect value; ops
 * that yield a point/size return a Vec2, metric ops return an FSize, and predicate
 * ops return a bool.
 *============================================================================*/

/**
 * @brief Area of a raw 2D rectangle (w * h).
 * @param r Raw source rectangle (4 contiguous FSize: x, y, w, h).
 * @return Area as FSize.
 */
FSize math_rect_area_1(FSize const *const r);

/**
 * @brief Area of a 2D rectangle (w * h).
 * @param r Source rectangle.
 * @return Area as FSize.
 */
FSize math_rect_area_2(Rect const r);

/**
 * @brief Bottom edge (y + h) of a raw 2D rectangle.
 * @param r Raw source rectangle (4 contiguous FSize: x, y, w, h).
 * @return Bottom edge as FSize.
 */
FSize math_rect_bottom_1(FSize const *const r);

/**
 * @brief Bottom edge (y + h) of a 2D rectangle.
 * @param r Source rectangle.
 * @return Bottom edge as FSize.
 */
FSize math_rect_bottom_2(Rect const r);

/**
 * @brief Bottom-left corner (x, y + h) of a raw 2D rectangle.
 * @param r Raw source rectangle (4 contiguous FSize: x, y, w, h).
 * @param dest Destination of 2 contiguous FSize.
 */
void math_rect_bottom_left_1(FSize const *const r, FSize *const dest);

/**
 * @brief Return the bottom-left corner (x, y + h) of a 2D rectangle.
 * @param r Source rectangle.
 * @return Bottom-left corner Vec2.
 */
Vec2 math_rect_bottom_left_2(Rect const r);

/**
 * @brief Bottom-right corner (x + w, y + h) of a raw 2D rectangle.
 * @param r Raw source rectangle (4 contiguous FSize: x, y, w, h).
 * @param dest Destination of 2 contiguous FSize.
 */
void math_rect_bottom_right_1(FSize const *const r, FSize *const dest);

/**
 * @brief Return the bottom-right corner (x + w, y + h) of a 2D rectangle.
 * @param r Source rectangle.
 * @return Bottom-right corner Vec2.
 */
Vec2 math_rect_bottom_right_2(Rect const r);

/**
 * @brief Center point of a raw 2D rectangle.
 * @param r Raw source rectangle (4 contiguous FSize: x, y, w, h).
 * @param dest Destination of 2 contiguous FSize.
 */
void math_rect_center_1(FSize const *const r, FSize *const dest);

/**
 * @brief Return the center point of a 2D rectangle.
 * @param r Source rectangle.
 * @return Center Vec2.
 */
Vec2 math_rect_center_2(Rect const r);

/**
 * @brief Reposition a raw inner rectangle centered inside a raw outer rectangle.
 * @param inner Raw inner rectangle (4 contiguous FSize: x, y, w, h).
 * @param outer Raw outer rectangle (4 contiguous FSize: x, y, w, h).
 * @param dest Destination of 4 contiguous FSize (x, y, w, h); size is preserved.
 */
void math_rect_center_in_1(FSize const *const inner, FSize const *const outer, FSize *const dest);

/**
 * @brief Return an inner rectangle centered inside an outer rectangle (size kept).
 * @param inner Inner rectangle.
 * @param outer Outer rectangle.
 * @return Centered Rect.
 */
Rect math_rect_center_in_2(Rect const inner, Rect const outer);

/**
 * @brief Clamp a raw point into a raw 2D rectangle.
 * @param r Raw source rectangle (4 contiguous FSize: x, y, w, h).
 * @param point Raw point (2 contiguous FSize).
 * @param dest Destination of 2 contiguous FSize.
 */
void math_rect_clamp_point_1(FSize const *const r, FSize const *const point, FSize *const dest);

/**
 * @brief Return a point clamped into a 2D rectangle.
 * @param r Source rectangle.
 * @param point Point to clamp.
 * @return Clamped Vec2.
 */
Vec2 math_rect_clamp_point_2(Rect const r, Vec2 const point);

/**
 * @brief Slide a raw inner rectangle to lie inside a raw bounds rectangle.
 * @param inner Raw inner rectangle (4 contiguous FSize: x, y, w, h).
 * @param bounds Raw bounds rectangle (4 contiguous FSize: x, y, w, h).
 * @param dest Destination of 4 contiguous FSize (x, y, w, h); size is preserved.
 */
void math_rect_constrain_1(FSize const *const inner, FSize const *const bounds, FSize *const dest);

/**
 * @brief Return an inner rectangle slid to lie inside a bounds rectangle (size kept).
 * @param inner Inner rectangle.
 * @param bounds Bounds rectangle.
 * @return Constrained Rect.
 */
Rect math_rect_constrain_2(Rect const inner, Rect const bounds);

/**
 * @brief Test whether a raw 2D rectangle contains a raw point (edges inclusive).
 * @param r Raw source rectangle (4 contiguous FSize: x, y, w, h).
 * @param point Raw point (2 contiguous FSize).
 * @return true when the point lies inside or on the rectangle.
 */
bool math_rect_contains_point_1(FSize const *const r, FSize const *const point);

/**
 * @brief Test whether a 2D rectangle contains a point (edges inclusive).
 * @param r Source rectangle.
 * @param point Point to test.
 * @return true when the point lies inside or on the rectangle.
 */
bool math_rect_contains_point_2(Rect const r, Vec2 const point);

/**
 * @brief Test whether a raw 2D rectangle fully contains another raw rectangle.
 * @param r Raw outer rectangle (4 contiguous FSize: x, y, w, h).
 * @param other Raw inner rectangle (4 contiguous FSize: x, y, w, h).
 * @return true when other lies entirely within r.
 */
bool math_rect_contains_rect_1(FSize const *const r, FSize const *const other);

/**
 * @brief Test whether a 2D rectangle fully contains another rectangle.
 * @param r Outer rectangle.
 * @param other Inner rectangle.
 * @return true when other lies entirely within r.
 */
bool math_rect_contains_rect_2(Rect const r, Rect const other);

/**
 * @brief Test two raw 2D rectangles for exact equality.
 * @param a Raw first rectangle (4 contiguous FSize: x, y, w, h).
 * @param b Raw second rectangle (4 contiguous FSize: x, y, w, h).
 * @return true when all four components are equal.
 */
bool math_rect_equal_1(FSize const *const a, FSize const *const b);

/**
 * @brief Test two 2D rectangles for exact equality.
 * @param a First rectangle.
 * @param b Second rectangle.
 * @return true when all four components are equal.
 */
bool math_rect_equal_2(Rect const a, Rect const b);

/**
 * @brief Scale a raw inner rectangle to fit a raw outer rectangle, aspect preserved.
 * @param inner Raw inner rectangle (4 contiguous FSize: x, y, w, h); w and h > 0.
 * @param outer Raw outer rectangle (4 contiguous FSize: x, y, w, h).
 * @param dest Destination of 4 contiguous FSize (x, y, w, h); centered in outer.
 * @note A zero, negative or NaN inner width or height returns the empty rect (0, 0, 0, 0).
 *       Detect it by size, not by isfinite(): the refusal is finite.
 */
void math_rect_fit_aspect_1(FSize const *const inner, FSize const *const outer, FSize *const dest);

/**
 * @brief Return an inner rectangle scaled to fit an outer rectangle, aspect kept.
 * @param inner Inner rectangle (supplies the aspect ratio); width and height > 0.
 * @param outer Outer rectangle to fit within.
 * @return Fitted Rect, centered inside outer.
 * @note A zero, negative or NaN inner width or height returns the empty rect (0, 0, 0, 0).
 *       Detect it by size, not by isfinite(): the refusal is finite.
 */
Rect math_rect_fit_aspect_2(Rect const inner, Rect const outer);

/**
 * @brief Build a raw rectangle from a raw min/max box.
 * @param aabb Raw source box (4 contiguous FSize: min x, min y, max x, max y).
 * @param dest Destination of 4 contiguous FSize (x, y, w, h); may overlap aabb.
 */
void math_rect_from_aabb2d_1(FSize const *const aabb, FSize *const dest);

/**
 * @brief Build a rectangle from a min/max box: the same area in the other 2D form.
 * @param aabb Source box.
 * @return Rect with origin at the box's min and size max - min.
 */
Rect math_rect_from_aabb2d_2(Aabb2d const aabb);

/**
 * @brief Build a raw 2D rectangle from a raw center point and a raw size.
 * @param center Raw center point (2 contiguous FSize).
 * @param size Raw size (2 contiguous FSize: w, h).
 * @param dest Destination of 4 contiguous FSize (x, y, w, h).
 */
void math_rect_from_center_1(FSize const *const center, FSize const *const size, FSize *const dest);

/**
 * @brief Return a 2D rectangle built from a center point and a size.
 * @param center Center point.
 * @param size Size as (w, h).
 * @return Constructed Rect.
 */
Rect math_rect_from_center_2(Vec2 const center, Vec2 const size);

/**
 * @brief Build a raw 2D rectangle spanning a raw min corner to a raw max corner.
 * @param min Raw minimum corner (2 contiguous FSize).
 * @param max Raw maximum corner (2 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize (x, y, w, h).
 */
void math_rect_from_min_max_1(FSize const *const min, FSize const *const max, FSize *const dest);

/**
 * @brief Return a 2D rectangle spanning a min corner to a max corner.
 * @param min Minimum corner.
 * @param max Maximum corner.
 * @return Constructed Rect.
 */
Rect math_rect_from_min_max_2(Vec2 const min, Vec2 const max);

/**
 * @brief Build a raw 2D rectangle bounding two raw points (always non-negative size).
 * @param a Raw first point (2 contiguous FSize).
 * @param b Raw second point (2 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize (x, y, w, h).
 */
void math_rect_from_points_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return a 2D rectangle bounding two points (always non-negative size).
 * @param a First point.
 * @param b Second point.
 * @return Constructed Rect.
 */
Rect math_rect_from_points_2(Vec2 const a, Vec2 const b);

/**
 * @brief Cell of a uniform grid over a raw 2D rectangle at (col, row).
 * @param r Raw source rectangle (4 contiguous FSize: x, y, w, h).
 * @param dims Grid dimensions as (columns, rows); a zero or negative count is treated as one.
 * @param cell Zero-based cell position as (column, row); not clamped to dims.
 * @param dest Destination of 4 contiguous FSize (x, y, w, h).
 */
void math_rect_grid_cell_1(FSize const *const r, IVec2 const dims, IVec2 const cell, FSize *const dest);

/**
 * @brief Return the (col, row) cell of a uniform grid over a 2D rectangle.
 * @param r Source rectangle.
 * @param dims Grid dimensions as (columns, rows); a zero or negative count is treated as one.
 * @param cell Zero-based cell position as (column, row); not clamped to dims.
 * @return Cell Rect.
 */
Rect math_rect_grid_cell_2(Rect const r, IVec2 const dims, IVec2 const cell);

/**
 * @brief Grow (or shrink) a raw 2D rectangle by a per-axis amount on every side.
 * @param r Raw source rectangle (4 contiguous FSize: x, y, w, h).
 * @param dx Horizontal amount added to each side (negative shrinks).
 * @param dy Vertical amount added to each side (negative shrinks).
 * @param dest Destination of 4 contiguous FSize (x, y, w, h).
 */
void math_rect_inflate_1(FSize const *const r, FSize const dx, FSize const dy, FSize *const dest);

/**
 * @brief Return a 2D rectangle grown (or shrunk) by a per-axis amount on every side.
 * @param r Source rectangle.
 * @param dx Horizontal amount added to each side (negative shrinks).
 * @param dy Vertical amount added to each side (negative shrinks).
 * @return Inflated Rect.
 */
Rect math_rect_inflate_2(Rect const r, FSize const dx, FSize const dy);

/**
 * @brief Construct a raw 2D rectangle from scalar components.
 * @param x Origin x.
 * @param y Origin y.
 * @param w Width.
 * @param h Height.
 * @param dest Destination of 4 contiguous FSize (x, y, w, h).
 */
void math_rect_init_1(FSize const x, FSize const y, FSize const w, FSize const h, FSize *const dest);

/**
 * @brief Return a 2D rectangle built from scalar components.
 * @param x Origin x.
 * @param y Origin y.
 * @param w Width.
 * @param h Height.
 * @return Constructed Rect.
 */
Rect math_rect_init_2(FSize const x, FSize const y, FSize const w, FSize const h);

/**
 * @brief Shrink a raw 2D rectangle by a per-edge inset.
 * @param r Raw source rectangle (4 contiguous FSize: x, y, w, h).
 * @param left Amount removed from the left edge.
 * @param top Amount removed from the top edge.
 * @param right Amount removed from the right edge.
 * @param bottom Amount removed from the bottom edge.
 * @param dest Destination of 4 contiguous FSize (x, y, w, h).
 */
void math_rect_inset_1(FSize const *const r, FSize const left, FSize const top, FSize const right, FSize const bottom, FSize *const dest);

/**
 * @brief Return a 2D rectangle shrunk by a per-edge inset.
 * @param r Source rectangle.
 * @param left Amount removed from the left edge.
 * @param top Amount removed from the top edge.
 * @param right Amount removed from the right edge.
 * @param bottom Amount removed from the bottom edge.
 * @return Inset Rect.
 */
Rect math_rect_inset_2(Rect const r, FSize const left, FSize const top, FSize const right, FSize const bottom);

/**
 * @brief Overlap of two raw 2D rectangles.
 * @param a Raw first rectangle (4 contiguous FSize: x, y, w, h).
 * @param b Raw second rectangle (4 contiguous FSize: x, y, w, h).
 * @param dest Destination of 4 contiguous FSize (x, y, w, h); zeroed when disjoint.
 */
void math_rect_intersection_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the overlap of two 2D rectangles.
 * @param a First rectangle.
 * @param b Second rectangle.
 * @return Overlap Rect, or a zero rectangle when the two are disjoint.
 */
Rect math_rect_intersection_2(Rect const a, Rect const b);

/**
 * @brief Test whether two raw 2D rectangles overlap.
 * @param a Raw first rectangle (4 contiguous FSize: x, y, w, h).
 * @param b Raw second rectangle (4 contiguous FSize: x, y, w, h).
 * @return true when the two rectangles overlap.
 */
bool math_rect_intersects_1(FSize const *const a, FSize const *const b);

/**
 * @brief Test whether two 2D rectangles overlap.
 * @param a First rectangle.
 * @param b Second rectangle.
 * @return true when the two rectangles overlap.
 */
bool math_rect_intersects_2(Rect const a, Rect const b);

/**
 * @brief Test whether a raw 2D rectangle is empty (w <= 0 or h <= 0).
 * @param r Raw source rectangle (4 contiguous FSize: x, y, w, h).
 * @return true when the rectangle has no positive area.
 */
bool math_rect_is_empty_1(FSize const *const r);

/**
 * @brief Test whether a 2D rectangle is empty (w <= 0 or h <= 0).
 * @param r Source rectangle.
 * @return true when the rectangle has no positive area.
 */
bool math_rect_is_empty_2(Rect const r);

/**
 * @brief Left edge (x) of a raw 2D rectangle.
 * @param r Raw source rectangle (4 contiguous FSize: x, y, w, h).
 * @return Left edge as FSize.
 */
FSize math_rect_left_1(FSize const *const r);

/**
 * @brief Left edge (x) of a 2D rectangle.
 * @param r Source rectangle.
 * @return Left edge as FSize.
 */
FSize math_rect_left_2(Rect const r);

/**
 * @brief Linearly interpolate between two raw 2D rectangles.
 * @param from Raw start rectangle (4 contiguous FSize: x, y, w, h).
 * @param to Raw end rectangle (4 contiguous FSize: x, y, w, h).
 * @param t Interpolation factor in [0, 1].
 * @param dest Destination of 4 contiguous FSize (x, y, w, h).
 */
void math_rect_lerp_1(FSize const *const from, FSize const *const to, FSize const t, FSize *const dest);

/**
 * @brief Return the linear interpolation between two 2D rectangles.
 * @param from Start rectangle.
 * @param to End rectangle.
 * @param t Interpolation factor in [0, 1].
 * @return Interpolated Rect.
 */
Rect math_rect_lerp_2(Rect const from, Rect const to, FSize const t);

/**
 * @brief Construct a raw 2D rectangle by copying a raw FSize source array.
 * @param src Raw source array (4 contiguous FSize: x, y, w, h).
 * @param dest Destination of 4 contiguous FSize (x, y, w, h).
 */
void math_rect_make_1(FSize const *const src, FSize *const dest);

/**
 * @brief Construct a 2D rectangle from a raw FSize source array.
 * @param src Raw source array (4 contiguous FSize: x, y, w, h).
 * @return Constructed Rect.
 */
Rect math_rect_make_2(FSize const *const src);

/**
 * @brief Maximum corner (x + w, y + h) of a raw 2D rectangle.
 * @param r Raw source rectangle (4 contiguous FSize: x, y, w, h).
 * @param dest Destination of 2 contiguous FSize.
 */
void math_rect_max_1(FSize const *const r, FSize *const dest);

/**
 * @brief Return the maximum corner (x + w, y + h) of a 2D rectangle.
 * @param r Source rectangle.
 * @return Maximum-corner Vec2.
 */
Vec2 math_rect_max_2(Rect const r);

/**
 * @brief Minimum corner (x, y) of a raw 2D rectangle.
 * @param r Raw source rectangle (4 contiguous FSize: x, y, w, h).
 * @param dest Destination of 2 contiguous FSize.
 */
void math_rect_min_1(FSize const *const r, FSize *const dest);

/**
 * @brief Return the minimum corner (x, y) of a 2D rectangle.
 * @param r Source rectangle.
 * @return Minimum-corner Vec2.
 */
Vec2 math_rect_min_2(Rect const r);

/**
 * @brief Normalize a raw 2D rectangle so its width and height are non-negative.
 * @param r Raw source rectangle (4 contiguous FSize: x, y, w, h).
 * @param dest Destination of 4 contiguous FSize (x, y, w, h).
 */
void math_rect_normalize_1(FSize const *const r, FSize *const dest);

/**
 * @brief Return a 2D rectangle normalized to a non-negative width and height.
 * @param r Source rectangle.
 * @return Normalized Rect.
 */
Rect math_rect_normalize_2(Rect const r);

/**
 * @brief Perimeter of a raw 2D rectangle (2 * (w + h)).
 * @param r Raw source rectangle (4 contiguous FSize: x, y, w, h).
 * @return Perimeter as FSize.
 */
FSize math_rect_perimeter_1(FSize const *const r);

/**
 * @brief Perimeter of a 2D rectangle (2 * (w + h)).
 * @param r Source rectangle.
 * @return Perimeter as FSize.
 */
FSize math_rect_perimeter_2(Rect const r);

/**
 * @brief Right edge (x + w) of a raw 2D rectangle.
 * @param r Raw source rectangle (4 contiguous FSize: x, y, w, h).
 * @return Right edge as FSize.
 */
FSize math_rect_right_1(FSize const *const r);

/**
 * @brief Right edge (x + w) of a 2D rectangle.
 * @param r Source rectangle.
 * @return Right edge as FSize.
 */
FSize math_rect_right_2(Rect const r);

/**
 * @brief Round every component of a raw 2D rectangle to the nearest integer.
 * @param r Raw source rectangle (4 contiguous FSize: x, y, w, h).
 * @param dest Destination of 4 contiguous FSize (x, y, w, h).
 */
void math_rect_round_1(FSize const *const r, FSize *const dest);

/**
 * @brief Return a 2D rectangle with every component rounded to the nearest integer.
 * @param r Source rectangle.
 * @return Rounded Rect.
 */
Rect math_rect_round_2(Rect const r);

/**
 * @brief Scale the size of a raw 2D rectangle by a scalar (origin unchanged).
 * @param r Raw source rectangle (4 contiguous FSize: x, y, w, h).
 * @param s Scalar factor applied to width and height.
 * @param dest Destination of 4 contiguous FSize (x, y, w, h).
 */
void math_rect_scale_1(FSize const *const r, FSize const s, FSize *const dest);

/**
 * @brief Return a 2D rectangle with its size scaled by a scalar (origin unchanged).
 * @param r Source rectangle.
 * @param s Scalar factor applied to width and height.
 * @return Scaled Rect.
 */
Rect math_rect_scale_2(Rect const r, FSize const s);

/**
 * @brief Size (w, h) of a raw 2D rectangle.
 * @param r Raw source rectangle (4 contiguous FSize: x, y, w, h).
 * @param dest Destination of 2 contiguous FSize.
 */
void math_rect_size_1(FSize const *const r, FSize *const dest);

/**
 * @brief Return the size (w, h) of a 2D rectangle.
 * @param r Source rectangle.
 * @return Size Vec2.
 */
Vec2 math_rect_size_2(Rect const r);

/**
 * @brief Copy a raw 2D rectangle into a raw destination array.
 * @param r Source rectangle.
 * @param dest Destination of 4 contiguous FSize (x, y, w, h).
 */
void math_rect_to_array(Rect const r, FSize *const dest);

/**
 * @brief Top edge (y) of a raw 2D rectangle.
 * @param r Raw source rectangle (4 contiguous FSize: x, y, w, h).
 * @return Top edge as FSize.
 */
FSize math_rect_top_1(FSize const *const r);

/**
 * @brief Top edge (y) of a 2D rectangle.
 * @param r Source rectangle.
 * @return Top edge as FSize.
 */
FSize math_rect_top_2(Rect const r);

/**
 * @brief Top-left corner (x, y) of a raw 2D rectangle.
 * @param r Raw source rectangle (4 contiguous FSize: x, y, w, h).
 * @param dest Destination of 2 contiguous FSize.
 */
void math_rect_top_left_1(FSize const *const r, FSize *const dest);

/**
 * @brief Return the top-left corner (x, y) of a 2D rectangle.
 * @param r Source rectangle.
 * @return Top-left corner Vec2.
 */
Vec2 math_rect_top_left_2(Rect const r);

/**
 * @brief Top-right corner (x + w, y) of a raw 2D rectangle.
 * @param r Raw source rectangle (4 contiguous FSize: x, y, w, h).
 * @param dest Destination of 2 contiguous FSize.
 */
void math_rect_top_right_1(FSize const *const r, FSize *const dest);

/**
 * @brief Return the top-right corner (x + w, y) of a 2D rectangle.
 * @param r Source rectangle.
 * @return Top-right corner Vec2.
 */
Vec2 math_rect_top_right_2(Rect const r);

/**
 * @brief Translate a raw 2D rectangle by a raw offset.
 * @param r Raw source rectangle (4 contiguous FSize: x, y, w, h).
 * @param offset Raw offset (2 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize (x, y, w, h).
 */
void math_rect_translate_1(FSize const *const r, FSize const *const offset, FSize *const dest);

/**
 * @brief Return a 2D rectangle translated by an offset.
 * @param r Source rectangle.
 * @param offset Translation offset.
 * @return Translated Rect.
 */
Rect math_rect_translate_2(Rect const r, Vec2 const offset);

/**
 * @brief Smallest raw 2D rectangle enclosing two raw rectangles.
 * @param a Raw first rectangle (4 contiguous FSize: x, y, w, h).
 * @param b Raw second rectangle (4 contiguous FSize: x, y, w, h).
 * @param dest Destination of 4 contiguous FSize (x, y, w, h).
 */
void math_rect_union_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the smallest 2D rectangle enclosing two rectangles.
 * @param a First rectangle.
 * @param b Second rectangle.
 * @return Enclosing Rect.
 */
Rect math_rect_union_2(Rect const a, Rect const b);

/**
 * @brief Smallest raw 2D rectangle enclosing a raw rectangle and a raw point.
 * @param r Raw source rectangle (4 contiguous FSize: x, y, w, h).
 * @param point Raw point (2 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize (x, y, w, h).
 */
void math_rect_union_point_1(FSize const *const r, FSize const *const point, FSize *const dest);

/**
 * @brief Return the smallest 2D rectangle enclosing a rectangle and a point.
 * @param r Source rectangle.
 * @param point Point to enclose.
 * @return Enclosing Rect.
 */
Rect math_rect_union_point_2(Rect const r, Vec2 const point);

#endif // MATH_RECT_H