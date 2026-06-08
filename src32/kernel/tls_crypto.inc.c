/* Tiny TLS helpers for LeonOS's small HTTPS smoke path.
 * This is intentionally narrow: SHA-256/HMAC/HKDF, X25519, and
 * ChaCha20-Poly1305 for TLS 1.3 TLS_CHACHA20_POLY1305_SHA256.
 */

struct Sha256Ctx {
    u32 state[8];
    u8 block[64];
    u32 block_len;
    u64 total_len;
};

#define TLS_AEAD_MAX_CIPHER 18448u
#define TLS_POLY_MSG_MAX 18512u

static u8 tls_poly_msg[TLS_POLY_MSG_MAX];

static u32 rot_r32(u32 x, u32 n)
{
    return (x >> n) | (x << (32u - n));
}

static u32 load_le32(const u8 *p)
{
    return ((u32) p[0]) | ((u32) p[1] << 8) |
           ((u32) p[2] << 16) | ((u32) p[3] << 24);
}

static void store_le32(u8 *p, u32 v)
{
    p[0] = (u8) v;
    p[1] = (u8) (v >> 8);
    p[2] = (u8) (v >> 16);
    p[3] = (u8) (v >> 24);
}

static const u32 sha256_k[64] = {
    0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u,
    0x3956C25Bu, 0x59F111F1u, 0x923F82A4u, 0xAB1C5ED5u,
    0xD807AA98u, 0x12835B01u, 0x243185BEu, 0x550C7DC3u,
    0x72BE5D74u, 0x80DEB1FEu, 0x9BDC06A7u, 0xC19BF174u,
    0xE49B69C1u, 0xEFBE4786u, 0x0FC19DC6u, 0x240CA1CCu,
    0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu,
    0x983E5152u, 0xA831C66Du, 0xB00327C8u, 0xBF597FC7u,
    0xC6E00BF3u, 0xD5A79147u, 0x06CA6351u, 0x14292967u,
    0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu, 0x53380D13u,
    0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u,
    0xA2BFE8A1u, 0xA81A664Bu, 0xC24B8B70u, 0xC76C51A3u,
    0xD192E819u, 0xD6990624u, 0xF40E3585u, 0x106AA070u,
    0x19A4C116u, 0x1E376C08u, 0x2748774Cu, 0x34B0BCB5u,
    0x391C0CB3u, 0x4ED8AA4Au, 0x5B9CCA4Fu, 0x682E6FF3u,
    0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u,
    0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u
};

