// -------------------------------------------------------------
//  Cubzh Core
//  quaternion.c
//  Created by Arthur Cormerais on january 27, 2021.
// -------------------------------------------------------------

#include "quaternion.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

#include "config.h"
#include "utils.h"

/// Internal epsilon for quaternion normalization, best leave it as low as possible to remove
/// imprecision every chance we get, however it could be slightly increased eg. 1e-8f or 1e-7f
/// within floating point imprecision, to reduce the number of normalize calls
#define QUATERNION_NORMALIZE_EPSILON 0.0f

Quaternion *quaternion_new(const float x,
                           const float y,
                           const float z,
                           const float w,
                           const bool normalized) {
    Quaternion *q = (Quaternion *)malloc(sizeof(Quaternion));
    q->x = x;
    q->y = y;
    q->z = z;
    q->w = w;
    q->normalized = normalized;
    return q;
}

Quaternion *quaternion_new_identity(void) {
    return quaternion_new(0.0f, 0.0f, 0.0f, 1.0f, true);
}

void quaternion_free(Quaternion *q) {
    free(q);
}

void quaternion_set(Quaternion *q1, const Quaternion *q2) {
    q1->x = q2->x;
    q1->y = q2->y;
    q1->z = q2->z;
    q1->w = q2->w;
    q1->normalized = q2->normalized;
}

void quaternion_set_identity(Quaternion *q) {
    q->x = 0.0f;
    q->y = 0.0f;
    q->z = 0.0f;
    q->w = 1.0f;
    q->normalized = true;
}

float quaternion_magnitude(Quaternion *q) {
    return sqrtf(q->x * q->x + q->y * q->y + q->z * q->z + q->w * q->w);
}

float quaternion_square_magnitude(Quaternion *q) {
    return q->x * q->x + q->y * q->y + q->z * q->z + q->w * q->w;
}

float quaternion_angle(Quaternion *q) {
    quaternion_op_normalize(q);
    return 2.0f * acosf(q->w);
    // The following may be more robust but more expensive:
    // return 2.0f * atan2f(sqrtf(q->x * q->x + q->y * q->y + q->z * q->z), q->w);
}

bool quaternion_is_zero(Quaternion *q, float epsilon) {
    quaternion_op_normalize(q);
    return float_isEqual(q->w, 1.0f, epsilon);
}

bool quaternion_is_normalized(Quaternion *q, float epsilon) {
    return float_isEqual(quaternion_square_magnitude(q), 1.0f, epsilon);
}

bool quaternion_is_equal(Quaternion *q1, Quaternion *q2, float epsilon) {
    const float angle = quaternion_angle_between(q1, q2);
    return float_isZero(angle, epsilon) || float_isEqual(angle, PI2_F, epsilon);
}

float quaternion_angle_between(Quaternion *q1, Quaternion *q2) {
    quaternion_op_normalize(q1);
    quaternion_op_normalize(q2);
    return 2.0f * acosf(CLAMP(quaternion_op_dot(q1, q2), -1.0f, 1.0f));
    // The following is equivalent but more expensive:
    /*Quaternion q = quaternion_op_mult(quaternion_op_conjugate(q1), q2);
    return quaternion_angle(&q);*/
}

// MARK: - Operations -

Quaternion *quaternion_op_scale(Quaternion *q, float f) {
    q->x *= f;
    q->y *= f;
    q->z *= f;
    q->w *= f;
    return q;
}

Quaternion *quaternion_op_unscale(Quaternion *q, float f) {
    q->x /= f;
    q->y /= f;
    q->z /= f;
    q->w /= f;
    return q;
}

Quaternion *quaternion_op_conjugate(Quaternion *q) {
    q->x = -q->x;
    q->y = -q->y;
    q->z = -q->z;
    return q;
}

/// Most operations work on normalized quaternions, we need to make this as cheap as possible
Quaternion *quaternion_op_normalize(Quaternion *q) {
    if (q->normalized) {
        return q;
    } else {
        q->normalized = true;
        const float sqm = quaternion_square_magnitude(q);
        if (float_isEqual(sqm, 1.0f, QUATERNION_NORMALIZE_EPSILON)) {
            return q;
        } else {
            return quaternion_op_unscale(q, sqrtf(sqm));
        }
    }
}

Quaternion *quaternion_op_inverse(Quaternion *q) {
    return quaternion_op_conjugate(quaternion_op_normalize(q));
}

