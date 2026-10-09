#include "steam_content.h"
#include "steam_wire.h"
#include "steam_hash.h"
#include <lzma.h>
#include <mbedtls/aes.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha1.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <zstd.h>
static uint32_t le32(const unsigned char *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
         (uint32_t)p[3] << 24;
}
static unsigned int le16(const unsigned char *p) {
  return p[0] | (unsigned int)p[1] << 8;
}
static int portable_cbc(const unsigned char key[32], const unsigned char *p,
                        size_t size, unsigned char *out) {
  unsigned char iv[16];
  mbedtls_aes_context aes;
  int ok;
  mbedtls_aes_init(&aes);
  ok = !mbedtls_aes_setkey_dec(&aes, key, 256) &&
       !mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, p, iv) &&
       !mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, size - 16, iv, p + 16,
                              out);
  mbedtls_aes_free(&aes);
  steam_secret_clear(iv, sizeof(iv));
  return ok;
}
#ifdef __SWITCH__
static int hardware_cbc(const unsigned char key[32], const unsigned char *p,
                        size_t size, unsigned char *out) {
  unsigned char iv[16];
  Aes256Context block;
  Aes256CbcContext cbc;
  int ok;
  aes256ContextCreate(&block, key, false);
  aes256DecryptBlock(&block, iv, p);
  aes256CbcContextCreate(&cbc, key, iv, false);
  ok = aes256CbcDecrypt(&cbc, out, p + 16, size - 16) == size - 16;
  steam_secret_clear(&block, sizeof(block));
  steam_secret_clear(&cbc, sizeof(cbc));
  steam_secret_clear(iv, sizeof(iv));
  return ok;
}
/* The processor's AES and SHA instructions are trusted only after they give
 * mbedtls's answers for one fixed input. */
