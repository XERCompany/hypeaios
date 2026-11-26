#ifndef HYPE_FREESTANDING_MATH_H
#define HYPE_FREESTANDING_MATH_H

double fabs(double x);
double floor(double x);
double ldexp(double x, int e);
double ceil(double x);
double trunc(double x);
double fmod(double x, double y);
double pow(double x, double y);
double exp(double x);
double log(double x);
double log10(double x);
double sqrt(double x);
double sin(double x);
double cos(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);
double sinh(double x);
double cosh(double x);
double tanh(double x);
int isnan(double x);
int isinf(double x);
int isfinite(double x);

#endif