static void sha256_transform(struct Sha256Ctx *ctx, const u8 block[64])
{
    u32 w[64];
    for (u32 i = 0; i < 16u; i += 1u) {
        w[i] = read_be32(block + i * 4u);
    }
    for (u32 i = 16u; i < 64u; i += 1u) {
        u32 s0 = rot_r32(w[i - 15u], 7u) ^ rot_r32(w[i - 15u], 18u) ^ (w[i - 15u] >> 3);
        u32 s1 = rot_r32(w[i - 2u], 17u) ^ rot_r32(w[i - 2u], 19u) ^ (w[i - 2u] >> 10);
        w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
    }

    u32 a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
    u32 e = ctx->state[4], f = ctx->state[5], g = ctx->state[6], h = ctx->state[7];
    for (u32 i = 0; i < 64u; i += 1u) {
        u32 s1 = rot_r32(e, 6u) ^ rot_r32(e, 11u) ^ rot_r32(e, 25u);
        u32 ch = (e & f) ^ ((~e) & g);
        u32 t1 = h + s1 + ch + sha256_k[i] + w[i];
        u32 s0 = rot_r32(a, 2u) ^ rot_r32(a, 13u) ^ rot_r32(a, 22u);
        u32 maj = (a & b) ^ (a & c) ^ (b & c);
        u32 t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_init(struct Sha256Ctx *ctx)
{
    ctx->state[0] = 0x6A09E667u; ctx->state[1] = 0xBB67AE85u;
    ctx->state[2] = 0x3C6EF372u; ctx->state[3] = 0xA54FF53Au;
    ctx->state[4] = 0x510E527Fu; ctx->state[5] = 0x9B05688Cu;
    ctx->state[6] = 0x1F83D9ABu; ctx->state[7] = 0x5BE0CD19u;
    ctx->block_len = 0u;
    ctx->total_len = 0u;
}

static void sha256_update(struct Sha256Ctx *ctx, const u8 *data, u32 len)
{
    ctx->total_len += (u64) len;
    while (len != 0u) {
        u32 take = 64u - ctx->block_len;
        if (take > len) {
            take = len;
        }
        mem_copy(ctx->block + ctx->block_len, data, take);
        ctx->block_len += take;
        data += take;
        len -= take;
        if (ctx->block_len == 64u) {
            sha256_transform(ctx, ctx->block);
            ctx->block_len = 0u;
        }
    }
}

static void sha256_final(struct Sha256Ctx *ctx, u8 out[32])
{
    u64 bits = ctx->total_len * 8u;
    ctx->block[ctx->block_len++] = 0x80u;
    if (ctx->block_len > 56u) {
        while (ctx->block_len < 64u) {
            ctx->block[ctx->block_len++] = 0u;
        }
        sha256_transform(ctx, ctx->block);
        ctx->block_len = 0u;
    }
    while (ctx->block_len < 56u) {
        ctx->block[ctx->block_len++] = 0u;
    }
    for (u32 i = 0; i < 8u; i += 1u) {
        ctx->block[56u + i] = (u8) (bits >> (56u - i * 8u));
    }
    sha256_transform(ctx, ctx->block);
    for (u32 i = 0; i < 8u; i += 1u) {
        write_be32_value(out + i * 4u, ctx->state[i]);
    }
}

static void sha256_hash(const u8 *data, u32 len, u8 out[32])
{
    struct Sha256Ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, out);
}

static void hmac_sha256(const u8 *key, u32 key_len, const u8 *data, u32 len,
                        u8 out[32])
{
    u8 k0[64];
    u8 inner[32];
    struct Sha256Ctx ctx;
    mem_zero(k0, sizeof(k0));
    if (key_len > 64u) {
        sha256_hash(key, key_len, k0);
        key_len = 32u;
    } else if (key_len != 0u) {
        mem_copy(k0, key, key_len);
    }

    for (u32 i = 0; i < 64u; i += 1u) {
        k0[i] ^= 0x36u;
    }
    sha256_init(&ctx);
    sha256_update(&ctx, k0, 64u);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, inner);

    for (u32 i = 0; i < 64u; i += 1u) {
        k0[i] ^= (0x36u ^ 0x5Cu);
    }
    sha256_init(&ctx);
    sha256_update(&ctx, k0, 64u);
    sha256_update(&ctx, inner, 32u);
    sha256_final(&ctx, out);
}

static void hkdf_extract(const u8 *salt, u32 salt_len, const u8 *ikm,
                         u32 ikm_len, u8 out[32])
{
    u8 zero[32];
    if (salt == 0 || salt_len == 0u) {
        mem_zero(zero, sizeof(zero));
        hmac_sha256(zero, 32u, ikm, ikm_len, out);
    } else {
        hmac_sha256(salt, salt_len, ikm, ikm_len, out);
    }
}

static void hkdf_expand(const u8 prk[32], const u8 *info, u32 info_len,
                        u8 *out, u32 out_len)
{
    u8 t[32];
    u8 msg[96];
    u32 done = 0u;
    u32 t_len = 0u;
    u8 counter = 1u;
    while (done < out_len) {
        mem_copy(msg, t, t_len);
        mem_copy(msg + t_len, info, info_len);
        msg[t_len + info_len] = counter;
        hmac_sha256(prk, 32u, msg, t_len + info_len + 1u, t);
        t_len = 32u;
        u32 take = out_len - done;
        if (take > 32u) {
            take = 32u;
        }
        mem_copy(out + done, t, take);
        done += take;
        counter += 1u;
    }
}

