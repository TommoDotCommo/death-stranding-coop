/* SHA-256 (FIPS 180-4), portable C99, no platform calls. The one-file Setup checks its embedded payload with it and the
 * launcher's install core (launcher/install.cpp) hashes files with it, so both build and test natively on Linux.
 * dist/test_dist.py checks it against the standard test vectors. */
#ifndef COOP_SHA256_H
#define COOP_SHA256_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct sha256_ctx { uint32_t h[8]; uint64_t bytes; unsigned char block[64]; size_t used; };

static inline uint32_t sha256_rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }

static inline void sha256_compress(uint32_t h[8], const unsigned char *p)
{
    static const uint32_t k[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u };
    uint32_t w[64], a, b, c, d, e, f, g, hh;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[4 * i] << 24) | ((uint32_t)p[4 * i + 1] << 16) | ((uint32_t)p[4 * i + 2] << 8) | p[4 * i + 3];
    for (i = 16; i < 64; i++) {
        uint32_t s0 = sha256_rotr(w[i - 15], 7) ^ sha256_rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = sha256_rotr(w[i - 2], 17) ^ sha256_rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4]; f = h[5]; g = h[6]; hh = h[7];
    for (i = 0; i < 64; i++) {
        uint32_t t1 = hh + (sha256_rotr(e, 6) ^ sha256_rotr(e, 11) ^ sha256_rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
        uint32_t t2 = (sha256_rotr(a, 2) ^ sha256_rotr(a, 13) ^ sha256_rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

static inline void sha256_init(struct sha256_ctx *c)
{
    static const uint32_t iv[8] = { 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
    memcpy(c->h, iv, sizeof(iv));
    c->bytes = 0;
    c->used = 0;
}

static inline void sha256_update(struct sha256_ctx *c, const void *data, size_t len)
{
    const unsigned char *p = (const unsigned char *)data;
    c->bytes += len;
    while (len) {
        size_t take = 64 - c->used;
        if (take > len) take = len;
        memcpy(c->block + c->used, p, take);
        c->used += take; p += take; len -= take;
        if (c->used == 64) { sha256_compress(c->h, c->block); c->used = 0; }
    }
}

/* Lower-case hex, 64 characters and a terminating zero. */
static inline void sha256_final_hex(struct sha256_ctx *c, char out[65])
{
    uint64_t bits = c->bytes * 8;
    unsigned char pad = 0x80, zero = 0, len[8];
    int i;
    sha256_update(c, &pad, 1);
    while (c->used != 56) sha256_update(c, &zero, 1);
    for (i = 0; i < 8; i++) len[i] = (unsigned char)(bits >> (56 - 8 * i));
    sha256_update(c, len, 8);
    for (i = 0; i < 32; i++) {
        unsigned char b = (unsigned char)(c->h[i / 4] >> (24 - 8 * (i % 4)));
        out[2 * i] = "0123456789abcdef"[b >> 4];
        out[2 * i + 1] = "0123456789abcdef"[b & 15];
    }
    out[64] = 0;
}

static inline void sha256_hex(const void *data, size_t len, char out[65])
{
    struct sha256_ctx c;
    sha256_init(&c);
    sha256_update(&c, data, len);
    sha256_final_hex(&c, out);
}
#endif