Quaternion quaternion_op_mult(const Quaternion *q1, const Quaternion *q2) {
    Quaternion q;
    q.x = q1->w * q2->x + q1->x * q2->w + q1->y * q2->z - q1->z * q2->y;
    q.y = q1->w * q2->y + q1->y * q2->w + q1->z * q2->x - q1->x * q2->z;
    q.z = q1->w * q2->z + q1->z * q2->w + q1->x * q2->y - q1->y * q2->x;
    q.w = q1->w * q2->w - q1->x * q2->x - q1->y * q2->y - q1->z * q2->z;
    q.normalized = false;
    return q;
}

Quaternion *quaternion_op_mult_left(Quaternion *q1, const Quaternion *q2) {
    Quaternion q = quaternion_op_mult(q1, q2);
    quaternion_set(q1, &q);
    return q1;
}

Quaternion *quaternion_op_mult_right(const Quaternion *q1, Quaternion *q2) {
    Quaternion q = quaternion_op_mult(q1, q2);
    quaternion_set(q2, &q);
    return q2;
}

Quaternion *quaternion_op_lerp(const Quaternion *from,
                               const Quaternion *to,
                               Quaternion *lerped,
                               const float t) {
    const float v = CLAMP01(t);
    lerped->x = LERP(from->x, to->x, v);
    lerped->y = LERP(from->y, to->y, v);
    lerped->z = LERP(from->z, to->z, v);
    lerped->w = LERP(from->w, to->w, v);
    lerped->normalized = false;
    return lerped;
}

float quaternion_op_dot(const Quaternion *q1, const Quaternion *q2) {
    return q1->w * q2->w + q1->x * q2->x + q1->y * q2->y + q1->z * q2->z;
}

/// Ref: http://www.opengl-tutorial.org/assets/faq_quaternions/index.html#Q54
/// For rotation matrix conversion, handedness & axes convention matters,
/// in order to adapt the formula, I swapped the axes as follows:
///    (-z, -x, -y) <- what we get w/ formula from ref
///    (x, y, z) <- what we want
void quaternion_to_rotation_matrix(Quaternion *q, Matrix4x4 *mtx) {
    quaternion_op_normalize(q);

    const float xx = q->y * q->y;
    const float xy = q->y * q->z;
    const float xz = q->y * q->x;
    const float xw = -q->y * q->w;

    const float yy = q->z * q->z;
    const float yz = q->z * q->x;
    const float yw = -q->z * q->w;

    const float zz = q->x * q->x;
    const float zw = -q->x * q->w;

    mtx->x1y1 = 1.0f - 2.0f * (yy + zz);
    mtx->x1y2 = 2.0f * (xy - zw);
    mtx->x1y3 = 2.0f * (xz + yw);

    mtx->x2y1 = 2.0f * (xy + zw);
    mtx->x2y2 = 1.0f - 2.0f * (xx + zz);
    mtx->x2y3 = 2.0f * (yz - xw);

    mtx->x3y1 = 2.0f * (xz - yw);
    mtx->x3y2 = 2.0f * (yz + xw);
    mtx->x3y3 = 1.0f - 2.0f * (xx + yy);

    mtx->x1y4 = mtx->x2y4 = mtx->x3y4 = 0.0f;
    mtx->x4y1 = mtx->x4y2 = mtx->x4y3 = 0.0f;
    mtx->x4y4 = 1.0f;
}

/// Ref: http://www.opengl-tutorial.org/assets/faq_quaternions/index.html#Q55
/// Adapted this function axes as well, see notes above quaternion_to_rotation_matrix
void rotation_matrix_to_quaternion(const Matrix4x4 *mtx, Quaternion *q) {
    const float t = matrix4x4_get_trace(mtx);
    float x, y, z, w;
    if (t > EPSILON_ZERO) {
        const float s = sqrtf(t) * 2.0f;
        x = (mtx->x3y2 - mtx->x2y3) / s;
        y = (mtx->x1y3 - mtx->x3y1) / s;
        z = (mtx->x2y1 - mtx->x1y2) / s;
        w = .25f * s;
    } else if (mtx->x1y1 > mtx->x2y2 && mtx->x1y1 > mtx->x3y3) {
        const float s = sqrtf(1.0f + mtx->x1y1 - mtx->x2y2 - mtx->x3y3) * 2.0f;
        x = .25f * s;
        y = (mtx->x2y1 + mtx->x1y2) / s;
        z = (mtx->x1y3 + mtx->x3y1) / s;
        w = (mtx->x3y2 - mtx->x2y3) / s;
    } else if (mtx->x2y2 > mtx->x3y3) {
        const float s = sqrtf(1.0f + mtx->x2y2 - mtx->x1y1 - mtx->x3y3) * 2.0f;
        x = (mtx->x2y1 + mtx->x1y2) / s;
        y = .25f * s;
        z = (mtx->x3y2 + mtx->x2y3) / s;
        w = (mtx->x1y3 - mtx->x3y1) / s;
    } else {
        const float s = sqrtf(1.0f + mtx->x3y3 - mtx->x1y1 - mtx->x2y2) * 2.0f;
        x = (mtx->x1y3 + mtx->x3y1) / s;
        y = (mtx->x3y2 + mtx->x2y3) / s;
        z = .25f * s;
        w = (mtx->x2y1 - mtx->x1y2) / s;
    }
    q->x = -z;
    q->y = -x;
    q->z = -y;
    q->w = w;
    q->normalized = false;
}

