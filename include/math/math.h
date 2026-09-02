/*
 * math.h - Umbrella header for the CFW math module
 *
 * Features:
 *   - Aggregates every math sub-module behind a single include (cglm_compat.h is
 *     internal to the wrappers and is not part of the API)
 *   - Every sub-module under include/math/ - the include list below is the inventory
 *   - Shared FSize/ISize types, constants, and raw<->cglm bridges (via types.h)
 *
 * Usage Examples:
 *   @code
 *   #include <math/math.h>
 *
 *   Vec3 const a   = { 1.0, 2.0, 3.0 };
 *   Vec3 const b   = { 4.0, 5.0, 6.0 };
 *   Vec3 const sum = math_vec3_add_2(a, b);
 *   @endcode
 *
 * Error Handling:
 *   - Delegated to each sub-module; pointer variants validate with error_check_null.
 *
 * Thread Safety:
 *   - All aggregated functions are pure and thread-safe; no shared state is touched.
 *
 * Memory Management:
 *   - No allocation is performed; every function operates on values only.
 *
 * Performance Characteristics:
 *   - See each sub-module; vector/matrix/quaternion wrappers call cglm's compiled
 *     glmc_* routines, which are scalar C except the vec4/mat4 paths. Every value
 *     crosses F64 -> float -> F64 at the boundary - see the Precision section in types.h.
 *
 * Dependencies:
 *   - <math/types.h> plus every math sub-module header.
 *
 * This header declares nothing of its own; it only re-exports the sub-modules.
 */

#ifndef MATH_H
#define MATH_H

#include <math/types.h>
#include <math/scalar.h>
#include <math/vec2.h>
#include <math/vec3.h>
#include <math/vec4.h>
#include <math/ivec2.h>
#include <math/ivec3.h>
#include <math/ivec4.h>
#include <math/mat2.h>
#include <math/mat3.h>
#include <math/mat4.h>
#include <math/mat2x3.h>
#include <math/mat2x4.h>
#include <math/mat3x2.h>
#include <math/mat3x4.h>
#include <math/mat4x2.h>
#include <math/mat4x3.h>
#include <math/quat.h>
#include <math/euler.h>
#include <math/affine.h>
#include <math/affine2d.h>
#include <math/cam.h>
#include <math/clipspace.h>
#include <math/aabb2d.h>
#include <math/rect.h>
#include <math/box.h>
#include <math/sphere.h>
#include <math/plane.h>
#include <math/frustum.h>
#include <math/ray.h>
#include <math/ease.h>
#include <math/project.h>
#include <math/curve.h>
#include <math/bezier.h>
#include <math/noise.h>
#include <math/io.h>
#include <math/timestep.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

// Semantic Versioning 2.0.0 for the math module as a whole; 0.y.z while the API is still settling.
#define MATH_VERSION "0.3.1"

#endif // MATH_H