#include "pch.h"
#include "customcrt_math.h"
#include <xmmintrin.h>

float csqrtf(float number) {
    float res;
    _mm_store_ss(&res, _mm_sqrt_ss(_mm_set_ss(number)));
    return res;
    //if (number < 0.0f) return -0.0f; 
    //if (number == 0.0f) return 0.0f;
    //
    //float x = number;
    //for (int i = 0; i < 10; i++)
    //{
    //    x = 0.5f * (x + number / x);
    //}
    //return x;
}

double csqrt(double number) {
    if (number < 0.0) return cNAN();
    //double res;
    //_mm_store_sd(&res, _mm_sqrt_sd(_mm_set_sd(number), _mm_set_sd(number)));
    //return res;

    if (number < 0.0f) return -0.0f;
    if (number == 0.0f) return 0.0f;

    float x = number;
    for (int i = 0; i < 5; i++)
    {
        x = 0.5f * (x + number / x);
    }
    return x;
}

float cfsqrtf(float number) {
    if (number < 0.0f) {
        return cNAN();
    }
    if (number == 0.0f) {
        return 0.0f;
    }

    union { float f; uint32 i; } conv = { number };
    conv.i = 0x5F3759DF - (conv.i >> 1); //  1/sqrt(x)
    float guess = conv.f * number;

    // guess = (guess + number / guess) / 2
    guess = 0.5f * (guess + number / guess);
    guess = 0.5f * (guess + number / guess);
    guess = 0.5f * (guess + number / guess);
    guess = 0.5f * (guess + number / guess);
    guess = 0.5f * (guess + number / guess);

    return guess;
}

float csinf(float x) {
    x = cfmodf(x, TWO_PI);
    if (x > PI) x -= TWO_PI;
    if (x < -PI) x += TWO_PI;

    if (x > HALF_PI) x = PI - x;
    if (x < -HALF_PI) x = -PI - x;

    // x - x^3/3! + x^5/5! - x^7/7! + ...
    const float x2 = x * x;
    float term = x;
    float sum = x;

    // x^3/3!
    term *= -x2 / (2 * 3);
    sum += term;
    // x^5/5!
    term *= -x2 / (4 * 5);
    sum += term;
    // x^7/7!
    term *= -x2 / (6 * 7);
    sum += term;
    // x^9/9!
    term *= -x2 / (8 * 9);
    sum += term;

    return sum;
}

float ccosf(float x) {
    return csinf(x + HALF_PI);
}

float csin(float x) {
    x = cfmodf(x, TWO_PI);

    if (x > PI) x -= TWO_PI;
    if (x < -PI) x += TWO_PI;

    if (x > HALF_PI) x = PI - x;
    if (x < -HALF_PI) x = -PI - x;

    const float x2 = x * x;
    float term = x;
    float sum = x;

    // x^3 / 3!
    term *= -x2 / 6.0f;
    sum += term;
    // x^5 / 5!
    term *= -x2 / 20.0f;
    sum += term;
    // x^7 / 7!
    term *= -x2 / 42.0f;
    sum += term;
    // x^9 / 9!
    term *= -x2 / 72.0f;
    sum += term;

    return sum;
}

float ccos(float x) {
    x = cfmodf(x, TWO_PI);

    if (x > PI) x -= TWO_PI;
    if (x < -PI) x += TWO_PI;

    if (x < 0.0f) x = -x;

    // cos(x) = -cos(PI - x)
    float sign = 1.0f;
    if (x > HALF_PI) {
        x = PI - x;
        sign = -1.0f;
    }

    const float x2 = x * x;
    float term = 1.0f;
    float sum = 1.0f;

    // x^2 / 2!
    term *= -x2 / 2.0f;
    sum += term;
    // x^4 / 4!
    term *= -x2 / 12.0f;
    sum += term;
    // x^6 / 6!
    term *= -x2 / 30.0f;
    sum += term;
    // x^8 / 8!
    term *= -x2 / 56.0f;
    sum += term;

    return sign * sum;
}

float casinf(float x) {
    if (x > 1.0f || x < -1.0f) {
        return cNAN();
    }
    if (x == 1.0f) return HALF_PI;
    if (x == -1.0f) return -HALF_PI;

    return catanf(x / csqrtf(1.0f - x * x));
}