void quaternion_to_axis_angle(Quaternion *q, float3 *axis, float *angle) {
    quaternion_op_normalize(q);

    const float cos_a = q->w;
    *angle = acosf(cos_a) * 2.0f;

    float sin_a = sqrtf(1.0f - cos_a * cos_a);
    if (fabsf(sin_a) < EPSILON_ZERO_RAD) {
        sin_a = 1.0f;
    }

    axis->x = q->y / sin_a;
    axis->y = q->z / sin_a;
    axis->z = q->x / sin_a;
}

void axis_angle_to_quaternion(float3 *axis, const float angle, Quaternion *q) {
    float3_normalize(axis);

    const float a2 = angle * .5f;
    const float sin_a = sinf(a2);
    const float cos_a = cosf(a2);

    q->x = axis->z * sin_a;
    q->y = axis->x * sin_a;
    q->z = axis->y * sin_a;
    q->w = cos_a;
    q->normalized = false;
}

void quaternion_to_euler(Quaternion *q, float3 *euler) {
    quaternion_op_normalize(q);

#if ROTATION_ORDER == 0 // XYZ
    const float singularityCheck = q->w * q->y - q->z * q->x;
    if (singularityCheck > .499f) {
        euler->x = PI_2_F;
        euler->y = -2 * atan2f(q->x, q->w);
        euler->z = 0.0f;
    } else if (singularityCheck < -.499f) {
        euler->x = -PI_2_F;
        euler->y = 2 * atan2f(q->x, q->w);
        euler->z = 0.0f;
    } else {
        const float sr_cp = 2 * (q->w * q->x + q->y * q->z);
        const float cr_cp = 1 - 2 * (q->x * q->x + q->y * q->y);
        const float roll = atan2f(sr_cp, cr_cp);

        const float sp = 2 * singularityCheck;
        const float pitch = asinf(sp);

        const float sy_cp = 2 * (q->w * q->z + q->x * q->y);
        const float cy_cp = 1 - 2 * (q->y * q->y + q->z * q->z);
        const float yaw = atan2f(sy_cp, cy_cp);

        euler->x = pitch;
        euler->y = yaw;
        euler->z = roll;
    }
#elif ROTATION_ORDER == 1 // ZYX
    // TODO: not implemented
#endif

    // remap to [0:2PI]
    if (euler->x < 0)
        euler->x += (float)(2 * M_PI);
    if (euler->y < 0)
        euler->y += (float)(2 * M_PI);
    if (euler->z < 0)
        euler->z += (float)(2 * M_PI);

    /// YZX from:
    /// https://www.euclideanspace.com/maths/geometry/rotations/conversions/quaternionToEuler/
    /*const float test = q->x * q->y + q->z * q->w;
    if (test > .499f) {
        euler->y = 2.0f * atan2f(q->x, q->w);
        euler->z = PI_2_F;
        euler->x = 0.0f;
    } else if (test < -.499f) {
        euler->y = -2.0f * atan2f(q->x, q->w);
        euler->z = -PI_2_F;
        euler->x = 0.0f;
    } else {
        const float sqx = q->x * q->x;
        const float sqy = q->y * q->y;
        const float sqz = q->z * q->z;
        euler->y = atan2f(2.0f * q->y * q->w - 2.0f * q->x * q->z, 1.0f - 2.0f * sqy - 2.0f * sqz);
        euler->z = asinf(2.0f * test);
        euler->x = atan2f(2.0f * q->x * q->w - 2.0f * q->y * q->z, 1.0f - 2.0f * sqx - 2.0f * sqz);
    }*/
}

