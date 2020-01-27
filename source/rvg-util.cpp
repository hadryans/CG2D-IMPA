#ifdef USE_LAPACK
#include <lapacke.h>
#endif

#include "rvg-util.h"
#include <iostream>

namespace rvg {
    namespace util {

float det(float a, float b, float c,
          float d, float e, float f,
          float g, float h, float i) {
    return a*e*i + b*f*g  + d*h*c - c*e*g - a*f*h - b*d*i;
}

float sq(float x) {
    return x*x;
}

float cube(float x) {
    return x*x*x;
}

float cr(float x, float y, float u, float v) {
    return v*x - u*y;
}

std::tuple<float, float> curvature(float ddx, float ddy, float ddw,
    float dx, float dy, float dw, float x, float y, float w) {
    float s = cube(w) * det(ddx, ddy, ddw, dx, dy, dw, x, y, w);
    float t = cube(std::hypot(cr(x, w, dx, dw),cr(y, w, dy, dw)));
    return std::make_tuple(s, t);
}

#ifdef USE_LAPACK
bool linear_solve(float a00, float a01, float a10, float a11,
    float b0, float b1, float *x, float *y) {
    lapack_int m = 2;
    lapack_int n = 2;
    lapack_int nrhs = 1;
    float a[4] = {a00, a10, a01, a11};
    lapack_int lda = 2;
    float b[2] = {b0, b1};
    lapack_int ldb = 2;
    lapack_int jpvt[2];
    float rcond = -1;
    lapack_int rank;
    float work[16];
    lapack_int lwork = 16;
    lapack_int info;
    LAPACK_sgelsy(&m, &n, &nrhs, a, &lda, b, &ldb, jpvt, &rcond, &rank,
        work, &lwork, &info);
    *x = b[0];
    *y = b[1];
    return true;
}
#else

template <typename T>
static bool linear_solve_helper(T a00, T a01, T a10, T a11, T b0, T b1,
    T *x, T *y) {
    if (is_almost_zero(a00)) {
        *x = T(0); *y = T(0);
        return false;
    }
    auto s = a10/a00;
    auto d = (a11-a01*s);
    if (is_almost_zero(d)) {
        *x = T(0); *y = T(0);
        return false;
    }
    *y = (b1-b0*s)/d;
    *x = (b0-a01*(*y))/a00;
    return true;
}

//??D This is not a good idea. Should stop writing linear
//    algebra code and use a library.
bool linear_solve(float a00, float a01, float a10, float a11, float b0, float b1, float *x, float *y) {
    // Pivot
    if (std::fabs(a00) < std::fabs(a10))
        return linear_solve_helper(a10, a11, a00, a01, b1, b0, x, y);
    else
        return linear_solve_helper(a00, a01, a10, a11, b0, b1, x, y);
}

bool linear_solve(double a00, double a01, double a10, double a11, double b0, double b1, double *x, double *y) {
    // Pivot
    if (std::fabs(a00) < std::fabs(a10))
        return linear_solve_helper(a10, a11, a00, a01, b1, b0, x, y);
    else
        return linear_solve_helper(a00, a01, a10, a11, b0, b1, x, y);
}
#endif

} } // namespace rvg::util