int steam_content_hardware(void) {
  static int known;
  if (!known) {
    unsigned char key[32], input[64], fast[48], slow[48], a[20], b[20];
    unsigned int i;
    Sha1Context sha;
    for (i = 0; i < 32; ++i)
      key[i] = (unsigned char)(i * 7 + 1);
    for (i = 0; i < 64; ++i)
      input[i] = (unsigned char)(i * 13 + 5);
    sha1ContextCreate(&sha);
    sha1ContextUpdate(&sha, input, 23);
    sha1ContextUpdate(&sha, input + 23, 41);
    sha1ContextGetHash(&sha, a);
    known = !mbedtls_sha1_ret(input, 64, b) && !memcmp(a, b, 20) &&
                    hardware_cbc(key, input, 64, fast) &&
                    portable_cbc(key, input, 64, slow) && !memcmp(fast, slow, 48)
                ? 1
                : -1;
  }
  return known > 0;
}
#endif
int steam_content_decrypt(const unsigned char key[32], const void *input,
                          size_t size, unsigned char *out, size_t cap,
                          size_t *used) {
  const unsigned char *p = input;
  size_t i, n;
  int ok = 0;
  *used = 0;
  if (size < 32 || size % 16 || size - 16 > cap)
    return 0;
#ifdef __SWITCH__
  if (steam_content_hardware() ? !hardware_cbc(key, p, size, out)
                               : !portable_cbc(key, p, size, out))
    goto done;
#else
  if (!portable_cbc(key, p, size, out))
    goto done;
#endif
  n = out[size - 17];
  if (!n || n > 16)
    goto done;
  for (i = 0; i < n; ++i)
    if (out[size - 17 - i] != n)
      goto done;
  *used = size - 16 - n;
  ok = 1;
done:
  if (!ok)
    steam_secret_clear(out, cap);
  return ok;
}
int steam_content_path(char *s) {
  char *p = s, *start = s;
  size_t n = strlen(s);
  if (!n || n >= 384 || s[0] == '/' || s[0] == '\\')
    return 0;
  for (;; ++p) {
    if (*p == '\\')
      *p = '/';
    if ((unsigned char)*p < 32 && *p)
      return 0;
    if (*p == ':' || *p == '*' || *p == '?' || *p == '"' || *p == '<' ||
        *p == '>' || *p == '|')
      return 0;
    if (*p == '/' || !*p) {
      size_t len = (size_t)(p - start);
      if (!len || len > 255 || (len == 1 && start[0] == '.') ||
          (len == 2 && start[0] == '.' && start[1] == '.') || p[-1] == '.' ||
          p[-1] == ' ')
        return 0;
      if (!*p)
        break;
      start = p + 1;
    }
  }
  return 1;
}
int steam_content_filename(const unsigned char key[32], const void *data,
                           size_t size, char out[384]) {
  unsigned char encrypted[768], plain[768];
  size_t n = 0, used = 0;
  int ok = 0;
  memset(out, 0, 384);
  if (mbedtls_base64_decode(encrypted, sizeof(encrypted), &n, data, size) ||
      !steam_content_decrypt(key, encrypted, n, plain, sizeof(plain), &used))
    goto done;
  if (used && plain[used - 1] == 0)
    --used;
  if (!used || used >= 384 || memchr(plain, 0, used))
    goto done;
  memcpy(out, plain, used);
  ok = steam_content_path(out);
done:
  steam_secret_clear(encrypted, sizeof(encrypted));
  steam_secret_clear(plain, sizeof(plain));
  if (!ok)
    memset(out, 0, 384);
  return ok;
}
static int unpack(const unsigned char *p, size_t n, unsigned char *out,
                  size_t expected, int verify_crc) {
  if (n >= 22 && !memcmp(p, "VZa", 3)) {
    uint32_t dict;
    lzma_ret status;
    if (p[n - 2] != 'z' || p[n - 1] != 'v' || le32(p + n - 6) != expected)
      return 0;
    dict = le32(p + 8);
    if (dict > 16u * 1024u * 1024u)
      return 0;
#ifdef LZMA_FILTER_LZMA1EXT
    {
      lzma_filter filters[2] = {{LZMA_FILTER_LZMA1EXT, NULL},
                                {LZMA_VLI_UNKNOWN, NULL}};
      lzma_options_lzma *options;
      size_t input = 0, output = 0;
      if (lzma_properties_decode(&filters[0], NULL, p + 7, 5) != LZMA_OK)
        return 0;
      options = filters[0].options;
      options->ext_flags = LZMA_LZMA1EXT_ALLOW_EOPM;
      lzma_set_ext_size(*options, expected);
      status = lzma_raw_buffer_decode(filters, NULL, p + 12, &input, n - 22,
                                      out, &output, expected);
      free(options);
      return status == LZMA_OK && input == n - 22 && output == expected &&
             (!verify_crc || (uint32_t)crc32(0, out, (uInt)expected) == le32(p + n - 10));
    }
#else
    lzma_stream stream = LZMA_STREAM_INIT;
    unsigned char header[13];
    unsigned int i;
    int ok;
    /* Known-size .lzma header permits streams without an end marker. */
    memcpy(header, p + 7, 5);
    for (i = 0; i < 8; ++i)
      header[5 + i] = (unsigned char)((uint64_t)expected >> (8 * i));
    if (lzma_alone_decoder(&stream, 32u * 1024u * 1024u) != LZMA_OK)
      return 0;
    stream.next_in = header;
    stream.avail_in = sizeof(header);
    stream.next_out = out;
    stream.avail_out = expected;
    status = lzma_code(&stream, LZMA_RUN);
    if (status != LZMA_OK || stream.avail_in) {
      lzma_end(&stream);
      return 0;
    }
    stream.next_in = p + 12;
    stream.avail_in = n - 22;
    status = lzma_code(&stream, LZMA_FINISH);
    ok = status == LZMA_STREAM_END && stream.total_out == expected &&
         !stream.avail_in;
    lzma_end(&stream);
    if (!ok) {
      lzma_filter filters[2] = {{LZMA_FILTER_LZMA1, NULL},
                                {LZMA_VLI_UNKNOWN, NULL}};
      size_t input = 0, output = 0;
      if (lzma_properties_decode(&filters[0], NULL, p + 7, 5) != LZMA_OK)
        return 0;
      status = lzma_raw_buffer_decode(filters, NULL, p + 12, &input, n - 22,
                                      out, &output, expected);
      free(filters[0].options);
      ok = status == LZMA_OK && input == n - 22 && output == expected;
    }
    return ok && (!verify_crc || (uint32_t)crc32(0, out, (uInt)expected) == le32(p + n - 10));
#endif
  }
  if (n >= 23 && !memcmp(p, "VSZa", 4)) {
    ZSTD_DCtx *ctx;
    size_t written;
    int ok;
    if (memcmp(p + n - 3, "zsv", 3) || le32(p + n - 11) != expected ||
        (verify_crc && le32(p + 4) != le32(p + n - 15)))
      return 0;
    ctx = ZSTD_createDCtx();
    if (!ctx)
      return 0;
    if (ZSTD_isError(ZSTD_DCtx_setParameter(ctx, ZSTD_d_windowLogMax, 24))) {
      ZSTD_freeDCtx(ctx);
      return 0;
    }
    written = ZSTD_decompressDCtx(ctx, out, expected, p + 8, n - 23);
    ZSTD_freeDCtx(ctx);
    ok = !ZSTD_isError(written) && written == expected;
    return ok && (!verify_crc || (uint32_t)crc32(0, out, (uInt)expected) == le32(p + 4));
  }
  if (n >= 30 && le32(p) == 0x04034b50) {
    size_t offset = 30u + le16(p + 26) + le16(p + 28);
    unsigned int method = le16(p + 8);
    z_stream z = {0};
    int status, ok;
    if (offset > n || (le16(p + 6) & 1))
      return 0;
    if (method == 0) {
      if (n - offset < expected)
        return 0;
      memcpy(out, p + offset, expected);
      return 1;
    }
    if (method != 8 || inflateInit2(&z, -15) != Z_OK)
      return 0;
    z.next_in = (unsigned char *)p + offset;
    z.avail_in = (uInt)(n - offset);
    z.next_out = out;
    z.avail_out = (uInt)expected;
    status = inflate(&z, Z_FINISH);
    ok = status == Z_STREAM_END && z.total_out == expected;
    inflateEnd(&z);
    return ok;
  }
  return 0;
}
int steam_content_chunk(const unsigned char key[32], const void *input,
                        size_t size, unsigned char *out, size_t expected,
                        uint32_t checksum, const unsigned char sha[20]) {
  return !steam_content_chunk_check(key,input,size,out,expected,checksum,sha,NULL);
}
unsigned int steam_content_chunk_check(const unsigned char key[32], const void *input,
                        size_t size, unsigned char *out, size_t expected,
                        uint32_t checksum, const unsigned char sha[20],unsigned int *format) {
  unsigned char *plain, digest[20];
  size_t n = 0;
  unsigned int reason = STEAM_INTEGRITY_NONE;
  if(format)*format=0;
  if (!size || size > STEAM_CHUNK_LIMIT + 64 || !expected ||
      expected > STEAM_CHUNK_LIMIT)
    return STEAM_INTEGRITY_SIZE;
  plain = malloc(size);
  if (!plain)
    return STEAM_INTEGRITY_MEMORY;
  if(!steam_content_decrypt(key,input,size,plain,size,&n))reason=STEAM_INTEGRITY_DECRYPT;
  else {
    if(format){
      if(n>=4&&!memcmp(plain,"VSZa",4))*format=3;
      else if(n>=3&&!memcmp(plain,"VZa",3))*format=2;
      else if(n>=4&&le32(plain)==0x04034b50)*format=1;
    }
    /* Like SteamKit DepotChunk.Process: container CRC fields are not the
     * authoritative depot checksum. ALWAYS require manifest Adler32 and SHA-1. */
    if(!unpack(plain,n,out,expected,0))reason=STEAM_INTEGRITY_COMPRESSION;
    else if((uint32_t)adler32(0,out,(uInt)expected)!=checksum)reason=STEAM_INTEGRITY_CHECKSUM;
    else{steam_sha1(out,expected,digest);if(memcmp(digest,sha,20))reason=STEAM_INTEGRITY_CHUNK_SHA;}
  }
  steam_secret_clear(plain, size);
  free(plain);
  steam_secret_clear(digest, sizeof(digest));
  if (reason)
    steam_secret_clear(out, expected);
  return reason;
}
