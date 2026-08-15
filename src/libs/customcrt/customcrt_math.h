#pragma once
constexpr float PI = 3.1415926535f;
constexpr float DEG_TO_RAD = PI / 180.0f;
constexpr float RAD_TO_DEG = 180.0f / PI;

constexpr float TWO_PI = 2.0f * PI;
constexpr float HALF_PI = 0.5f * PI;
constexpr float INV_PI = 1.0f / PI;

constexpr float cNAN() {
    union {
        unsigned int i;
        float f;
    } nan_union{};

    nan_union.i = 0x7FC00001;
    return nan_union.f;
}

float csqrtf(float number); // xmm instruct
double csqrt(double number); // tailor method, five steps
float cfsqrtf(float number); // fast sqrtf, five tailor steps
float csinf(float x);
float ccosf(float x);
float csin(float x);
float ccos(float x);
float catanf(float x);
float casinf(float x);
float catan2f(float y, float x);
double catan(double x);
double casin(double x);
double catan2(double y, double x);
float clogf(float x);
float cexf(float x);
float cpowf(float x, float y);
double cpow(double x, double y);

float cfloorf(float x);
double cfloor(double x);
float cceilf(float x);
double cceil(double x);
float croundf(float x);
double cround(double x);

float cacosf(float x);
double cacos(double x);
double cfmod(double x, double y);
float cfmodf(float x, float y);

float cclamp(float val, float min, float max);
float ctanf(float x);
double ccldexp(double x, int exp);
float ccldexpf(float x, int exp);

bool cisfinite(float f);
inline float cabs(float x) { return (x < 0.0f) ? -x : x; }
inline float cfabs(float x) { return (x < 0.0f) ? -x : x; }
inline double cfabs(double x) { return (x < 0.0) ? -x : x; }

#define ccmin(a, b) (a > b ? b : a)
#define ccmax(a, b) (a > b ? a : b)
#define cclerp(a, b, val) (a > b ? (b + ((a - b) * val)) : (a + ((b - a) * val)))