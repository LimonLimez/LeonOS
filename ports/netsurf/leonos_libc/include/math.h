#ifndef LEONOS_LIBC_MATH_H
#define LEONOS_LIBC_MATH_H

#define M_PI 3.14159265358979323846
#define M_PI_2 1.57079632679489661923
#define INFINITY (1.0 / 0.0)
#define NAN (0.0 / 0.0)

double fabs(double x);
float fabsf(float x);
double fmin(double x, double y);
double fmax(double x, double y);
double fmod(double x, double y);
double floor(double x);
double ceil(double x);
double round(double x);
double trunc(double x);
long lrint(double x);
float ceilf(float x);
double sqrt(double x);
double hypot(double x, double y);
double cbrt(double x);
double pow(double x, double y);
float powf(float x, float y);
double sin(double x);
double cos(double x);
double acos(double x);
double asin(double x);
double atan(double x);
double atan2(double y, double x);
double tan(double x);
double sinh(double x);
double cosh(double x);
double tanh(double x);
double asinh(double x);
double acosh(double x);
double atanh(double x);
double exp(double x);
double expm1(double x);
double log(double x);
double log1p(double x);
double log2(double x);
double log10(double x);
long lroundf(float x);
int isnan(double x);
int isfinite(double x);
int isinf(double x);
int signbit(double x);

#endif
