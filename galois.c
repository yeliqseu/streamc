/************************************************************************
 * galois.c
 * Functions of Galois field arithmetic.
 * Supports GF(2), GF(4), ..., GF(256)
 ************************************************************************/
#include <stdlib.h>
#include <string.h>
#if defined(INTEL_SSSE3)
#include <tmmintrin.h>
#endif
#if defined(INTEL_AVX2)
#include <immintrin.h>
#endif
#include "galois.h"
static int constructed = 0;
//static GF_ELEMENT galois_log_table[1<<GF_POWER];
//static GF_ELEMENT galois_ilog_table[(1<<GF_POWER)];
//static GF_ELEMENT galois_mult_table[(1<<GF_POWER)*(1<<GF_POWER)];
//static GF_ELEMENT galois_divi_table[(1<<GF_POWER)*(1<<GF_POWER)];
static int GF_POWER;
static GF_ELEMENT *galois_log_table;
static GF_ELEMENT *galois_ilog_table;
static GF_ELEMENT *galois_mult_table;
static GF_ELEMENT *galois_divi_table;


// Two half tables are used for SSE multiply_add_region, used only by GF(2) and GF(2^8)
#if defined(INTEL_SSSE3)
static uint8_t galois_half_mult_table_high[(1<<8)][(1<<4)];
static uint8_t galois_half_mult_table_low[(1<<8)][(1<<4)];
#endif

static int primitive_poly_2  = 07;      // 111: x^2 + x + 1
static int primitive_poly_3  = 013;     // 001 011: x^3 + x + 1 
static int primitive_poly_4  = 023;     // 010 011: x^4 + x + 1
static int primitive_poly_5  = 045;     // 100 101: x^5 + x^2 + 1
static int primitive_poly_6  = 0103;    // 001 000 011: x^6 + x + 1
static int primitive_poly_7  = 0203;    // 010 000 011: x^7 + x + 1
static int primitive_poly_8  = 0435;    // 100 011 101: x^8 + x^4 + x^3 + x^2 + 1
static int galois_create_log_table();
static int galois_create_mult_table();

int GFConstructed() {
    return constructed;
}

int constructField(int gf_power)
{
    if (constructed) {
        // printf("Galois field GF(2^%d) exists.\n", GF_POWER);
        return 0;
    } else {
        if (gf_power==1)
            GF_POWER = 8;           // Use GF(256) for GF(2)
        else
            GF_POWER = gf_power;
        // Allocate memory for GF lookup tables
        galois_log_table = calloc((1<<GF_POWER), sizeof(GF_ELEMENT));
        galois_ilog_table = calloc((1<<GF_POWER), sizeof(GF_ELEMENT));
        galois_mult_table = calloc((1<<GF_POWER)*(1<<GF_POWER), sizeof(GF_ELEMENT));
        galois_divi_table = calloc((1<<GF_POWER)*(1<<GF_POWER), sizeof(GF_ELEMENT));
        if (galois_create_mult_table() < 0) {
            perror("constructField");
            exit(1);
        }

#if defined(INTEL_SSSE3)
        /*
        * Create half tables for SSE multiply_add_region:
        * low table contains the products of an element with all 4-bit words;
        * high table contains the products of an element with all 8-bit words
        * whose last 4 bits are all zero. So each half table contains 256 rows
        * and 16 columns.
        */
       if (GF_POWER == 8) {
            int a, b, c, d;
            int pp = primitive_poly_8;
            for (a = 1; a < (1<<4) ; a++) {
                b = 1;
                c = a;
                d = (a << 4);
                do {
                    galois_half_mult_table_low[b][a] = c;
                    galois_half_mult_table_high[b][a] = d;
                    b <<= 1;
                    if (b & (1<<8)) b ^= pp;
                    c <<= 1;
                    if (c & (1<<8)) c ^= pp;
                    d <<= 1;
                    if (d & (1<<8)) d ^= pp;
                } while (c != a);
            }
        }
#endif  // ifdef(INTEL_SSSE3)
        constructed = 1;
    }
    return 0;
}