float catanf(float x) {
    float abs_x = (x < 0.0f) ? -x : x;
    float res;

    if (abs_x > 1.0f) {
        float inv_x = 1.0f / abs_x;
        float x2 = inv_x * inv_x;
        res = HALF_PI - (inv_x * (0.9998660f + x2 * (-0.3302995f + x2 * (0.1801410f + x2 * (-0.0851330f + x2 * 0.0208351f)))));
    }
    else {
        float x2 = abs_x * abs_x;
        res = abs_x * (0.9998660f + x2 * (-0.3302995f + x2 * (0.1801410f + x2 * (-0.0851330f + x2 * 0.0208351f))));
    }

    return (x < 0.0f) ? -res : res;
}

float catan2f(float y, float x) {
    if (x == 0.0f) {
        if (y > 0.0f) return HALF_PI;
        if (y < 0.0f) return -HALF_PI;
        return 0.0f; // atan2(0,0) -> 0
    }

    if (x > 0.0f) {
        return catanf(y / x);
    }
    if (x < 0.0f) {
        if (y >= 0.0f) {
            return catanf(y / x) + PI;
        }
        else {
            return catanf(y / x) - PI;
        }
    }
    return 0.0f;
}

double catan(double x) {
    double abs_x = (x < 0.0) ? -x : x;
    double res;
    double x2;
    double val;

    if (abs_x > 1.0) {
        val = 1.0 / abs_x;
        x2 = val * val;
        res = HALF_PI - (val * (0.99999999999925182 + x2 * (-0.33333333331704839 + x2 * (0.19999999844654009 + x2 * (-0.14285709743945028 + x2 * (0.11111010750060511 + x2 * (-0.09088714443953337 + x2 * (0.07633510948064283 + x2 * -0.0632506537621228))))))));
    }
    else {
        val = abs_x;
        x2 = val * val;
        res = val * (0.99999999999925182 + x2 * (-0.33333333331704839 + x2 * (0.19999999844654009 + x2 * (-0.14285709743945028 + x2 * (0.11111010750060511 + x2 * (-0.09088714443953337 + x2 * (0.07633510948064283 + x2 * -0.0632506537621228)))))));
    }

    return (x < 0.0) ? -res : res;
}

double casin(double x) {
    if (x > 1.0 || x < -1.0) {
        return cNAN();
    }
    if (x == 1.0) return HALF_PI;
    if (x == -1.0) return -HALF_PI;

    // arcsin(x) = arctan( x / sqrt(1 - x^2) )
    return catan(x / csqrt(1.0 - x * x));
}

double catan2(double y, double x) {
    if (x == 0.0) {
        if (y > 0.0) return HALF_PI;
        if (y < 0.0) return -HALF_PI;
        return 0.0; // atan2(0,0) -> 0
    }

    if (x > 0.0) {
        return catan(y / x);
    }
    if (x < 0.0) {
        if (y >= 0.0) {
            return catan(y / x) + PI;
        }
        else {
            return catan(y / x) - PI;
        }
    }
    return 0.0;
}

float cclamp(float val, float min, float max) {
    if (val < min)
        return min;
    if (max < val)
        return max;
    return val;
}
double ccldexp(double x, int exp) {
    if (x == 0.0 || exp == 0) return x;

    if (x != x) return x;

    int is_neg = (exp < 0);
    unsigned int uexp;

    if (is_neg) {
        uexp = (exp == -2147483648) ? 2147483648U : (unsigned int)(-exp);
    }
    else {
        uexp = (unsigned int)exp;
    }

    if (uexp > 2500) uexp = 2500;

    double mult64 = is_neg ? (1.0 / 18446744073709551616.0) : 18446744073709551616.0; // 2^-64 / 2^64
    double mult8 = is_neg ? (1.0 / 256.0) : 256.0;                                // 2^-8 / 2^8
    double mult1 = is_neg ? 0.5 : 2.0;                                            // 2^-1 / 2^1

    while (uexp >= 64) {
        x *= mult64;
        uexp -= 64;
    }
    while (uexp >= 8) {
        x *= mult8;
        uexp -= 8;
    }
    while (uexp > 0) {
        x *= mult1;
        uexp--;
    }

    return x;
}

unsigned int cc_as_uint(float f) {
    return *(unsigned int*)&f;
}

bool cisfinite(float f) {
    unsigned int bits = cc_as_uint(f);
    return (bits & 0x7F800000) != 0x7F800000;
}

