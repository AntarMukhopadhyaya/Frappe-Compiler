//
// Created by antar on 25/10/26.
//
// The Windows LLVM installer ships LLVMSupport.lib with references to
// zlib's `crc32` symbol but does not bundle zlib.lib. CMake's
// find_package(LLVM) advertises ZLIB::ZLIB as a transitive dependency,
// which we satisfy with an empty INTERFACE imported target — that
// silences configure but leaves the symbol unresolved at link time.
//
// This provides a minimal, correct, self-contained CRC32 matching
// zlib's public signature:
//
//     unsigned long crc32(unsigned long crc, const unsigned char* buf,
//                         unsigned int len);
//
// Standard reflected polynomial 0xEDB88320, same as zlib, so LLVM's
// CRC.cpp / JamCRC callers get correct results. On non-Windows builds
// this file compiles to an empty object — the real zlib is linked
// through LLVM's own transitive dependency there.
//

#ifdef _WIN32

#include <stdint.h>

unsigned long crc32(unsigned long crc, const unsigned char *buf,
                    unsigned int len) {
    // Reflected polynomial table for 0xEDB88320. Built lazily on first
    // call. Not thread-safe, but LLVM's CRC use at link time is
    // single-threaded in the compiler's own process.
    static uint32_t table[256];
    static int initialized = 0;
    if (!initialized) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) {
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        initialized = 1;
    }

    uint32_t c = (uint32_t)crc ^ 0xFFFFFFFFu;
    for (unsigned int i = 0; i < len; i++) {
        c = table[(c ^ buf[i]) & 0xFFu] ^ (c >> 8);
    }
    return (unsigned long)(c ^ 0xFFFFFFFFu);
}

#endif // _WIN32


#include <stddef.h>

/* =========================================================================
 * zlib compression stubs
 *
 * LLVMSupport.lib (Compression.cpp) references these symbols, but the
 * Windows installer doesn't ship zlib.lib. Frappe never calls LLVM's
 * compression API, so these are only needed to satisfy the linker.
 * Return error codes so any hypothetical call fails visibly.
 * ========================================================================= */

unsigned long compressBound(unsigned long sourceLen) {
    (void)sourceLen;
    return 0;   /* caller can't use this; would abort on real compress */
}

int compress2(unsigned char *dest, unsigned long *destLen,
              const unsigned char *source, unsigned long sourceLen,
              int level) {
    (void)dest; (void)destLen; (void)source; (void)sourceLen; (void)level;
    return -1;   /* Z_BUF_ERROR equivalent */
}

int uncompress(unsigned char *dest, unsigned long *destLen,
               const unsigned char *source, unsigned long sourceLen) {
    (void)dest; (void)destLen; (void)source; (void)sourceLen;
    return -1;
}

/* =========================================================================
 * zstd compression stubs
 *
 * Same story. LLVMSupport.lib references zstd for bitcode compression.
 * Frappe doesn't use bitcode, so these are linker-only.
 * ========================================================================= */

typedef struct ZSTD_CCtx_s ZSTD_CCtx;

ZSTD_CCtx *ZSTD_createCCtx(void) {
    return NULL;
}

size_t ZSTD_CCtx_setParameter(ZSTD_CCtx *cctx, int param, int value) {
    (void)cctx; (void)param; (void)value;
    return (size_t)-1;
}

unsigned ZSTD_isError(size_t code) {
    return code == (size_t)-1 ? 1 : 0;
}

size_t ZSTD_compressBound(size_t srcSize) {
    (void)srcSize;
    return 0;
}

size_t ZSTD_compress2(ZSTD_CCtx *cctx, void *dst, size_t dstCapacity,
                      const void *src, size_t srcSize) {
    (void)cctx; (void)dst; (void)dstCapacity; (void)src; (void)srcSize;
    return (size_t)-1;
}

size_t ZSTD_freeCCtx(ZSTD_CCtx *cctx) {
    (void)cctx;
    return 0;
}

size_t ZSTD_decompress(void *dst, size_t dstCapacity,
                       const void *src, size_t compressedSize) {
    (void)dst; (void)dstCapacity; (void)src; (void)compressedSize;
    return (size_t)-1;
}

const char *ZSTD_getErrorName(size_t code) {
    (void)code;
    return "compression unsupported (frappe stub)";
}