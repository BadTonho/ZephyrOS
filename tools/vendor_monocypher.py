#!/usr/bin/env python3
"""Extrai o subconjunto Ed25519/SHA-512 do Monocypher 4.0.3."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


EXPECTED_VERSION = "// Monocypher version 4.0.3"
EXPECTED_MAIN_SHA256 = "57eb914fc88136119bd41655cccb8c250048bf54d470540625186f8ab16f64be"
EXPECTED_OPTIONAL_SHA256 = "60fce3578fb00b00da96490653d993c4cb427b1e1be38183285c66e04d22cc18"

UTILITIES = r'''
/////////////////
/// Utilities ///
/////////////////
#define FOR_T(type, i, start, end) for (type i = (start); i < (end); i++)
#define FOR(i, start, end)         FOR_T(size_t, i, start, end)
#define COPY(dst, src, size)       FOR(_i_, 0, size) (dst)[_i_] = (src)[_i_]
#define ZERO(buf, size)            FOR(_i_, 0, size) (buf)[_i_] = 0
#define WIPE_CTX(ctx)              crypto_wipe(ctx   , sizeof(*(ctx)))
#define WIPE_BUFFER(buffer)        crypto_wipe(buffer, sizeof(buffer))
#define MIN(a, b)                  ((a) <= (b) ? (a) : (b))
#define MAX(a, b)                  ((a) >= (b) ? (a) : (b))

typedef int8_t   i8;
typedef uint8_t  u8;
typedef int16_t  i16;
typedef uint32_t u32;
typedef int32_t  i32;
typedef int64_t  i64;
typedef uint64_t u64;

static u32 load24_le(const u8 s[3])
{
    return
        ((u32)s[0] <<  0) |
        ((u32)s[1] <<  8) |
        ((u32)s[2] << 16);
}

static u32 load32_le(const u8 s[4])
{
    return
        ((u32)s[0] <<  0) |
        ((u32)s[1] <<  8) |
        ((u32)s[2] << 16) |
        ((u32)s[3] << 24);
}

static u64 load64_le(const u8 s[8])
{
    return load32_le(s) | ((u64)load32_le(s + 4) << 32);
}

static void store32_le(u8 out[4], u32 in)
{
    out[0] = (u8)(in      );
    out[1] = (u8)(in >>  8);
    out[2] = (u8)(in >> 16);
    out[3] = (u8)(in >> 24);
}

static void load32_le_buf(u32 *dst, const u8 *src, size_t size)
{
    FOR(i, 0, size) {
        dst[i] = load32_le(src + i * 4);
    }
}

static void store32_le_buf(u8 *dst, const u32 *src, size_t size)
{
    FOR(i, 0, size) {
        store32_le(dst + i * 4, src[i]);
    }
}

static int neq0(u64 diff)
{
    u64 half = (diff >> 32) | ((u32)diff);
    u64 eq0 = 1 & ((half - 1) >> 32);
    return (int)eq0 - 1;
}

static u64 x16(const u8 a[16], const u8 b[16])
{
    return (load64_le(a) ^ load64_le(b))
        | (load64_le(a + 8) ^ load64_le(b + 8));
}

static u64 x32(const u8 a[32], const u8 b[32])
{
    return x16(a, b) | x16(a + 16, b + 16);
}

static int crypto_verify32(const u8 a[32], const u8 b[32])
{
    return neq0(x32(a, b));
}

static void crypto_wipe(void *secret, size_t size)
{
    volatile u8 *volatile_secret = (u8*)secret;
    ZERO(volatile_secret, size);
}
'''

WRAPPER = r'''

int crypto_sha512_digest(const uint8_t* data, uint32_t size,
                         uint8_t hash[CRYPTO_SHA512_SIZE])
{
    crypto_sha512_ctx_t ctx;

    if (!hash || (!data && size > 0U)) {
        LOG_ERROR("CRYPTO", "Argumento nulo no SHA-512");
        return ERR_NULL;
    }
    crypto_sha512_init(&ctx);
    crypto_sha512_update(&ctx, data, size);
    crypto_sha512_final(&ctx, hash);
    return OK;
}

static int ed25519_point_is_canonical(const uint8_t encoded[32])
{
    uint8_t input_y[32];
    uint8_t canonical_y[32];
    ge point;
    fe zero;
    int valid;

    kmemcpy(input_y, encoded, sizeof(input_y));
    if (ge_frombytes_neg_vartime(&point, encoded) != 0) {
        crypto_wipe(input_y, sizeof(input_y));
        return 0;
    }
    ge_tobytes(canonical_y, &point);
    input_y[31] &= 0x7FU;
    canonical_y[31] &= 0x7FU;
    valid = crypto_verify32(input_y, canonical_y) == 0;
    fe_0(zero);
    if (fe_isequal(point.X, zero) && (encoded[31] & 0x80U) != 0U) {
        valid = 0;
    }
    crypto_wipe(input_y, sizeof(input_y));
    crypto_wipe(canonical_y, sizeof(canonical_y));
    crypto_wipe(&point, sizeof(point));
    crypto_wipe(zero, sizeof(zero));
    return valid;
}

int crypto_ed25519_verify_init(
    crypto_ed25519_verify_ctx_t* ctx,
    const uint8_t signature[CRYPTO_ED25519_SIGNATURE_SIZE],
    const uint8_t public_key[CRYPTO_ED25519_PUBLIC_KEY_SIZE])
{
    if (!ctx || !signature || !public_key) {
        LOG_ERROR("CRYPTO", "Argumento nulo ao iniciar Ed25519");
        return ERR_NULL;
    }
    kmemcpy(ctx->signature, signature, CRYPTO_ED25519_SIGNATURE_SIZE);
    kmemcpy(ctx->public_key, public_key, CRYPTO_ED25519_PUBLIC_KEY_SIZE);
    crypto_sha512_init(&ctx->hash);
    crypto_sha512_update(&ctx->hash, signature, 32U);
    crypto_sha512_update(&ctx->hash, public_key, 32U);
    ctx->active = 1;
    return OK;
}

int crypto_ed25519_verify_update(crypto_ed25519_verify_ctx_t* ctx,
                                 const uint8_t* data, uint32_t size)
{
    if (!ctx || (!data && size > 0U)) {
        LOG_ERROR("CRYPTO", "Argumento nulo ao atualizar Ed25519");
        return ERR_NULL;
    }
    if (!ctx->active) {
        LOG_ERROR("CRYPTO", "Contexto Ed25519 inativo");
        return ERR_STATE;
    }
    crypto_sha512_update(&ctx->hash, data, size);
    return OK;
}

int crypto_ed25519_verify_final(crypto_ed25519_verify_ctx_t* ctx)
{
    uint8_t expanded[64];
    uint8_t reduced[32];
    int valid;

    if (!ctx) {
        LOG_ERROR("CRYPTO", "Contexto Ed25519 nulo ao finalizar");
        return ERR_NULL;
    }
    if (!ctx->active) {
        LOG_ERROR("CRYPTO", "Contexto Ed25519 finalizado duas vezes");
        return ERR_STATE;
    }
    crypto_sha512_final(&ctx->hash, expanded);
    crypto_eddsa_reduce(reduced, expanded);
    valid = ed25519_point_is_canonical(ctx->signature)
        && ed25519_point_is_canonical(ctx->public_key)
        && crypto_eddsa_check_equation(
            ctx->signature, ctx->public_key, reduced) == 0;
    crypto_wipe(expanded, sizeof(expanded));
    crypto_wipe(reduced, sizeof(reduced));
    ctx->active = 0;
    if (!valid) {
        LOG_ERROR("CRYPTO", "Assinatura Ed25519 invalida");
        return ERR_INVALID;
    }
    return OK;
}
'''


def lines(path: Path, expected_sha256: str) -> list[str]:
    """Le o fonte upstream e confirma a versao exata."""
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != expected_sha256:
        raise ValueError(f"hash upstream inesperado: {path}")
    content = raw.decode("utf-8").splitlines(keepends=True)
    if not content or content[0].rstrip() != EXPECTED_VERSION:
        raise ValueError(f"fonte inesperado: {path}")
    return content


def block(content: list[str], first: int, last: int) -> str:
    """Seleciona um intervalo inclusivo usando numeros de linha humanos."""
    return "".join(content[first - 1 : last])


def render(source_dir: Path) -> str:
    """Monta uma unidade freestanding contendo apenas verificacao."""
    main = lines(source_dir / "monocypher.c", EXPECTED_MAIN_SHA256)
    optional = lines(
        source_dir / "optional" / "monocypher-ed25519.c",
        EXPECTED_OPTIONAL_SHA256,
    )
    license_header = block(main, 1, 53)
    field = block(main, 924, 1514)
    scalar = block(main, 1596, 1603) + block(main, 1618, 1696)
    ed25519 = block(main, 1712, 2065)
    sha_utilities = block(optional, 72, 109)
    sha512 = block(optional, 110, 302)
    sha512 = sha512.replace("crypto_sha512_ctx", "crypto_sha512_ctx_t")
    return (
        license_header
        + "\n/* Subconjunto verify-only adaptado para o kernel ZephyrOS. */\n"
        + '#include "core/crypto.h"\n'
        + '#include "core/errors.h"\n'
        + '#include "core/log.h"\n'
        + '#include "core/string.h"\n\n'
        + UTILITIES
        + "\n"
        + field
        + "\n"
        + scalar
        + "\n"
        + ed25519
        + "\n"
        + sha_utilities
        + "\n"
        + sha512
        + WRAPPER
    )


def main() -> int:
    """Gera o arquivo rastreado a partir do release oficial conferido."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if args.output.exists():
        raise SystemExit(f"saida ja existe: {args.output}")
    args.output.write_bytes(render(args.source_dir).encode("utf-8"))
    print(f"Gerado {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