void euler_to_quaternion(const float x, const float y, const float z, Quaternion *q) {
#if ROTATION_ORDER == 0 // XYZ
    const float cx = cosf(0.5f * x);
    const float sx = sinf(0.5f * x);
    const float cy = cosf(0.5f * y);
    const float sy = sinf(0.5f * y);
    const float cz = cosf(0.5f * z);
    const float sz = sinf(0.5f * z);

    q->x = sz * cx * cy - cz * sx * sy;
    q->y = cz * sx * cy + sz * cx * sy;
    q->z = cz * cx * sy - sz * sx * cy;
    q->w = cz * cx * cy + sz * sx * sy;
#elif ROTATION_ORDER == 1 // ZYX
    const float cx = cosf(0.5f * x);
    const float sx = sinf(0.5f * x);
    const float cy = cosf(0.5f * y);
    const float sy = sinf(0.5f * y);
    const float cz = cosf(0.5f * z);
    const float sz = sinf(0.5f * z);

    q->x = sz * cx * cy + cz * sx * sy;
    q->y = cz * sx * cy - sz * cx * sy;
    q->z = cz * cx * sy + sz * sx * cy;
    q->w = cz * cx * cy - sz * sx * sy;
#endif
    q->normalized = false;

    /// YZX from:
    /// https://www.euclideanspace.com/maths/geometry/rotations/conversions/eulerToQuaternion/
    /*const float cy = cosf(0.5f * y);
    const float sy = sinf(0.5f * y);
    const float cz = cosf(0.5f * z);
    const float sz = sinf(0.5f * z);
    const float cx = cosf(0.5f * x);
    const float sx = sinf(0.5f * x);
    const float cycz = cy * cz;
    const float sysz = sy * sz;
    q->x = cycz * sx + sysz * cx;
    q->y = sy * cz * cx + cy * sz * sx;
    q->z = cy * sz * cx - sy * cz * sx;
    q->w = cycz * cx - sysz * sx;
    q->normalized = false;*/

    /// For testing: using axis angle quaternions
    /// Ref:
    /// https://www.euclideanspace.com/maths/geometry/rotations/conversions/eulerToQuaternion/Euler%20to%20quat.pdf
    /*Quaternion q1, q2, q3;
#if ROTATION_ORDER == 0 // XYZ
    axis_angle_to_quaternion(&float3_right, x, &q1);
    axis_angle_to_quaternion(&float3_up, y, &q2);
    axis_angle_to_quaternion(&float3_forward, z, &q3);
#elif ROTATION_ORDER == 1 // ZYX
    axis_angle_to_quaternion(&float3_right, z, &q1);
    axis_angle_to_quaternion(&float3_up, y, &q2);
    axis_angle_to_quaternion(&float3_forward, x, &q3);
#endif
    Quaternion q12 = quaternion_op_mult(&q2, &q1);
    Quaternion q123 = quaternion_op_mult(&q3, &q12);
    quaternion_set(q, &q123);*/
}

void euler_to_quaternion_vec(const float3 *euler, Quaternion *q) {
    euler_to_quaternion(euler->x, euler->y, euler->z, q);
}

// MARK: - Utils -

void quaternion_rotate_vector(Quaternion *q, float3 *v) {
    quaternion_op_normalize(q);

    Quaternion pure = {v->z, v->x, v->y, 0.0f};
    Quaternion q2;
    quaternion_set(&q2, q);
    quaternion_op_inverse(&q2);

    q2 = quaternion_op_mult(&pure, &q2);
    q2 = quaternion_op_mult(q, &q2);

    v->x = q2.y;
    v->y = q2.z;
    v->z = q2.x;
}

void quaternion_op_mult_euler(float3 *euler1, const float3 *euler2) {
    Quaternion q1;
    euler_to_quaternion_vec(euler1, &q1);
    Quaternion q2;
    euler_to_quaternion_vec(euler2, &q2);
    quaternion_op_mult_right(&q2, &q1);
    quaternion_to_euler(&q1, euler1);
}