static int galois_create_log_table()
{
    int j, b;
    int m = GF_POWER;

    int gf_poly;
    if (GF_POWER==2)
        gf_poly = primitive_poly_2;
    if (GF_POWER==3)
        gf_poly = primitive_poly_3;
    if (GF_POWER==4)
        gf_poly = primitive_poly_4;
    if (GF_POWER==5)
        gf_poly = primitive_poly_5;
    if (GF_POWER==6)
        gf_poly = primitive_poly_6;
    if (GF_POWER==7)
        gf_poly = primitive_poly_7;
    if (GF_POWER==8)
        gf_poly = primitive_poly_8;

    int nw      =  1 << GF_POWER;
    int nwml    = (1 << GF_POWER) - 1;

    for (j=0; j<nw; j++) {
        galois_log_table[j] = nwml;
        galois_ilog_table[j] = 0;
    }

    b = 1;
    for (j=0; j<nwml; j++) {
        if (galois_log_table[b] != nwml) {
            fprintf(stderr, "Galois_create_log_tables Error: j=%d, b=%d, B->J[b]=%d, J->B[j]=%d (0%o)\n", j, b, galois_log_table[b], galois_ilog_table[j], (b << 1) ^ gf_poly);
            exit(1);
        }
        galois_log_table[b] = j;
        galois_ilog_table[j] = b;
        b = b << 1;
        if (b & nw)
            b = (b ^ gf_poly) & nwml;
    }

    return 0;
}

static int galois_create_mult_table()
{
    int j, x, y, logx;
    int nw = (1<<GF_POWER);

    // create tables
    if (galois_create_log_table() < 0) {
        fprintf(stderr, "create log/ilog tables failed\n");
        return -1;
    }

    /* Set mult/div tables for x = 0 */
    j = 0;
    galois_mult_table[j] = 0;   /* y = 0 */
    galois_divi_table[j] = -1;
    j++;
    for (y=1; y<nw; y++) {   /* y > 0 */
        galois_mult_table[j] = 0;
        galois_divi_table[j] = 0;
        j++;
    }

    for (x=1; x<nw; x++) {  /* x > 0 */
        galois_mult_table[j] = 0; /* y = 0 */
        galois_divi_table[j] = -1;
        j++;
        logx = galois_log_table[x];

        for (y=1; y<nw; y++) {  /* y > 0 */
            int tmp;
            tmp = logx + galois_log_table[y];
            if (tmp >= ((1<<GF_POWER) - 1))
                tmp -= ((1<<GF_POWER) - 1);             // avoid cross the boundary of log/ilog tables
            galois_mult_table[j] = galois_ilog_table[tmp];

            tmp = logx - galois_log_table[y];
            while (tmp < 0)
                tmp += ((1<<GF_POWER) - 1);
            galois_divi_table[j] = galois_ilog_table[tmp];

            j++;
        }
    }

    return 0;
}

// add operation over GF(2^m)
inline GF_ELEMENT galois_add(GF_ELEMENT a, GF_ELEMENT b)
{
    return a ^ b;
}

inline GF_ELEMENT galois_sub(GF_ELEMENT a, GF_ELEMENT b)
{
    return a ^ b;
}

inline GF_ELEMENT galois_multiply(GF_ELEMENT a, GF_ELEMENT b)
{
    if (a ==0 || b== 0)
        return 0;

    if (a == 1)
        return b;
    else if (b == 1)
        return a;

    GF_ELEMENT result = galois_mult_table[(a<<GF_POWER) | b];
    return result;
}

// return a/b
inline GF_ELEMENT galois_divide(GF_ELEMENT a, GF_ELEMENT b)
{
    if (b == 0) {
        fprintf(stderr, "ERROR! Divide by ZERO!\n");
        return -1;
    }

    if (a == 0)
        return 0;

    if (b == 1)
        return a;

    GF_ELEMENT result =  galois_divi_table[(a<<GF_POWER) | b];
    return result;
}

/*
 * When SSE is enabled, use SSE instructions to do multiply_add_region
 */