float ccldexpf(float x, int exp) {
    if (x == 0.0f || exp == 0) return x;
    if (x != x) return x; // NaN

    int is_neg = (exp < 0);
    unsigned int uexp;

    if (is_neg) {
        uexp = (exp == -2147483648) ? 2147483648U : (unsigned int)(-exp);
    }
    else {
        uexp = (unsigned int)exp;
    }

    if (uexp > 300) uexp = 300;

    float mult64 = is_neg ? (1.0f / 18446744073709551616.0f) : 18446744073709551616.0f;
    float mult8 = is_neg ? (1.0f / 256.0f) : 256.0f;
    float mult1 = is_neg ? 0.5f : 2.0f;

    while (uexp >= 64) {
        x *= mult64;
        uexp -= 64;
    }
    while (uexp >= 8) {
        x *= mult8;
        uexp -= 8;
    }
    while (uexp > 0) {
        x *= mult1;
        uexp--;
    }

    return x;
}

float ctanf(float x) {
    x = cfmodf(x, PI);
    if (x > PI / 2.0f) x -= PI;
    if (x < -PI / 2.0f) x += PI;

    float x2 = x * x;

    float res = x * (1.0f + x2 * (0.333333333f + x2 * (0.133333333f + x2 * (0.053968253f))));
    return res;
}

float clogf(float x) {
    union { float f; int i; } u = { x };
    float log_2 = (float)u.i * 1.1920928955078125e-7f - 126.94269504f;
    return log_2 * 0.69314718f;
}

float cexf(float x) {
    union { float f; int i; } u;
    u.i = (int)(x * 12102203.0f + 1064866805.0f);
    return u.f;
}

float cpowf(float x, float y) {
    if (y == 0) return 1.0f;
    if (x == 0) return 0.0f;
    if (y == 1.0f) return x;

    float y_abs = (y < 0) ? -y : y;
    if (y_abs == (float)(int)y_abs) {
        float res = 1.0f;
        int p = (int)y_abs;
        while (p > 0) {
            if (p & 1) res *= x;
            x *= x;
            p >>= 1;
        }
        return (y < 0) ? (1.0f / res) : res;
    }

    if (x < 0) return cNAN();
    return cexf(y * clogf(x));
}

double cpow(double x, double y) {
    return (double)cpowf((float)x, (float)y);
}
float cfloorf(float x) {
    if (x >= 8388608.0f || x <= -8388608.0f) return x;

    int i = (int)x;
    float fi = (float)i;

    if (x < 0.0f && x != fi) return fi - 1.0f;

    return fi;
}

float cceilf(float x) {
    if (x >= 8388608.0f || x <= -8388608.0f) return x;

    int i = (int)x;
    float fi = (float)i;

    if (x > 0.0f && x != fi) return fi + 1.0f;

    return fi;
}

double cfloor(double x) {
    if (x >= 4503599627370496.0 || x <= -4503599627370496.0) return x;

    long long i = (long long)x;
    double di = (double)i;

    if (x < 0.0 && x != di) return di - 1.0;

    return di;
}

float croundf(float x) {
    if (x >= 0.0f) {
        return (float)(long)(x + 0.5f);
    }
    else {
        return (float)(long)(x - 0.5f);
    }
}

double cround(double x) {
    if (x >= 0.0) {
        return (double)(long long)(x + 0.5);
    }
    else {
        return (double)(long long)(x - 0.5);
    }
}

double cceil(double x) {
    if (x >= 4503599627370496.0 || x <= -4503599627370496.0) return x;

    long long i = (long long)x;
    double di = (double)i;

    if (x > 0.0 && x != di) return di + 1.0;

    return di;
}
float cfmodf(float x, float y) {
    if (y == 0.0f) return cNAN();

    float tmp = x / y;
    int i = (int)tmp;
    float fi = (float)i;

    float res = x - fi * y;
    return res;
}

double cfmod(double x, double y) {
    if (y == 0.0) return cNAN();

    double tmp = x / y;
    long long i = (long long)tmp;
    double di = (double)i;

    return x - di * y;
}
float cacosf(float x) {
    if (x < -1.0f || x > 1.0f) return cNAN();

    return catan2f(csqrtf(1.0f - x * x), x);
}

double cacos(double x) {
    if (x < -1.0 || x > 1.0) return (double)cNAN();

    return (double)catan2f((float)csqrt((double)(1.0 - x * x)), (float)x);
}