void quaternion_run_unit_tests(void) {
    Quaternion q1, q2, q3, q4;
    float f;
    float3 e1, e2, e3, e4;
    float3 v1, v2;
    Matrix4x4 *mtx1 = matrix4x4_new_identity();
    Matrix4x4 *mtx2 = matrix4x4_new_identity();

    float3_set(&e1, .2f, 1.5f, .8f);
    float3_set(&e2, .1f, .3f, 2.1f);
    float3_set(&v1, 3, -8, 2);
    float3_normalize(&v1);

    //// Redundant checks
    /// Euler
    euler_to_quaternion_vec(&e1, &q1);
    quaternion_to_euler(&q1, &e3);
    vx_assert(float3_isEqual(&e1, &e3, EPSILON_QUATERNION_ERROR));

    /// Rotation matrix
    quaternion_to_rotation_matrix(&q1, mtx1);
    rotation_matrix_to_quaternion(mtx1, &q2);
    vx_assert(quaternion_is_equal(&q1, &q2, EPSILON_QUATERNION_ERROR));

    /// Axis-angle
    axis_angle_to_quaternion(&v1, .6f, &q2);
    quaternion_to_axis_angle(&q2, &v2, &f);
    vx_assert(float3_isEqual(&v1, &v2, EPSILON_QUATERNION_ERROR));
    vx_assert(float_isEqual(f, .6f, EPSILON_QUATERNION_ERROR));

    /// Inverse
    quaternion_set(&q2, &q1);
    quaternion_op_inverse(quaternion_op_inverse(&q2));
    vx_assert(quaternion_is_equal(&q1, &q2, EPSILON_QUATERNION_ERROR));

    /// Scale
    quaternion_set(&q2, &q1);
    quaternion_op_unscale(quaternion_op_scale(&q2, .2f), .2f);
    vx_assert(quaternion_is_equal(&q1, &q2, EPSILON_QUATERNION_ERROR));

    /// Mult
    euler_to_quaternion_vec(&e2, &q2);
    q3 = quaternion_op_mult(&q1, &q2);
    q3 = quaternion_op_mult(&q3, quaternion_op_inverse(&q2));
    vx_assert(quaternion_is_equal(&q3, &q1, EPSILON_QUATERNION_ERROR));

    /// Lerp
    euler_to_quaternion_vec(&e2, &q2);
    quaternion_op_lerp(&q1, &q2, &q3, 0.0f);
    vx_assert(quaternion_is_equal(&q3, &q1, EPSILON_QUATERNION_ERROR));
    quaternion_op_lerp(&q1, &q2, &q3, 1.0f);
    vx_assert(quaternion_is_equal(&q3, &q2, EPSILON_QUATERNION_ERROR));

    /// Rotate
    float3_copy(&v2, &v1);
    quaternion_rotate_vector(&q1, &v2);
    quaternion_rotate_vector(quaternion_op_inverse(&q1), &v2);
    vx_assert(float3_isEqual(&v1, &v2, EPSILON_QUATERNION_ERROR));

    //// Singularities checks
    // TODO check every 90° steps (not sure where our singularities are)

    //// Error tolerance check
    /// number of calculations could increase with scene depth, how deep can we go without
    /// normalizing?
    /*quaternion_set(&q2, &q1);
     for (int i = 0; i < 200; ++i) {
     q2 = quaternion_op_mult(&q2, &q1);
     vx_assert(quaternion_is_normalized(&q2, EPSILON_QUATERNION_ERROR) == true)
     }*/
    /// on Android, answer is: 36 times

    //// Quaternion & matrix coherence check
    q3 = quaternion_op_mult(&q1, &q2);

    quaternion_to_rotation_matrix(&q1, mtx1);
    quaternion_to_rotation_matrix(&q2, mtx2);
    matrix4x4_op_multiply(mtx1, mtx2);

    rotation_matrix_to_quaternion(mtx1, &q4);
    vx_assert(quaternion_is_equal(&q4, &q3, EPSILON_QUATERNION_ERROR));

    quaternion_to_rotation_matrix(&q3, mtx2);
    matrix4x4_get_euler(mtx1, &e3);
    matrix4x4_get_euler(mtx2, &e4);
    vx_assert(float3_isEqual(&e3, &e4, EPSILON_QUATERNION_ERROR));

    matrix4x4_free(mtx1);
    matrix4x4_free(mtx2);
}

float4 *quaternion_to_float4(Quaternion *q) {
    return float4_new(q->x, q->y, q->z, q->w);
}

Quaternion *quaternion_from_float4(float4 *f) {
    return quaternion_new(f->x, f->y, f->z, f->w, false);
}