void galois_multiply_add_region(uint8_t *dst, uint8_t *src, uint8_t multiplier, int bytes)
{
    if (multiplier == 0) {
        // add nothing to bytes starting from *dst, just return
        return;
    }
    int i;
#if defined(INTEL_SSSE3)
    // if gf_power is 1 or 8, use SIMD to accelerate table look-up
    if (GF_POWER == 1 || GF_POWER == 8) {
        uint8_t *sptr, *dptr, *top;
        sptr = src;
        dptr = dst;
        top  = src + bytes;

        uint8_t *bh, *bl;
        __m128i mth, mtl, loset;
#if defined(INTEL_AVX2)
        __m256i mth2, mtl2, loset2;
#endif  // ifdef(INTEL_AVX2)
        if (multiplier != 1) {
            /* half tables only needed for multiplier != 1 */
            bh = (uint8_t*) galois_half_mult_table_high;
            bh += (multiplier << 4);
            bl = (uint8_t*) galois_half_mult_table_low;
            bl += (multiplier << 4);
            // read split tables as 128-bit values
            mth = _mm_loadu_si128((__m128i *)(bh));
            mtl = _mm_loadu_si128((__m128i *)(bl));
#if defined(INTEL_AVX2)
            mtl2 = _mm256_broadcastsi128_si256 (mtl);
            mth2 = _mm256_broadcastsi128_si256 (mth);
            loset2 = _mm256_set1_epi8 (0x0f);
#else
            loset = _mm_set1_epi8(0x0f);
#endif  // ifdef(INTEL_AVX2)
        }

        __m128i va, vb, r, t1, r2;
        while (sptr < top)
        {
#if defined(INTEL_AVX2)
            if (sptr + 32 > top) {
                /* remaining data doesn't fit into __m256i, do not use AVX2 */
                for (i=0; i<top-sptr; i++) {
                    if (multiplier == 1)
                        *(dptr+i) ^= *(sptr+i);
                    else
                        *(dptr+i) ^= galois_mult_table[((*(sptr+i))<<GF_POWER) | multiplier];
                }
                break;
            }
#else
            if (sptr + 16 > top) {
                /* remaining data doesn't fit into __m128i, do not use SSE */
                for (i=0; i<top-sptr; i++) {
                    if (multiplier == 1)
                        *(dptr+i) ^= *(sptr+i);
                    else
                        *(dptr+i) ^= galois_mult_table[((*(sptr+i))<<GF_POWER) | multiplier];
                }
                break;
            }
#endif  // ifdef(INTEL_AVX2)
#if defined(INTEL_AVX2)
            __m256i vaa, vbb, rr, tt1, rr2;
            vaa = _mm256_loadu_si256 ((__m256i *)(sptr));
            if (multiplier == 1) {
                vbb = _mm256_loadu_si256 ((__m256i *)(dptr));
                vbb = _mm256_xor_si256(vaa, vbb);
                _mm256_storeu_si256 ((__m256i *)(dptr), vbb);
            } else {
                // use half tables
                tt1 = _mm256_and_si256 (loset2, vaa);
                rr  = _mm256_shuffle_epi8 (mtl2, tt1);
                vaa = _mm256_srli_epi64 (vaa, 4);
                tt1 = _mm256_and_si256 (loset2, vaa);
                rr2 = _mm256_shuffle_epi8 (mth2, tt1);
                rr  = _mm256_xor_si256 (rr, rr2);
                vaa = _mm256_loadu_si256 ((__m256i *)(dptr));
                rr  = _mm256_xor_si256 (rr, vaa);
                _mm256_storeu_si256 ((__m256i *)(dptr), rr);
            }
            dptr += 32;
            sptr += 32;
#else
            va = _mm_loadu_si128 ((__m128i *)(sptr));
            if (multiplier == 1) {
                /* just XOR */
                vb = _mm_loadu_si128 ((__m128i *)(dptr));
                vb = _mm_xor_si128(va, vb);
                _mm_storeu_si128 ((__m128i *)(dptr), vb);
            } else {
                /* use half tables */
                t1 = _mm_and_si128 (loset, va);    // obtain lower 4-bit of the 16 src elements
                r  = _mm_shuffle_epi8 (mtl, t1);    // obtain products of the lower 4-bit 
                va = _mm_srli_epi64 (va, 4);       // shift the bits of the 16 src elements to right
                t1 = _mm_and_si128 (loset, va);    // obtain higher 4-bit of the src elements
                r2 = _mm_shuffle_epi8 (mth, t1);   // obtain products of the higher 4-bit
                r  = _mm_xor_si128 (r, r2);         // obtain final result of src * multiplier
                //r = _mm_xor_si128 (r, _mm_shuffle_epi8 (mth, t1));
                va = _mm_loadu_si128 ((__m128i *)(dptr));
                r = _mm_xor_si128 (r, va);
                _mm_storeu_si128 ((__m128i *)(dptr), r);
            }
            dptr += 16;
            sptr += 16;
#endif  // ifdef(INTEL_AVX2)
        }
        return;
    } else {
            // SSE is supported, but the Galois field power is not 1 or 8. SIMD is not supported for other GF sizes right now.
        if (multiplier == 1) {
            for (i=0; i<bytes; i++)
                dst[i] ^= src[i];
            return;
        }

        for (i = 0; i < bytes; i++)
            dst[i] ^= galois_mult_table[(src[i]<<GF_POWER) | multiplier];
        return;
    }
#else  // if not defined INTEL_SSSE3
    if (multiplier == 1) {
        for (i=0; i<bytes; i++)
            dst[i] ^= src[i];
        return;
    }

    for (i = 0; i < bytes; i++)
        dst[i] ^= galois_mult_table[(src[i]<<GF_POWER) | multiplier];
    return;
#endif  // ifdef(INTEL_SSSE3)
}