static void tls_hkdf_expand_label(const u8 secret[32], const char *label,
                                  const u8 *ctx, u32 ctx_len,
                                  u8 *out, u32 out_len)
{
    u8 info[96];
    const char prefix[] = "tls13 ";
    u32 p = 0u;
    u32 label_len = 0u;
    while (label[label_len] != 0) {
        label_len += 1u;
    }
    write_be16(info + p, (u16) out_len); p += 2u;
    info[p++] = (u8) (6u + label_len);
    mem_copy(info + p, prefix, 6u); p += 6u;
    mem_copy(info + p, label, label_len); p += label_len;
    info[p++] = (u8) ctx_len;
    if (ctx_len != 0u) {
        mem_copy(info + p, ctx, ctx_len);
        p += ctx_len;
    }
    hkdf_expand(secret, info, p, out, out_len);
}

static void tls_derive_secret(const u8 secret[32], const char *label,
                              const u8 transcript_hash[32], u8 out[32])
{
    tls_hkdf_expand_label(secret, label, transcript_hash, 32u, out, 32u);
}

typedef i64 gf[16];

static const gf x25519_121665 = {
    0xDB41, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static void x25519_car(gf o)
{
    for (u32 i = 0; i < 16u; i += 1u) {
        o[i] += (i64) 1 << 16;
        i64 c = o[i] >> 16;
        o[(i + 1u) & 15u] += c - 1 + 37 * (c - 1) * (i == 15u);
        o[i] -= c << 16;
    }
}

static void x25519_sel(gf p, gf q, i32 b)
{
    i64 c = ~(b - 1);
    for (u32 i = 0; i < 16u; i += 1u) {
        i64 t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

static void x25519_unpack(gf o, const u8 n[32])
{
    for (u32 i = 0; i < 16u; i += 1u) {
        o[i] = (i64) n[2u * i] + ((i64) n[2u * i + 1u] << 8);
    }
    o[15] &= 0x7FFF;
}

static void x25519_add(gf o, const gf a, const gf b)
{
    for (u32 i = 0; i < 16u; i += 1u) {
        o[i] = a[i] + b[i];
    }
}

static void x25519_sub(gf o, const gf a, const gf b)
{
    for (u32 i = 0; i < 16u; i += 1u) {
        o[i] = a[i] - b[i];
    }
}

static void x25519_mul(gf o, const gf a, const gf b)
{
    i64 t[31];
    for (u32 i = 0; i < 31u; i += 1u) {
        t[i] = 0;
    }
    for (u32 i = 0; i < 16u; i += 1u) {
        for (u32 j = 0; j < 16u; j += 1u) {
            t[i + j] += a[i] * b[j];
        }
    }
    for (u32 i = 0; i < 15u; i += 1u) {
        t[i] += 38 * t[i + 16u];
    }
    for (u32 i = 0; i < 16u; i += 1u) {
        o[i] = t[i];
    }
    x25519_car(o);
    x25519_car(o);
}

static void x25519_square(gf o, const gf a)
{
    x25519_mul(o, a, a);
}

static void x25519_inv(gf o, const gf i)
{
    gf c;
    for (u32 a = 0; a < 16u; a += 1u) {
        c[a] = i[a];
    }
    for (i32 a = 253; a >= 0; a -= 1) {
        x25519_square(c, c);
        if (a != 2 && a != 4) {
            x25519_mul(c, c, i);
        }
    }
    for (u32 a = 0; a < 16u; a += 1u) {
        o[a] = c[a];
    }
}

static void x25519_pack(u8 o[32], const gf n)
{
    gf m, t;
    for (u32 i = 0; i < 16u; i += 1u) {
        t[i] = n[i];
    }
    x25519_car(t);
    x25519_car(t);
    x25519_car(t);
    for (u32 j = 0; j < 2u; j += 1u) {
        m[0] = t[0] - 0xFFED;
        for (u32 i = 1u; i < 15u; i += 1u) {
            m[i] = t[i] - 0xFFFF - ((m[i - 1u] >> 16) & 1);
            m[i - 1u] &= 0xFFFF;
        }
        m[15] = t[15] - 0x7FFF - ((m[14] >> 16) & 1);
        i64 b = (m[15] >> 16) & 1;
        m[14] &= 0xFFFF;
        x25519_sel(t, m, (i32) (1 - b));
    }
    for (u32 i = 0; i < 16u; i += 1u) {
        o[2u * i] = (u8) t[i];
        o[2u * i + 1u] = (u8) (t[i] >> 8);
    }
}

static void x25519_scalarmult(u8 q[32], const u8 n_in[32], const u8 p[32])
{
    u8 z[32];
    gf x, a, b, c, d, e, f;
    mem_copy(z, n_in, 32u);
    z[0] &= 248u;
    z[31] &= 127u;
    z[31] |= 64u;
    x25519_unpack(x, p);
    for (u32 i = 0; i < 16u; i += 1u) {
        b[i] = x[i];
        d[i] = 0;
        a[i] = c[i] = 0;
    }
    a[0] = d[0] = 1;
    for (i32 i = 254; i >= 0; i -= 1) {
        i32 r = (i32) ((z[i >> 3] >> (i & 7)) & 1);
        x25519_sel(a, b, r);
        x25519_sel(c, d, r);
        x25519_add(e, a, c);
        x25519_sub(a, a, c);
        x25519_add(c, b, d);
        x25519_sub(b, b, d);
        x25519_square(d, e);
        x25519_square(f, a);
        x25519_mul(a, c, a);
        x25519_mul(c, b, e);
        x25519_add(e, a, c);
        x25519_sub(a, a, c);
        x25519_square(b, a);
        x25519_sub(c, d, f);
        x25519_mul(a, c, x25519_121665);
        x25519_add(a, a, d);
        x25519_mul(c, c, a);
        x25519_mul(a, d, f);
        x25519_mul(d, b, x);
        x25519_square(b, e);
        x25519_sel(a, b, r);
        x25519_sel(c, d, r);
    }
    x25519_inv(c, c);
    x25519_mul(a, a, c);
    x25519_pack(q, a);
}

static void chacha20_block(const u8 key[32], const u8 nonce[12],
                           u32 counter, u8 out[64])
{
    u32 x[16];
    x[0] = 0x61707865u; x[1] = 0x3320646Eu; x[2] = 0x79622D32u; x[3] = 0x6B206574u;
    for (u32 i = 0; i < 8u; i += 1u) {
        x[4u + i] = load_le32(key + i * 4u);
    }
    x[12] = counter;
    x[13] = load_le32(nonce + 0u);
    x[14] = load_le32(nonce + 4u);
    x[15] = load_le32(nonce + 8u);
    u32 w[16];
    for (u32 i = 0; i < 16u; i += 1u) {
        w[i] = x[i];
    }
#define QR(a,b,c,d) do { \
    w[a] += w[b]; w[d] ^= w[a]; w[d] = (w[d] << 16) | (w[d] >> 16); \
    w[c] += w[d]; w[b] ^= w[c]; w[b] = (w[b] << 12) | (w[b] >> 20); \
    w[a] += w[b]; w[d] ^= w[a]; w[d] = (w[d] << 8) | (w[d] >> 24); \
    w[c] += w[d]; w[b] ^= w[c]; w[b] = (w[b] << 7) | (w[b] >> 25); \
} while (0)
    for (u32 i = 0; i < 10u; i += 1u) {
        QR(0, 4, 8, 12); QR(1, 5, 9, 13); QR(2, 6, 10, 14); QR(3, 7, 11, 15);
        QR(0, 5, 10, 15); QR(1, 6, 11, 12); QR(2, 7, 8, 13); QR(3, 4, 9, 14);
    }
#undef QR
    for (u32 i = 0; i < 16u; i += 1u) {
        store_le32(out + i * 4u, w[i] + x[i]);
    }
}

static void chacha20_xor(const u8 key[32], const u8 nonce[12], u32 counter,
                         u8 *data, u32 len)
{
    u8 block[64];
    u32 off = 0u;
    while (off < len) {
        chacha20_block(key, nonce, counter, block);
        counter += 1u;
        u32 take = len - off;
        if (take > 64u) {
            take = 64u;
        }
        for (u32 i = 0; i < take; i += 1u) {
            data[off + i] ^= block[i];
        }
        off += take;
    }
}

static void poly1305_auth(u8 out[16], const u8 *m, u32 bytes, const u8 key[32])
{
    u32 r0 = load_le32(key + 0u) & 0x3FFFFFFu;
    u32 r1 = (load_le32(key + 3u) >> 2) & 0x3FFFF03u;
    u32 r2 = (load_le32(key + 6u) >> 4) & 0x3FFC0FFu;
    u32 r3 = (load_le32(key + 9u) >> 6) & 0x3F03FFFu;
    u32 r4 = (load_le32(key + 12u) >> 8) & 0x00FFFFFu;
    u32 s1 = r1 * 5u, s2 = r2 * 5u, s3 = r3 * 5u, s4 = r4 * 5u;
    u32 h0 = 0u, h1 = 0u, h2 = 0u, h3 = 0u, h4 = 0u;

    while (bytes != 0u) {
        u8 block[16];
        u32 hibit = 1u << 24;
        u32 take = bytes > 16u ? 16u : bytes;
        mem_zero(block, sizeof(block));
        mem_copy(block, m, take);
        if (take < 16u) {
            block[take] = 1u;
            hibit = 0u;
        }

        u32 t0 = load_le32(block + 0u);
        u32 t1 = load_le32(block + 4u);
        u32 t2 = load_le32(block + 8u);
        u32 t3 = load_le32(block + 12u);

        h0 += t0 & 0x3FFFFFFu;
        h1 += ((t0 >> 26) | (t1 << 6)) & 0x3FFFFFFu;
        h2 += ((t1 >> 20) | (t2 << 12)) & 0x3FFFFFFu;
        h3 += ((t2 >> 14) | (t3 << 18)) & 0x3FFFFFFu;
        h4 += ((t3 >> 8) & 0x00FFFFFFu) | hibit;

        u64 d0 = (u64) h0 * r0 + (u64) h1 * s4 + (u64) h2 * s3 + (u64) h3 * s2 + (u64) h4 * s1;
        u64 d1 = (u64) h0 * r1 + (u64) h1 * r0 + (u64) h2 * s4 + (u64) h3 * s3 + (u64) h4 * s2;
        u64 d2 = (u64) h0 * r2 + (u64) h1 * r1 + (u64) h2 * r0 + (u64) h3 * s4 + (u64) h4 * s3;
        u64 d3 = (u64) h0 * r3 + (u64) h1 * r2 + (u64) h2 * r1 + (u64) h3 * r0 + (u64) h4 * s4;
        u64 d4 = (u64) h0 * r4 + (u64) h1 * r3 + (u64) h2 * r2 + (u64) h3 * r1 + (u64) h4 * r0;

        u32 c = (u32) (d0 >> 26); h0 = (u32) d0 & 0x3FFFFFFu; d1 += c;
        c = (u32) (d1 >> 26); h1 = (u32) d1 & 0x3FFFFFFu; d2 += c;
        c = (u32) (d2 >> 26); h2 = (u32) d2 & 0x3FFFFFFu; d3 += c;
        c = (u32) (d3 >> 26); h3 = (u32) d3 & 0x3FFFFFFu; d4 += c;
        c = (u32) (d4 >> 26); h4 = (u32) d4 & 0x3FFFFFFu; h0 += c * 5u;
        c = h0 >> 26; h0 &= 0x3FFFFFFu; h1 += c;

        m += take;
        bytes -= take;
    }

    u32 c = h1 >> 26; h1 &= 0x3FFFFFFu; h2 += c;
    c = h2 >> 26; h2 &= 0x3FFFFFFu; h3 += c;
    c = h3 >> 26; h3 &= 0x3FFFFFFu; h4 += c;
    c = h4 >> 26; h4 &= 0x3FFFFFFu; h0 += c * 5u;
    c = h0 >> 26; h0 &= 0x3FFFFFFu; h1 += c;

    u32 g0 = h0 + 5u;
    c = g0 >> 26; g0 &= 0x3FFFFFFu;
    u32 g1 = h1 + c; c = g1 >> 26; g1 &= 0x3FFFFFFu;
    u32 g2 = h2 + c; c = g2 >> 26; g2 &= 0x3FFFFFFu;
    u32 g3 = h3 + c; c = g3 >> 26; g3 &= 0x3FFFFFFu;
    u32 g4 = h4 + c - (1u << 26);
    u32 mask = (g4 >> 31) - 1u;
    u32 nmask = ~mask;
    h0 = (h0 & nmask) | (g0 & mask);
    h1 = (h1 & nmask) | (g1 & mask);
    h2 = (h2 & nmask) | (g2 & mask);
    h3 = (h3 & nmask) | (g3 & mask);
    h4 = (h4 & nmask) | (g4 & mask);

    h0 = (h0 | (h1 << 26)) + load_le32(key + 16u);
    h1 = ((h1 >> 6) | (h2 << 20)) + load_le32(key + 20u) + (h0 < load_le32(key + 16u));
    h2 = ((h2 >> 12) | (h3 << 14)) + load_le32(key + 24u) + (h1 < load_le32(key + 20u));
    h3 = ((h3 >> 18) | (h4 << 8)) + load_le32(key + 28u) + (h2 < load_le32(key + 24u));

    store_le32(out + 0u, h0);
    store_le32(out + 4u, h1);
    store_le32(out + 8u, h2);
    store_le32(out + 12u, h3);
}

static void poly1305_msg_auth(u8 tag[16], const u8 poly_key[32],
                              const u8 *aad, u32 aad_len,
                              const u8 *cipher, u32 cipher_len)
{
    u8 *msg = tls_poly_msg;
    u32 p = 0u;
    mem_copy(msg + p, aad, aad_len); p += aad_len;
    while ((p & 15u) != 0u) {
        msg[p++] = 0u;
    }
    mem_copy(msg + p, cipher, cipher_len); p += cipher_len;
    while ((p & 15u) != 0u) {
        msg[p++] = 0u;
    }
    for (u32 i = 0; i < 8u; i += 1u) {
        msg[p++] = (u8) ((u64) aad_len >> (8u * i));
    }
    for (u32 i = 0; i < 8u; i += 1u) {
        msg[p++] = (u8) ((u64) cipher_len >> (8u * i));
    }
    poly1305_auth(tag, msg, p, poly_key);
}

static void tls_nonce(u8 out[12], const u8 iv[12], u64 seq)
{
    mem_copy(out, iv, 12u);
    for (u32 i = 0; i < 8u; i += 1u) {
        out[11u - i] ^= (u8) (seq >> (i * 8u));
    }
}

static u8 chacha20_poly1305_decrypt(const u8 key[32], const u8 iv[12],
                                    u64 seq, const u8 aad[5],
                                    u8 *cipher, u32 cipher_len)
{
    if (cipher_len < 16u || cipher_len > TLS_AEAD_MAX_CIPHER) {
        return 0u;
    }
    u8 nonce[12];
    u8 block0[64];
    u8 tag[16];
    u32 text_len = cipher_len - 16u;
    tls_nonce(nonce, iv, seq);
    chacha20_block(key, nonce, 0u, block0);
    poly1305_msg_auth(tag, block0, aad, 5u, cipher, text_len);
    if (!mem_equal(tag, cipher + text_len, 16u)) {
        return 0u;
    }
    chacha20_xor(key, nonce, 1u, cipher, text_len);
    return 1u;
}

static u32 chacha20_poly1305_encrypt(const u8 key[32], const u8 iv[12],
                                     u64 seq, u8 aad[5],
                                     u8 *plain, u32 plain_len)
{
    u8 nonce[12];
    u8 block0[64];
    u8 tag[16];
    tls_nonce(nonce, iv, seq);
    u32 cipher_len = plain_len + 16u;
    write_be16(aad + 3u, (u16) cipher_len);
    chacha20_xor(key, nonce, 1u, plain, plain_len);
    chacha20_block(key, nonce, 0u, block0);
    poly1305_msg_auth(tag, block0, aad, 5u, plain, plain_len);
    mem_copy(plain + plain_len, tag, 16u);
    return cipher_len;
}
