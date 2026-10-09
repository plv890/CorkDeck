#define _POSIX_C_SOURCE 200809L
#include "../source/steam/steam_app.h"
#include "../source/steam/steam_content.h"
#include "../source/steam/steam_library.h"
#include "../source/steam/steam_manifest.h"
#include <assert.h>
#include <lzma.h>
#include <mbedtls/aes.h>
#include <mbedtls/sha1.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>
#include <zstd.h>
static void put32(unsigned char *p, uint32_t x) {
  unsigned int i;
  for (i = 0; i < 4; ++i)
    p[i] = (unsigned char)(x >> (8 * i));
}
static void owned(void) {
  unsigned char b[512], g[256];
  struct steam_wire_writer w = {b, sizeof(b), 0, 0}, s = {g, sizeof(g), 0, 0};
  struct steam_library l = {0};
  struct steam_cm_message m = {.type = 147, .result = 1, .body = b};
  struct steam_game page[1];
  size_t total;
  steam_wire_uint(&w, 1, 1);
  steam_wire_uint(&s, 1, 123);
  steam_wire_bytes(&s, 2, "Synthetic Game", 14);
  steam_wire_bytes(&w, 2, g, s.size);
  m.body_size = w.size;
  assert(steam_library_read(&m, 99, &l) == STEAM_LIBRARY_OK && l.count == 1 &&
         l.account == 99);
  assert(steam_library_page(&l, "game", 0, page, 1, &total) == 1 &&
         total == 1 && page[0].app_id == 123);
  m.body_size--;
  assert(steam_library_read(&m, 99, &l) == STEAM_LIBRARY_PROTOCOL &&
         l.count == 1);
  m.body_size = w.size;
  steam_wire_bytes(&w, 2, g, s.size);
  m.body_size = w.size;
  assert(steam_library_read(&m, 99, &l) == STEAM_LIBRARY_PROTOCOL &&
         l.count == 1);
  steam_library_clear(&l);
}
static void appinfo(void) {
  const char *kv =
      "\"appinfo\" { \"common\" { \"name\" \"Synthetic\" \"oslist\" "
      "\"windows,linux\" } \"depots\" { \"101\" { \"config\" { \"oslist\" "
      "\"windows\" } \"manifests\" { \"public\" { \"gid\" \"999\" \"size\" "
      "\"12\" } } } \"102\" { \"config\" { \"oslist\" \"linux\" } "
      "\"manifests\" { \"public\" { \"gid\" \"998\" } } } } }";
  struct steam_app a;
  assert(steam_app_parse(kv, strlen(kv), 100, &a));
  assert(a.count == 1 && a.depots[0].id == 101 && a.depots[0].manifest == 999 &&
         a.total_size == 12 && a.windows == 1);
  assert(!steam_app_parse(kv, strlen(kv) - 1, 100, &a));
  {
    /* Fallout 3's layout: the game, an optional regional copy of it, and an
     * optional regional copy of an add-on that has no dlcappid of its own. */
    const char *optional =
        "\"appinfo\" { \"depots\" { \"22301\" { \"manifests\" { \"public\" "
        "{ \"gid\" \"11\" \"size\" \"7\" } } } \"22305\" { \"optional\" "
        "\"1\" \"manifests\" { \"public\" { \"gid\" \"12\" \"size\" \"5\" } "
        "} } \"22405\" { \"manifests\" { \"public\" { \"gid\" \"13\" "
        "\"size\" \"3\" } } \"optional\" \"1\" } } }";
    assert(steam_app_parse(optional, strlen(optional), 22300, &a));
    assert(a.count == 1 && a.depots[0].id == 22301 && a.total_size == 7);
  }
}
static void content(void) {
  unsigned char key[32] = {0}, iv[16] = {0}, plain[256] = {0}, encrypted[288],
                out[64], sha[20];
  const unsigned char source[] = "synthetic verified chunk";
  size_t n, pad, compressed;
  uint32_t crc;
  mbedtls_aes_context aes;
  char good[] = "data\\file.exe", bad[] = "../escape.exe";
  assert(steam_content_path(good) && !strcmp(good, "data/file.exe"));
  assert(!steam_content_path(bad));
  memcpy(plain, "VSZa", 4);
  /* A container CRC is not a manifest checksum. SteamKit's chunk decoder
   * ignores it; the valid Adler32 and SHA-1 below must still be required. */
  crc = 0;
  put32(plain + 4, crc);
  compressed = ZSTD_compress(plain + 8, 200, source, sizeof(source), 1);
  assert(!ZSTD_isError(compressed));
  n = 8 + compressed;
  put32(plain + n, crc);
  put32(plain + n + 4, sizeof(source));
  memset(plain + n + 8, 0, 4);
  memcpy(plain + n + 12, "zsv", 3);
  n += 15;
  pad = 16 - n % 16;
  memset(plain + n, (int)pad, pad);
  n += pad;
  mbedtls_aes_init(&aes);
  assert(!mbedtls_aes_setkey_enc(&aes, key, 256));
  assert(!mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, iv, encrypted));
  assert(!mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, n, iv, plain,
                                encrypted + 16));
  mbedtls_aes_free(&aes);
  assert(!mbedtls_sha1_ret(source, sizeof(source), sha));
  assert(steam_content_chunk(key, encrypted, n + 16, out, sizeof(source),
                             (uint32_t)adler32(0, source, sizeof(source)),
                             sha));
  assert(!memcmp(out, source, sizeof(source)));
  {unsigned int format=0;unsigned char wrong[20];memcpy(wrong,sha,20);wrong[0]^=1;
   assert(steam_content_chunk_check(key,encrypted,n+16,out,sizeof(source),1,sha,&format)==STEAM_INTEGRITY_CHECKSUM&&format==3);
   assert(steam_content_chunk_check(key,encrypted,n+16,out,sizeof(source),(uint32_t)adler32(0,source,sizeof(source)),wrong,&format)==STEAM_INTEGRITY_CHUNK_SHA);
   assert(!memcmp(out,(unsigned char[sizeof(source)]){0},sizeof(source)));}
  encrypted[n + 15] ^= 1;
  assert(!steam_content_chunk(key, encrypted, n + 16, out, sizeof(source),
                              (uint32_t)adler32(0, source, sizeof(source)),
                              sha));
}
static void section(FILE *f, uint32_t magic, const unsigned char *data,
                    size_t n) {
  unsigned char header[8];
  put32(header, magic);
  put32(header + 4, (uint32_t)n);
  assert(fwrite(header, 1, 8, f) == 8);
  assert(fwrite(data, 1, n, f) == n);
}
static void lzma_content(void) {
  const unsigned char source[] = "LZMA payload fixture";
  unsigned char alone[256], plain[256] = {0}, encrypted[288], out[64],
                            key[32] = {0}, iv[16] = {0}, sha[20];
  lzma_options_lzma options;
  lzma_stream stream = LZMA_STREAM_INIT;
  size_t n, pad;
  mbedtls_aes_context aes;
  assert(!lzma_lzma_preset(&options, 1));
  assert(lzma_alone_encoder(&stream, &options) == LZMA_OK);
  stream.next_in = source;
  stream.avail_in = sizeof(source);
  stream.next_out = alone;
  stream.avail_out = sizeof(alone);
  assert(lzma_code(&stream, LZMA_FINISH) == LZMA_STREAM_END);
  n = (size_t)stream.total_out;
  lzma_end(&stream);
  memcpy(plain, "VZa", 3);
  memcpy(plain + 7, alone, 5);
  memcpy(plain + 12, alone + 13, n - 13);
  n = 12 + n - 13;
  put32(plain + n, 0); /* Outer manifest integrity is authoritative. */
  put32(plain + n + 4, sizeof(source));
  memcpy(plain + n + 8, "zv", 2);
  n += 10;
  pad = 16 - n % 16;
  memset(plain + n, (int)pad, pad);
  n += pad;
  mbedtls_aes_init(&aes);
  assert(!mbedtls_aes_setkey_enc(&aes, key, 256));
  assert(!mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, iv, encrypted));
  assert(!mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, n, iv, plain,
                                encrypted + 16));
  mbedtls_aes_free(&aes);
  assert(!mbedtls_sha1_ret(source, sizeof(source), sha));
  assert(steam_content_chunk(key, encrypted, n + 16, out, sizeof(source),
                             (uint32_t)adler32(0, source, sizeof(source)),
                             sha));
  assert(!memcmp(source, out, sizeof(source)));
}
static void manifest(void) {
  char path[] = "/tmp/autorun-manifest-XXXXXX";
  int fd = mkstemp(path);
  FILE *f;
  unsigned char file[128], payload[160], metadata[64], sha[20] = {0}, end[4],
                                                       key[32] = {0};
  struct steam_wire_writer w = {file, sizeof(file), 0, 0},
                           p = {payload, sizeof(payload), 0, 0},
                           m = {metadata, sizeof(metadata), 0, 0};
  struct steam_manifest manifest;
  struct steam_manifest_file mapping;
  assert(fd >= 0);
  f = fdopen(fd, "wb");
  assert(f);
  steam_wire_bytes(&w, 1, "safe.exe", 8);
  steam_wire_uint(&w, 2, 0);
  steam_wire_bytes(&w, 5, sha, 20);
  steam_wire_bytes(&p, 1, file, w.size);
  steam_wire_uint(&m, 1, 101);
  steam_wire_uint(&m, 2, 999);
  steam_wire_uint(&m, 4, 0);
  steam_wire_uint(&m, 5, 0);
  section(f, 0x71f617d0, payload, p.size);
  section(f, 0x1f4812be, metadata, m.size);
  section(f, 0x1b81b817, sha, 20);
  put32(end, 0x32c415ab);
  assert(fwrite(end, 1, 4, f) == 4);
  assert(!fclose(f));
  assert(steam_manifest_open(&manifest, path, 101, 999, key));
  assert(steam_manifest_next(&manifest, &mapping) == 1 &&
         !strcmp(mapping.path, "safe.exe"));
  assert(steam_manifest_next(&manifest, &mapping) == 0);
  steam_manifest_close(&manifest);
  assert(!steam_manifest_open(&manifest, path, 101, 998, key));
  unlink(path);
}
int main(void) {
  owned();
  appinfo();
  content();
  lzma_content();
  manifest();
  puts("Owned library, Windows depot, encrypted chunk and streaming manifest "
       "fixtures passed");
  return 0;
}