/*
 * Muliply a region of elements with multiplier. When SSE is available, use it
 */
void galois_multiply_region(uint8_t *src, uint8_t multiplier, int bytes)
{
    if (multiplier == 0) {
        memset(src, 0, sizeof(uint8_t)*bytes);
        return;
    } else if (multiplier == 1) {
        return;
    }
#if defined(INTEL_SSSE3)
    if (GF_POWER == 1 || GF_POWER == 8) {
        uint8_t *sptr, *top;
        sptr = src;
        top  = src + bytes;

        uint8_t *bh, *bl;
        /* half tables only needed for multiplier != 1 */
        bh = (uint8_t*) galois_half_mult_table_high;
        bh += (multiplier << 4);
        bl = (uint8_t*) galois_half_mult_table_low;
        bl += (multiplier << 4);
        // read split tables as 128-bit values
        __m128i mth = _mm_loadu_si128((__m128i *)(bh));
        __m128i mtl = _mm_loadu_si128((__m128i *)(bl));
        __m128i loset = _mm_set1_epi8(0x0f);
#if defined(INTEL_AVX2)
        __m256i mtl2 = _mm256_broadcastsi128_si256 (mtl);
        __m256i mth2 = _mm256_broadcastsi128_si256 (mth);
        __m256i loset2 = _mm256_set1_epi8 (0x0f);
        __m256i vaa, rr, tt1, rr2;
#endif

        __m128i va, r, t1;
        while (sptr < top)
        {
#if defined(INTEL_AVX2)
            if (sptr + 32 > top) {
                /* remaining data doesn't fit into __m128i, do not use SSE */
                for (int i=0; i<top-sptr; i++)
                    *(sptr+i) = galois_mult_table[((*(sptr+i))<<GF_POWER) | multiplier];
                break;
            }
            vaa = _mm256_loadu_si256 ((__m256i *)(sptr));
            tt1 = _mm256_and_si256 (loset2, vaa);
            rr  = _mm256_shuffle_epi8 (mtl2, tt1);
            vaa = _mm256_srli_epi64 (vaa, 4);
            tt1 = _mm256_and_si256 (loset2, vaa);
            rr2 = _mm256_shuffle_epi8 (mth2, tt1);
            rr  = _mm256_xor_si256 (rr, rr2);
            _mm256_storeu_si256 ((__m256i *)(sptr), rr);
            sptr += 32;
#else
            if (sptr + 16 > top) {
                /* remaining data doesn't fit into __m128i, do not use SSE */
                for (int i=0; i<top-sptr; i++)
                    *(sptr+i) = galois_mult_table[((*(sptr+i))<<GF_POWER) | multiplier];
                break;
            }
            va = _mm_loadu_si128 ((__m128i *)(sptr));
            t1 = _mm_and_si128 (loset, va);
            r = _mm_shuffle_epi8 (mtl, t1);
            va = _mm_srli_epi64 (va, 4);
            t1 = _mm_and_si128 (loset, va);
            r = _mm_xor_si128 (r, _mm_shuffle_epi8 (mth, t1));
            _mm_storeu_si128 ((__m128i *)(sptr), r);
            sptr += 16;
#endif
        }
        return;
    } else {
        for (int i=0; i<bytes; i++)
            src[i] = galois_mult_table[((src[i])<<GF_POWER) | multiplier];
        return;
    }
#else
    for (int i=0; i<bytes; i++)
        src[i] = galois_mult_table[((src[i])<<GF_POWER) | multiplier];
    return;
#endif
}

/*
void galois_multiply_add_region(GF_ELEMENT *dst, GF_ELEMENT *src, GF_ELEMENT multiplier, int bytes)
{
    if (multiplier == 0) {
        // add nothing to bytes starting from *dst, just return
        return;
    }
    int i;
    if (multiplier == 1) {
        for (i=0; i<bytes; i++)
            dst[i] ^= src[i];
        return;
    }

    for (i = 0; i < bytes; i++)
        dst[i] ^= galois_mult_table[(src[i]<<GF_POWER) | multiplier];
    return;
}

void galois_multiply_region(GF_ELEMENT *src, GF_ELEMENT multiplier, int bytes)
{
    if (multiplier == 0) {
        memset(src, 0, sizeof(GF_ELEMENT)*bytes);
        return;
    } else if (multiplier == 1) {
        return;
    }
    for (int i=0; i<bytes; i++)
        src[i] = galois_mult_table[((src[i])<<GF_POWER) | multiplier];
    return;
}
*/
