/* Offline transaction tests: synthetic licensed metadata, in-memory HTTPS
 * fixture. No Steam credentials, network, or production transport overrides. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <curl/curl.h>
#include <stdarg.h>
#include <sys/statvfs.h>
static int low_disk;
static int corrupt_responses,corrupt_forever,short_responses;
static int language_missing, unlicensed, unlicensed_game, shared_game;
static int (*real_disk)(const char *,struct statvfs *)=statvfs;
static int fixture_disk(const char *,struct statvfs *);
#define statvfs(a,b) fixture_disk(a,b)
#undef curl_easy_setopt
#undef curl_easy_getinfo
#define curl_easy_setopt fixture_setopt
#define curl_easy_perform fixture_perform
#define curl_easy_getinfo fixture_getinfo
#define curl_easy_init fixture_init
#define curl_easy_cleanup fixture_cleanup
#define curl_easy_reset fixture_reset
static CURL *fixture_init(void);
static CURLcode fixture_setopt(CURL *, CURLoption, ...);
static CURLcode fixture_perform(CURL *);
static CURLcode fixture_getinfo(CURL *, CURLINFO, ...);
static void fixture_cleanup(CURL *);
static void fixture_reset(CURL *c) { (void)c; }
#include "../source/steam/steam_install.c"
#include <mbedtls/aes.h>
#include <zlib.h>
#include <zstd.h>
static unsigned char encrypted[2][256];
static size_t encrypted_size[2];
static unsigned char data[128];
static const size_t length = sizeof(data);
static unsigned char key[32];
static char hashtext[2][41];
static int cancel_at_commit, explicit_discard, cancel_after_first, deny, requests, handles,cleanups,auth_required,auth_requests;
static long status;
static struct steam_download_view result;
static struct transfer *active;
static char url[2048];
static int fixture_disk(const char *p,struct statvfs *d){if(!low_disk)return real_disk(p,d);memset(d,0,sizeof(*d));d->f_frsize=1;d->f_bavail=1;return 0;}
static CURL *fixture_init(void) { ++handles;return (CURL *)&requests; }
static CURLcode fixture_setopt(CURL *c, CURLoption option, ...) {
  va_list ap;
  (void)c;
  va_start(ap, option);
  if (option == CURLOPT_WRITEDATA)
    active = va_arg(ap, struct transfer *);
  else if (option == CURLOPT_URL)
    snprintf(url, sizeof(url), "%s", va_arg(ap, const char *));
  va_end(ap);
  return CURLE_OK;
}
static CURLcode fixture_perform(CURL *c) {
  size_t i;
  (void)c;
  ++requests;
  status=auth_required&&!strstr(url,"fixture=test")?403:200;
  if(status==403)return CURLE_OK;
  for (i = 0; i < 2; ++i)
    if (strstr(url, hashtext[i])) {
      unsigned char bytes[256];size_t n=encrypted_size[i];memcpy(bytes,encrypted[i],n);
      if(corrupt_forever || corrupt_responses){bytes[17]^=1;if(corrupt_responses)--corrupt_responses;}
      if(short_responses){--n;--short_responses;}
      return write_data((char *)bytes,1,n,active)==n?CURLE_OK:CURLE_WRITE_ERROR;
    }
  assert(!"Unexpected fixture URL");
  return CURLE_HTTP_RETURNED_ERROR;
}
static CURLcode fixture_getinfo(CURL *c, CURLINFO info, ...) {
  va_list ap;
  (void)c;
  va_start(ap, info);
  if (info == CURLINFO_STARTTRANSFER_TIME || info == CURLINFO_TOTAL_TIME)
    *va_arg(ap, double *) = info == CURLINFO_STARTTRANSFER_TIME ? 0.01 : 0.02;
  else {
    assert(info == CURLINFO_RESPONSE_CODE || info == CURLINFO_NUM_CONNECTS);
    *va_arg(ap, long *) = info==CURLINFO_RESPONSE_CODE?status:0;
  }
  va_end(ap);
  return CURLE_OK;
}
static void fixture_cleanup(CURL *c) { (void)c;++cleanups; }
unsigned int steam_cm_error(const struct steam_cm *c) {
  (void)c;
  return 0;
}
int steam_backend_app(struct steam_backend *b, uint32_t app,
                      struct steam_app *out) {
  (void)b;
  assert(app == 123);
  *out = (struct steam_app){.id = app, .windows = 1, .count = 1};
  if(language_missing){out->language_missing=1;out->count=0;out->available_languages=2;return 1;}
  strcpy(out->name,"Fixture: game");
  out->depots[0] = (struct steam_depot){.id = 124, .source_app = app, .manifest = 999};
  /* All of a game's files may be another application's; a small shared
   * depot beside them does not make a refused game this account's. */
  if(shared_game){out->depots[0].source_app=77;out->depots[0].size=1000;out->count=2;out->depots[1]=(struct steam_depot){.id=126,.source_app=228980,.manifest=6,.size=10};}
  if(unlicensed){out->count=2;out->english_available=1;out->depots[1]=(struct steam_depot){.id=125,.source_app=app,.manifest=5,.language=1,.language_specific=1};}
  return 1;
}
int steam_backend_depot(struct steam_backend *b, uint32_t app,
                        const struct steam_depot *d, unsigned char out[32],
                        uint64_t *code) {
  (void)app;
  if (unlicensed_game || d->id == 125 || d->id == 126 || (shared_game == 2 && d->id == 124)) {
    b->error = 1015;
    return 0;
  }
  if (deny) {
    b->error = 1009;
    return 0;
  }
  memcpy(out, key, 32);
  *code = 1;
  return 1;
}
int steam_backend_servers(struct steam_backend *b, char hosts[8][128],
                          size_t *n) {
  (void)b;
  strcpy(hosts[0], "fixture.steamcontent.com");
  *n = 1;
  return 1;
}
int steam_backend_cdn_auth(struct steam_backend *b, uint32_t app,
                           uint32_t depot, const char *host, char token[1024]) {
  (void)b;
  (void)app;
  (void)depot;
  (void)host;
  ++auth_requests;
  if(auth_required){strcpy(token,"?fixture=test");return 1;}return 0;
}
static int fixture_tick(void *c) {
  (void)c;
  return !explicit_discard && !(cancel_after_first && requests>=2);
}
static int fixture_discard(void *c){(void)c;return explicit_discard || (cancel_after_first && requests>=2);}
static void fixture_progress(void *c, const struct steam_download_view *v) {
  (void)c;
  result = *v;
  if(cancel_at_commit&&v->state==STEAM_DOWNLOAD_COMMITTING)explicit_discard=1;
}
static void put32(unsigned char *p, uint32_t v) {
  unsigned i;
  for (i = 0; i < 4; ++i)
    p[i] = (unsigned char)(v >> (8 * i));
}
static void section(FILE *f, uint32_t magic, const unsigned char *p, size_t n) {
  unsigned char h[8];
  put32(h, magic);
  put32(h + 4, (uint32_t)n);
  assert(fwrite(h, 1, 8, f) == 8 && fwrite(p, 1, n, f) == n);
}
static void fixtures(const char *root) {
  unsigned char chunk[128], mapping[1024], payload[1200], metadata[128],
      sha[20], end[4], packed[200], iv[16];
  struct steam_wire_writer map = {mapping, sizeof(mapping), 0, 0},
                           pay = {payload, sizeof(payload), 0, 0},
                           meta = {metadata, sizeof(metadata), 0, 0};
  char folder[1024], manifest[1024];
  FILE *f;
  size_t i, j;
  for (i = 0; i < length; ++i)
    data[i] = (unsigned char)(i * 3);
  steam_wire_bytes(&map, 1, "game.exe", 8);
  steam_wire_uint(&map, 2, length);
  assert(!mbedtls_sha1_ret(data, length, sha));
  steam_wire_bytes(&map, 5, sha, 20);
  for (i = 0; i < 2; ++i) {
    size_t n, z, pad;
    uint32_t crc = (uint32_t)crc32(0, data + i * 64, 64);
    mbedtls_aes_context aes;
    struct steam_wire_writer w = {chunk, sizeof(chunk), 0, 0};
    memcpy(packed, "VSZa", 4);
    put32(packed + 4, crc);
    z = ZSTD_compress(packed + 8, 128, data + i * 64, 64, 1);
    assert(!ZSTD_isError(z));
    n = 8 + z;
    put32(packed + n, crc);
    put32(packed + n + 4, 64);
    memset(packed + n + 8, 0, 4);
    memcpy(packed + n + 12, "zsv", 3);
    n += 15;
    pad = 16 - n % 16;
    memset(packed + n, (int)pad, pad);
    n += pad;
    memset(iv, 0, 16);
    mbedtls_aes_init(&aes);
    assert(!mbedtls_aes_setkey_enc(&aes, key, 256));
    assert(!mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, iv, encrypted[i]));
    assert(!mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, n, iv, packed,
                                  encrypted[i] + 16));
    mbedtls_aes_free(&aes);
    encrypted_size[i] = n + 16;
    assert(!mbedtls_sha1_ret(data + i * 64, 64, sha));
    for (j = 0; j < 20; ++j)
      snprintf(hashtext[i] + j * 2, 3, "%02x", sha[j]);
    steam_wire_bytes(&w, 1, sha, 20); /* fixed32 checksum */
    chunk[w.size++] = 21;
    put32(chunk + w.size, (uint32_t)adler32(0, data + i * 64, 64));
    w.size += 4;
    steam_wire_uint(&w, 3, i * 64);
    steam_wire_uint(&w, 4, 64);
    steam_wire_uint(&w, 5, encrypted_size[i]);
    steam_wire_bytes(&map, 6, chunk, w.size);
  }
  steam_wire_bytes(&pay, 1, mapping, map.size);
  steam_wire_uint(&meta, 1, 124);
  steam_wire_uint(&meta, 2, 999);
  steam_wire_uint(&meta, 4, 0);
  steam_wire_uint(&meta, 5, length);
  snprintf(folder, sizeof(folder), "%s/steam/manifests/123", root);
  assert(mkdirs(folder));
  assert(path(manifest, sizeof(manifest), folder, "124-999.manifest"));
  f = fopen(manifest, "wb");
  assert(f);
  section(f, 0x71f617d0, payload, pay.size);
  section(f, 0x1f4812be, metadata, meta.size);
  section(f, 0x1b81b817, sha, 20);
  put32(end, 0x32c415ab);
  assert(fwrite(end, 1, 4, f) == 4);
  assert(!fclose(f));
}
int main(void) {
  char root[] = "/tmp/autorun-install-XXXXXX", exe[512], p[1024];
  FILE *f;
  unsigned char readback[128];
  struct steam_backend b = {.account = 99, .language="english"};
  struct steam_install_callbacks cb = {.tick = fixture_tick,
                                       .progress = fixture_progress};
  assert(mkdtemp(root));
  fixtures(root);
  language_missing=1;
  assert(!steam_install_run(&b,root,123,&cb)&&result.error==3111&&result.available_languages==2&&!requests&&!steam_install_present(root,123));
  language_missing=0;
  deny = 1;
  assert(!steam_install_run(&b, root, 123, &cb) && result.error == 1009 &&
         !steam_install_present(root, 123) && !requests && !strcmp(result.language,"english"));
  deny = 0;
  low_disk=1;result=(struct steam_download_view){0};
  assert(!steam_install_run(&b,root,123,&cb)&&result.error==INSTALL_DISK&&!requests&&!steam_install_present(root,123));low_disk=0;
  cancel_after_first = 1;
  result = (struct steam_download_view){0};
  assert(!steam_install_run(&b, root, 123, &cb) &&
         result.state == STEAM_DOWNLOAD_CANCELLED && requests >= 2 && result.downloaded<=128 &&
         !steam_install_present(root, 123));
  cancel_after_first = 0;
  result = (struct steam_download_view){0};
  requests = 0;
  /* Whatever reached the card before the cancellation is checked and kept. */
  assert(steam_install_run(&b, root, 123, &cb) && requests <= 2 &&
         steam_install_present(root, 123) && result.downloaded == 128 &&
         result.state == STEAM_DOWNLOAD_COMPLETE);
  assert(steam_install_executable(root, 123, exe));
  assert(strstr(exe,"/drive_c/Steam/Fixture_ game/game.exe"));
  assert(steam_install_select_executable(root,123,exe));
  assert(!steam_install_select_executable(root,123,"/tmp/unrelated.exe"));
  f = fopen(exe, "rb");
  assert(f && fread(readback, 1, 128, f) == 128 &&
         !memcmp(readback, data, 128));
  fclose(f);
  result = (struct steam_download_view){0};
  requests = 0;
  assert(!steam_install_run(&b, root, 123, &cb) && !requests &&
         steam_install_present(root, 123));
  /* Recreate interrupted staging and corrupt just one previously valid chunk.
   */
  {
    char stage[1024], final[1024];
    snprintf(stage, sizeof(stage), "%s/steam/staging/123", root);
    snprintf(final, sizeof(final), "%s/drive_c/Steam/Fixture_ game", root);
    assert(!rename(final, stage));
    snprintf(p, sizeof(p), "%s/steam/staging/123/autorun-steam-install.ini",
             root);
    assert(!unlink(p));
    snprintf(p, sizeof(p), "%s/steam/staging/123/game.exe", root);
    f = fopen(p, "r+b");
    assert(f && fputc(255, f) != EOF && !fseek(f,64,SEEK_SET)&&fputc(255,f)!=EOF && !fclose(f));
    result = (struct steam_download_view){0};
    requests = handles = cleanups = auth_requests = 0;auth_required=1;
    assert(steam_install_run(&b, root, 123, &cb) && requests == 3 && auth_requests==1&&
           steam_install_present(root, 123));
  }
  /* Decode failures must be retried without trusting bad bytes or counting
   * reused verified chunks twice. Permanent corruption stays bounded. */
  assert(!steam_storage_uninstall(root,123,NULL,NULL));requests=0;auth_required=0;corrupt_responses=1;
  assert(steam_install_run(&b,root,123,&cb)&&result.integrity_retries==1&&result.downloaded==128&&requests>=3&&requests<=4);
  assert(!steam_storage_uninstall(root,123,NULL,NULL));requests=0;corrupt_forever=1;
  assert(!steam_install_run(&b,root,123,&cb)&&result.error==3103&&result.integrity_retries>=2&&result.integrity_retries<=4&&result.integrity_detail==STEAM_INTEGRITY_COMPRESSION&&!steam_install_present(root,123));
  corrupt_forever=0;requests=0;
  assert(steam_install_run(&b,root,123,&cb)&&result.downloaded==128);
  /* Explicit Cancel removes data from an earlier interrupted attempt as well
   * as this transfer. Missing fresh-download fixtures are rebuilt as CDN input. */
  assert(!steam_storage_uninstall(root,123,NULL,NULL));
  requests=0;auth_required=0;cancel_after_first=1;
  assert(!steam_install_run(&b,root,123,&cb)&&result.state==STEAM_DOWNLOAD_CANCELLED);
  explicit_discard=1;cb.discard_requested=fixture_discard;requests=0;
  assert(!steam_install_run(&b,root,123,&cb)&&result.state==STEAM_DOWNLOAD_DISCARDED&&!result.error&&!result.cleanup_errno);
  snprintf(p,sizeof(p),"%s/steam/staging/123",root);assert(access(p,F_OK)&&errno==ENOENT);
  snprintf(p,sizeof(p),"%s/steam/staging/123.plan",root);assert(access(p,F_OK)&&errno==ENOENT);
  snprintf(p,sizeof(p),"%s/steam/manifests/123",root);assert(access(p,F_OK)&&errno==ENOENT);
  assert(!result.downloaded&&!result.total&&!steam_install_present(root,123));
  explicit_discard=cancel_after_first=0;requests=0;fixtures(root);
  assert(steam_install_run(&b,root,123,&cb)&&requests==2&&steam_install_present(root,123));
  assert(!steam_storage_uninstall(root,123,NULL,NULL));requests=0;cancel_at_commit=1;
  assert(!steam_install_run(&b,root,123,&cb)&&result.state==STEAM_DOWNLOAD_DISCARDED&&!steam_install_present(root,123));
  cancel_at_commit=explicit_discard=0;requests=0;fixtures(root);
  assert(steam_install_run(&b,root,123,&cb)&&requests==2&&steam_install_present(root,123));
  /* Cancellation is not an uninstall, even if a caller supplies the flag after
   * encountering an already-installed AppID. */
  explicit_discard=1;requests=0;
  assert(!steam_install_run(&b,root,123,&cb)&&result.state==STEAM_DOWNLOAD_DISCARDED&&!requests&&steam_install_present(root,123));
  explicit_discard=0;
  /* A depot the licence does not cover is left out, as Steam leaves it; when
   * it held the chosen language the game is installed in English. A game with
   * no licensed depot of its own is refused. */
  assert(!steam_storage_uninstall(root,123,NULL,NULL));requests=0;fixtures(root);unlicensed=1;snprintf(b.language,sizeof(b.language),"russian");
  assert(steam_install_run(&b,root,123,&cb)&&requests==2&&steam_install_present(root,123)&&!strcmp(result.language,"english")&&!strcmp(b.language,"english"));
  assert(!steam_storage_uninstall(root,123,NULL,NULL));requests=0;unlicensed_game=1;
  assert(!steam_install_run(&b,root,123,&cb)&&result.error==1015&&!requests&&!steam_install_present(root,123));
  unlicensed=unlicensed_game=0;shared_game=1;
  assert(steam_install_run(&b,root,123,&cb)&&steam_install_present(root,123));
  assert(!steam_storage_uninstall(root,123,NULL,NULL));requests=0;shared_game=2;
  assert(!steam_install_run(&b,root,123,&cb)&&result.error==1015&&!requests&&!steam_install_present(root,123));
  shared_game=0;
  assert(steam_install_run(&b,root,123,&cb)&&steam_install_present(root,123));
  /* Remove only this test's individually named fixtures. */
  snprintf(p,sizeof(p),"%s/steam/selected/123.exe",root);unlink(p);snprintf(p,sizeof(p),"%s/steam/selected",root);rmdir(p);
  assert(!steam_storage_uninstall(root,123,NULL,NULL));
  assert(!steam_install_present(root,123));
  unlink(exe);
  snprintf(p, sizeof(p), "%s/drive_c/Steam/Fixture_ game/autorun-steam-install.ini",
           root);
  unlink(p);
  snprintf(p, sizeof(p), "%s/drive_c/Steam/Fixture_ game", root);
  rmdir(p);
  snprintf(p, sizeof(p), "%s/drive_c/Steam", root);
  rmdir(p);
  snprintf(p, sizeof(p), "%s/drive_c", root);
  rmdir(p);
  snprintf(p, sizeof(p), "%s/steam/manifests/123/124-999.manifest", root);
  unlink(p);
  snprintf(p, sizeof(p), "%s/steam/manifests/123", root);
  rmdir(p);
  snprintf(p, sizeof(p), "%s/steam/manifests", root);
  rmdir(p);
  snprintf(p, sizeof(p), "%s/steam/staging/123.plan", root);
  unlink(p);
  snprintf(p, sizeof(p), "%s/steam/staging", root);
  rmdir(p);
  snprintf(p, sizeof(p), "%s/steam", root);
  rmdir(p);
  snprintf(p,sizeof(p),"%s/steam/installed",root);rmdir(p);snprintf(p,sizeof(p),"%s/steam/trash",root);rmdir(p);snprintf(p,sizeof(p),"%s/steam",root);rmdir(p);
  snprintf(p,sizeof(p),"%s/logs/steam-download.log",root);assert(!unlink(p));
  snprintf(p,sizeof(p),"%s/logs",root);assert(!rmdir(p));
  assert(!rmdir(root));
  puts("Install ownership/space, cancel/resume, corrupt chunk repair, one CDN token, selection and existing-install preservation passed");
  return 0;
}
