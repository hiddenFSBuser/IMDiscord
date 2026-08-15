#include "pch.h"
#include "crypto.h"

// Scalar multiplication of the P-256 base point.
//
// CNG will not do this: it generates keys and it agrees on secrets, but there
// is no call that turns a scalar somebody handed us into the matching point.
// MLS needs exactly that. TreeKEM derives a node secret and both sides have to
// arrive at the same key pair from it, so the multiplication has to happen
// here.
//
// None of this is on a hot path - a handful of points per epoch - so the
// reduction is done by long division rather than by the Solinas formula for
// this prime. It is several times slower and far harder to get wrong, and a
// wrong modular reduction is the kind of bug that only shows up as somebody
// else's media refusing to decrypt.

namespace
{
    typedef unsigned int u32;
    typedef unsigned long long u64;

    // 256 bits as eight 32-bit words, least significant first.
    struct big
    {
        u32 w[8];
    };

    // y^2 = x^3 - 3x + b over F_p.
    const unsigned char P_BYTES[32] = {
        0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

    const unsigned char N_BYTES[32] = {
        0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
        0xBC,0xE6,0xFA,0xAD,0xA7,0x17,0x9E,0x84,0xF3,0xB9,0xCA,0xC2,0xFC,0x63,0x25,0x51 };

    // The curve's b, needed to check that a point somebody else sent is
    // actually on this curve.
    const unsigned char B_BYTES[32] = {
        0x5A,0xC6,0x35,0xD8,0xAA,0x3A,0x93,0xE7,0xB3,0xEB,0xBD,0x55,0x76,0x98,0x86,0xBC,
        0x65,0x1D,0x06,0xB0,0xCC,0x53,0xB0,0xF6,0x3B,0xCE,0x3C,0x3E,0x27,0xD2,0x60,0x4B };

    const unsigned char GX_BYTES[32] = {
        0x6B,0x17,0xD1,0xF2,0xE1,0x2C,0x42,0x47,0xF8,0xBC,0xE6,0xE5,0x63,0xA4,0x40,0xF2,
        0x77,0x03,0x7D,0x81,0x2D,0xEB,0x33,0xA0,0xF4,0xA1,0x39,0x45,0xD8,0x98,0xC2,0x96 };

    const unsigned char GY_BYTES[32] = {
        0x4F,0xE3,0x42,0xE2,0xFE,0x1A,0x7F,0x9B,0x8E,0xE7,0xEB,0x4A,0x7C,0x0F,0x9E,0x16,
        0x2B,0xCE,0x33,0x57,0x6B,0x31,0x5E,0xCE,0xCB,0xB6,0x40,0x68,0x37,0xBF,0x51,0xF5 };

    void from_bytes(const unsigned char in[32], big* out)
    {
        for (int i = 0; i < 8; i++)
        {
            const unsigned char* p = in + (7 - i) * 4;
            out->w[i] = ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
        }
    }

    void to_bytes(const big* in, unsigned char out[32])
    {
        for (int i = 0; i < 8; i++)
        {
            unsigned char* p = out + (7 - i) * 4;
            p[0] = (unsigned char)(in->w[i] >> 24);
            p[1] = (unsigned char)(in->w[i] >> 16);
            p[2] = (unsigned char)(in->w[i] >> 8);
            p[3] = (unsigned char)(in->w[i]);
        }
    }

    void set_zero(big* a) { for (int i = 0; i < 8; i++) a->w[i] = 0; }
    void set_word(big* a, u32 v) { set_zero(a); a->w[0] = v; }
    void copy(big* dst, const big* src) { for (int i = 0; i < 8; i++) dst->w[i] = src->w[i]; }

    bool is_zero(const big* a)
    {
        u32 acc = 0;
        for (int i = 0; i < 8; i++) acc |= a->w[i];
        return acc == 0;
    }

    bool equal(const big* a, const big* b)
    {
        u32 diff = 0;
        for (int i = 0; i < 8; i++) diff |= a->w[i] ^ b->w[i];
        return diff == 0;
    }

    // -1, 0 or 1.
    int compare(const big* a, const big* b)
    {
        for (int i = 7; i >= 0; i--)
        {
            if (a->w[i] != b->w[i]) return a->w[i] < b->w[i] ? -1 : 1;
        }
        return 0;
    }

    u32 add_raw(big* r, const big* a, const big* b)
    {
        u64 carry = 0;
        for (int i = 0; i < 8; i++)
        {
            u64 sum = (u64)a->w[i] + b->w[i] + carry;
            r->w[i] = (u32)sum;
            carry = sum >> 32;
        }
        return (u32)carry;
    }

    u32 sub_raw(big* r, const big* a, const big* b)
    {
        u64 borrow = 0;
        for (int i = 0; i < 8; i++)
        {
            u64 diff = (u64)a->w[i] - b->w[i] - borrow;
            r->w[i] = (u32)diff;
            borrow = (diff >> 32) & 1;
        }
        return (u32)borrow;
    }

    big g_p;
    big g_n;
    big g_b;
    bool g_ready = false;

    void ensure_constants()
    {
        if (g_ready) return;
        from_bytes(P_BYTES, &g_p);
        from_bytes(N_BYTES, &g_n);
        from_bytes(B_BYTES, &g_b);
        g_ready = true;
    }

    void mod_add(big* r, const big* a, const big* b)
    {
        u32 carry = add_raw(r, a, b);
        if (carry || compare(r, &g_p) >= 0)
        {
            big t;
            sub_raw(&t, r, &g_p);
            copy(r, &t);
        }
    }

    void mod_sub(big* r, const big* a, const big* b)
    {
        u32 borrow = sub_raw(r, a, b);
        if (borrow)
        {
            big t;
            add_raw(&t, r, &g_p);
            copy(r, &t);
        }
    }

    // Reduces a 512 bit product, held as sixteen words least significant first.
    // The accumulator needs one word more than the modulus: shifting a value
    // already below p left by one bit can reach 2p.
    void reduce_512(const u32 product[16], big* out)
    {
        u32 acc[9];
        for (int i = 0; i < 9; i++) acc[i] = 0;

        for (int bit = 511; bit >= 0; bit--)
        {
            // acc <<= 1
            u32 carry = 0;
            for (int i = 0; i < 9; i++)
            {
                u32 next = acc[i] >> 31;
                acc[i] = (acc[i] << 1) | carry;
                carry = next;
            }

            acc[0] |= (product[bit >> 5] >> (bit & 31)) & 1;

            // if acc >= p then acc -= p
            bool ge = acc[8] != 0;
            if (!ge)
            {
                ge = true;
                for (int i = 7; i >= 0; i--)
                {
                    if (acc[i] != g_p.w[i]) { ge = acc[i] > g_p.w[i]; break; }
                    if (i == 0) ge = true;    // exactly equal
                }
            }

            if (ge)
            {
                u64 borrow = 0;
                for (int i = 0; i < 8; i++)
                {
                    u64 diff = (u64)acc[i] - g_p.w[i] - borrow;
                    acc[i] = (u32)diff;
                    borrow = (diff >> 32) & 1;
                }
                acc[8] -= (u32)borrow;
            }
        }

        for (int i = 0; i < 8; i++) out->w[i] = acc[i];
    }

    void mod_mul(big* r, const big* a, const big* b)
    {
        u32 product[16];
        for (int i = 0; i < 16; i++) product[i] = 0;

        for (int i = 0; i < 8; i++)
        {
            u64 carry = 0;
            for (int j = 0; j < 8; j++)
            {
                u64 cur = (u64)a->w[i] * b->w[j] + product[i + j] + carry;
                product[i + j] = (u32)cur;
                carry = cur >> 32;
            }
            product[i + 8] = (u32)carry;
        }

        reduce_512(product, r);
    }

    void mod_sqr(big* r, const big* a) { mod_mul(r, a, a); }

    // a^e mod p, with the exponent given as bytes, most significant first.
    void mod_exp(big* r, const big* a, const unsigned char* e, unsigned int e_len)
    {
        big result;
        set_word(&result, 1);

        for (unsigned int i = 0; i < e_len; i++)
        {
            for (int bit = 7; bit >= 0; bit--)
            {
                big t;
                mod_sqr(&t, &result);
                copy(&result, &t);

                if ((e[i] >> bit) & 1)
                {
                    mod_mul(&t, &result, a);
                    copy(&result, &t);
                }
            }
        }
        copy(r, &result);
    }

    // Fermat: a^(p-2). Slower than the extended euclidean algorithm and used
    // twice per scalar multiplication, which is nothing.
    void mod_inv(big* r, const big* a)
    {
        big exponent;
        big two;
        set_word(&two, 2);
        sub_raw(&exponent, &g_p, &two);

        unsigned char bytes[32];
        to_bytes(&exponent, bytes);
        mod_exp(r, a, bytes, 32);
    }

    // Jacobian coordinates: the affine point is (X/Z^2, Y/Z^3), and Z zero is
    // the point at infinity.
    struct jacobian
    {
        big x, y, z;
    };

    void set_infinity(jacobian* p)
    {
        set_word(&p->x, 1);
        set_word(&p->y, 1);
        set_zero(&p->z);
    }

    bool at_infinity(const jacobian* p) { return is_zero(&p->z); }

    // dbl-2001-b, which assumes a = -3. P-256 does.
    void point_double(jacobian* r, const jacobian* p)
    {
        if (at_infinity(p) || is_zero(&p->y)) { set_infinity(r); return; }

        big delta, gamma, beta, alpha, t1, t2, t3;

        mod_sqr(&delta, &p->z);            // delta = Z^2
        mod_sqr(&gamma, &p->y);            // gamma = Y^2
        mod_mul(&beta, &p->x, &gamma);     // beta = X*gamma

        mod_sub(&t1, &p->x, &delta);       // X - delta
        mod_add(&t2, &p->x, &delta);       // X + delta
        mod_mul(&t3, &t1, &t2);
        mod_add(&alpha, &t3, &t3);
        mod_add(&alpha, &alpha, &t3);      // alpha = 3*(X-delta)*(X+delta)

        big x3, y3, z3, eight_beta;
        mod_add(&t1, &beta, &beta);        // 2 beta
        mod_add(&t1, &t1, &t1);            // 4 beta
        mod_add(&eight_beta, &t1, &t1);    // 8 beta

        mod_sqr(&x3, &alpha);
        mod_sub(&x3, &x3, &eight_beta);

        mod_add(&t2, &p->y, &p->z);
        mod_sqr(&t2, &t2);
        mod_sub(&t2, &t2, &gamma);
        mod_sub(&z3, &t2, &delta);         // Z3 = (Y+Z)^2 - gamma - delta

        mod_sub(&t2, &t1, &x3);            // 4 beta - X3
        mod_mul(&y3, &alpha, &t2);

        mod_sqr(&t3, &gamma);              // gamma^2
        mod_add(&t3, &t3, &t3);
        mod_add(&t3, &t3, &t3);
        mod_add(&t3, &t3, &t3);            // 8 gamma^2
        mod_sub(&y3, &y3, &t3);

        copy(&r->x, &x3);
        copy(&r->y, &y3);
        copy(&r->z, &z3);
    }

    // madd-2007-bl: Jacobian plus affine, which is all the base point needs.
    void point_add_affine(jacobian* r, const jacobian* p,
                          const big* qx, const big* qy)
    {
        if (at_infinity(p))
        {
            copy(&r->x, qx);
            copy(&r->y, qy);
            set_word(&r->z, 1);
            return;
        }

        big z1z1, u2, s2, h, hh, i, j, rr, v, t1, t2;

        mod_sqr(&z1z1, &p->z);
        mod_mul(&u2, qx, &z1z1);

        mod_mul(&t1, &p->z, &z1z1);
        mod_mul(&s2, qy, &t1);

        mod_sub(&h, &u2, &p->x);
        mod_sub(&t2, &s2, &p->y);

        if (is_zero(&h))
        {
            if (is_zero(&t2)) { point_double(r, p); return; }
            set_infinity(r);
            return;
        }

        mod_sqr(&hh, &h);
        mod_add(&i, &hh, &hh);
        mod_add(&i, &i, &i);               // I = 4*HH
        mod_mul(&j, &h, &i);
        mod_add(&rr, &t2, &t2);            // r = 2*(S2-Y1)
        mod_mul(&v, &p->x, &i);

        big x3, y3, z3;
        mod_sqr(&x3, &rr);
        mod_sub(&x3, &x3, &j);
        mod_sub(&x3, &x3, &v);
        mod_sub(&x3, &x3, &v);             // X3 = r^2 - J - 2V

        mod_sub(&t1, &v, &x3);
        mod_mul(&y3, &rr, &t1);
        mod_mul(&t1, &p->y, &j);
        mod_add(&t1, &t1, &t1);            // 2*Y1*J
        mod_sub(&y3, &y3, &t1);

        mod_add(&t1, &p->z, &h);
        mod_sqr(&t1, &t1);
        mod_sub(&t1, &t1, &z1z1);
        mod_sub(&z3, &t1, &hh);            // Z3 = (Z1+H)^2 - Z1Z1 - HH

        copy(&r->x, &x3);
        copy(&r->y, &y3);
        copy(&r->z, &z3);
    }

    bool to_affine(const jacobian* p, big* x, big* y)
    {
        if (at_infinity(p)) return false;

        big zinv, zinv2, zinv3;
        mod_inv(&zinv, &p->z);
        mod_sqr(&zinv2, &zinv);
        mod_mul(&zinv3, &zinv2, &zinv);

        mod_mul(x, &p->x, &zinv2);
        mod_mul(y, &p->y, &zinv3);
        return true;
    }
}

bool crypto::p256_scalar_point_mult(const unsigned char scalar[32],
                                    const unsigned char point_x[32],
                                    const unsigned char point_y[32],
                                    unsigned char out_x[32], unsigned char out_y[32])
{
    ensure_constants();

    big d;
    from_bytes(scalar, &d);
    if (is_zero(&d) || compare(&d, &g_n) >= 0) return false;

    big px, py;
    from_bytes(point_x, &px);
    from_bytes(point_y, &py);

    // A point somebody else chose has to be checked before it is used. An
    // attacker who can hand over a point on a different, weaker curve learns
    // the private scalar from the result; this is the invalid curve attack,
    // and one line of arithmetic is the whole defence.
    //
    //   y^2 == x^3 - 3x + b
    {
        big lhs, x2, x3, three_x, rhs;

        mod_sqr(&lhs, &py);
        mod_sqr(&x2, &px);
        mod_mul(&x3, &x2, &px);

        mod_add(&three_x, &px, &px);
        mod_add(&three_x, &three_x, &px);

        mod_sub(&rhs, &x3, &three_x);
        mod_add(&rhs, &rhs, &g_b);

        if (compare(&lhs, &rhs) != 0) return false;
    }

    jacobian acc;
    set_infinity(&acc);

    // Double and add, most significant bit first - the same walk the base
    // point version does, with the peer's point instead of G.
    for (int bit = 255; bit >= 0; bit--)
    {
        jacobian doubled;
        point_double(&doubled, &acc);
        acc = doubled;

        if ((d.w[bit >> 5] >> (bit & 31)) & 1)
        {
            jacobian sum;
            point_add_affine(&sum, &acc, &px, &py);
            acc = sum;
        }
    }

    big x, y;
    if (!to_affine(&acc, &x, &y)) return false;

    to_bytes(&x, out_x);
    to_bytes(&y, out_y);
    return true;
}

bool crypto::p256_scalar_base_mult(const unsigned char scalar[32],
                                   unsigned char out_x[32], unsigned char out_y[32])
{
    ensure_constants();

    big d;
    from_bytes(scalar, &d);

    // Zero is the point at infinity and anything at or above the group order is
    // not a private key at all.
    if (is_zero(&d) || compare(&d, &g_n) >= 0) return false;

    big gx, gy;
    from_bytes(GX_BYTES, &gx);
    from_bytes(GY_BYTES, &gy);

    jacobian acc;
    set_infinity(&acc);

    // Plain double and add, most significant bit first. This is not constant
    // time; the scalars it runs on are derived key material rather than long
    // lived identity keys, and nothing here shares a machine with an attacker
    // measuring it.
    for (int bit = 255; bit >= 0; bit--)
    {
        jacobian doubled;
        point_double(&doubled, &acc);
        acc = doubled;

        if ((d.w[bit >> 5] >> (bit & 31)) & 1)
        {
            jacobian sum;
            point_add_affine(&sum, &acc, &gx, &gy);
            acc = sum;
        }
    }

    big x, y;
    if (!to_affine(&acc, &x, &y)) return false;

    to_bytes(&x, out_x);
    to_bytes(&y, out_y);
    return true;
}

bool crypto::p256_keypair_from_scalar(const unsigned char scalar[32],
                                      unsigned char public_key[65],
                                      unsigned char private_key[96])
{
    unsigned char x[32], y[32];
    if (!p256_scalar_base_mult(scalar, x, y)) return false;

    // The layout CNG exports: X, then Y, then the scalar, all big endian.
    ccpy(private_key, x, 32);
    ccpy(private_key + 32, y, 32);
    ccpy(private_key + 64, scalar, 32);

    public_key[0] = 0x04;
    ccpy(public_key + 1, x, 32);
    ccpy(public_key + 33, y, 32);
    return true;
}

bool crypto::p256_scalar_in_range(const unsigned char scalar[32])
{
    ensure_constants();

    big d;
    from_bytes(scalar, &d);
    return !is_zero(&d) && compare(&d, &g_n) < 0;
}
