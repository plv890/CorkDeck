/* Deterministic out-of-order/403/cancel fixtures around the actual installer
 * and SD worker. The transport itself has a separate real HTTPS overlap test.
 */
#define steam_cdn_create fixture_pool_create
#define steam_cdn_start fixture_pool_start
#define steam_cdn_pump fixture_pool_pump
#define steam_cdn_take fixture_pool_take
#define steam_cdn_metrics fixture_pool_metrics
#define steam_cdn_cancel fixture_pool_cancel
#define steam_cdn_destroy fixture_pool_destroy
#define main serial_fixture_main
#include "steam_install_host.c"
#undef main
struct pool_request {
  char url[2048];
  unsigned char *buffer;
  size_t capacity;
  int active, done, connected;
  struct steam_cdn_result result;
};
struct steam_cdn {
  unsigned int count;
  struct pool_request jobs[STEAM_CDN_MAX_REQUESTS];
  struct steam_cdn_metrics metrics;
};
struct steam_cdn *fixture_pool_create(unsigned int count) {
  struct steam_cdn *p = calloc(1, sizeof(*p));
  assert(p && count <= STEAM_CDN_MAX_REQUESTS);
  p->count = count;
  return p;
}
int fixture_pool_start(struct steam_cdn *p, unsigned int slot, const char *url,
                       unsigned char *buffer, size_t capacity) {
  unsigned int i, active = 0;
  struct pool_request *r = &p->jobs[slot];
  assert(slot < p->count && !r->active && !r->done);
  snprintf(r->url, sizeof(r->url), "%s", url);
  r->buffer = buffer;
  r->capacity = capacity;
  r->active = 1;
  if (!r->connected) {
    r->connected = 1;
    ++p->metrics.connections;
  }
  for (i = 0; i < p->count; ++i)
    if (p->jobs[i].active)
      ++active;
  if (active > p->metrics.peak_active)
    p->metrics.peak_active = active;
  return 1;
}
int fixture_pool_pump(struct steam_cdn *p, unsigned int wait) {
  unsigned int i;
  (void)wait;
  for (i = p->count; i-- > 0;)
    if (p->jobs[i].active) {
      struct pool_request *r = &p->jobs[i];
      unsigned int chunk;
      ++requests;
      ++p->metrics.requests;
      p->metrics.request_ms += 10;
      p->metrics.first_byte_ms += 5;
      p->metrics.body_ms += 5;
      r->result = (struct steam_cdn_result){
          .slot = i,
          .http = auth_required && !strstr(r->url, "fixture=test") ? 403 : 200};
      if (r->result.http == 200)
        for (chunk = 0; chunk < 2; ++chunk)
          if (strstr(r->url, hashtext[chunk])) {
            assert(encrypted_size[chunk] <= r->capacity);
            memcpy(r->buffer, encrypted[chunk], encrypted_size[chunk]);
            r->result.bytes = encrypted_size[chunk];
            if(corrupt_forever || corrupt_responses){r->buffer[17]^=1;if(corrupt_responses)--corrupt_responses;}
            if(short_responses){--r->result.bytes;--short_responses;}
            p->metrics.bytes += encrypted_size[chunk];
            break;
          }
      r->active = 0;
      r->done = 1;
    }
  return 1;
}
int fixture_pool_take(struct steam_cdn *p, struct steam_cdn_result *r) {
  unsigned int i;
  for (i = p->count; i-- > 0;)
    if (p->jobs[i].done) {
      *r = p->jobs[i].result;
      p->jobs[i].done = 0;
      return 1;
    }
  return 0;
}
void fixture_pool_metrics(struct steam_cdn *p, struct steam_cdn_metrics *out) {
  *out = p->metrics;
}
void fixture_pool_cancel(struct steam_cdn *p) {
  unsigned int i;
  for (i = 0; i < p->count; ++i) {
    p->jobs[i].active = p->jobs[i].done = 0;
    p->jobs[i].buffer = NULL;
    memset(p->jobs[i].url, 0, sizeof(p->jobs[i].url));
  }
}
void fixture_pool_destroy(struct steam_cdn *p) {
  if (p) {
    fixture_pool_cancel(p);
    free(p);
  }
}
static void small_manifest(const char *root,unsigned int files,int mixed){
  unsigned char payload[32768],metadata[128],mapping[1024],chunk[128],sha[20],end[4];
  struct steam_wire_writer pay={payload,sizeof(payload),0,0},meta={metadata,sizeof(metadata),0,0};
  char path[1024],name[64];unsigned int i;FILE *f;
  fixtures(root); /* also constructs both real encrypted CDN chunks */
  for(i=0;i<files;++i){
    struct steam_wire_writer map={mapping,sizeof(mapping),0,0},w={chunk,sizeof(chunk),0,0};
    const unsigned char *bytes=data+(i%2)*64;
    size_t size=mixed&&i==9 ? 128 : mixed&&(i==7||i==8) ? 0 : 64;
    unsigned int part,parts=size==128 ? 2 : size ? 1 : 0;
    snprintf(name,sizeof(name),i ? "tiny/%03u.dat" : "game.exe",i);
    if(mixed){
      if(i==7)strcpy(name,"dir");
      if(i==8)strcpy(name,"dir/empty.dat");
      if(i==9)strcpy(name,"big.dat");
      if(i==10||i==11)strcpy(name,"overlap.dat");
    }
    assert(!mbedtls_sha1_ret(size==128 ? data : bytes,size,sha));
    if(i==8&&mixed==2)memset(sha,0,20);
    steam_wire_bytes(&map,1,name,strlen(name));steam_wire_uint(&map,2,size);
    if(!(i==8&&mixed==3))steam_wire_bytes(&map,5,sha,20);
    if(mixed&&i==7)steam_wire_uint(&map,3,64);
    for(part=0;part<parts;++part){
      unsigned int selected=size==128 ? part : i%2;
      const unsigned char *part_bytes=data+selected*64;
      w=(struct steam_wire_writer){chunk,sizeof(chunk),0,0};
      assert(!mbedtls_sha1_ret(part_bytes,64,sha));
      steam_wire_bytes(&w,1,sha,20);chunk[w.size++]=21;put32(chunk+w.size,(uint32_t)adler32(0,part_bytes,64));w.size+=4;
      steam_wire_uint(&w,3,part*64);steam_wire_uint(&w,4,64);steam_wire_uint(&w,5,encrypted_size[selected]);
      steam_wire_bytes(&map,6,chunk,w.size);
    }
    steam_wire_bytes(&pay,1,mapping,map.size);
  }
  steam_wire_uint(&meta,1,124);steam_wire_uint(&meta,2,999);steam_wire_uint(&meta,4,0);steam_wire_uint(&meta,5,files*64);
  snprintf(path,sizeof(path),"%s/steam/manifests/123/124-999.manifest",root);f=fopen(path,"wb");assert(f);
  section(f,0x71f617d0,payload,pay.size);section(f,0x1f4812be,metadata,meta.size);section(f,0x1b81b817,sha,20);
  put32(end,0x32c415ab);assert(fwrite(end,1,4,f)==4&&!fclose(f));
}
static void small_installer_tests(void){
  char root[]="/tmp/steam-small-install-XXXXXX",path[1024],exe[512];
  struct steam_backend b={.account=99,.concurrency=2};
  struct steam_install_callbacks cb={.tick=fixture_tick,.progress=fixture_progress};
  const unsigned int concurrency[]={1,2,4,16};unsigned int i,j,error;
  assert(mkdtemp(root));small_manifest(root,40,0);
  for(i=0;i<4;++i){
    b.concurrency=concurrency[i];requests=auth_requests=0;auth_required=1;
    assert(steam_install_run(&b,root,123,&cb)&&result.state==STEAM_DOWNLOAD_COMPLETE&&
      result.batch_files==40&&result.batch_peak_files<=OPEN_FILES&&result.peak_requests==b.concurrency&&
      result.downloaded==40*64&&requests==(int)(40+b.concurrency)&&auth_requests==1);
    assert(steam_install_executable(root,123,exe)&&strstr(exe,"game.exe"));
    for(j=0;j<40;++j){FILE *f;unsigned char saved[64];
      if(j)snprintf(path,sizeof(path),"%s/drive_c/Steam/Fixture_ game/tiny/%03u.dat",root,j);
      else snprintf(path,sizeof(path),"%s/drive_c/Steam/Fixture_ game/game.exe",root);
      f=fopen(path,"rb");assert(f&&fread(saved,1,64,f)==64&&!memcmp(saved,data+(j%2)*64,64)&&!fclose(f));
    }
    assert(!steam_storage_uninstall(root,123,NULL,NULL));
  }
  auth_required=0;requests=0;corrupt_responses=1;
  assert(steam_install_run(&b,root,123,&cb)&&result.integrity_retries==1&&result.downloaded==40*64&&requests>40&&requests<=48);
  assert(!steam_storage_uninstall(root,123,NULL,NULL));requests=0;short_responses=1;
  assert(steam_install_run(&b,root,123,&cb)&&result.integrity_retries==1&&result.downloaded==40*64);
  assert(!steam_storage_uninstall(root,123,NULL,NULL));requests=0;corrupt_forever=1;
  assert(!steam_install_run(&b,root,123,&cb)&&result.error==3103&&result.integrity_retries>=2&&result.integrity_detail==STEAM_INTEGRITY_COMPRESSION&&!steam_install_present(root,123));
  corrupt_forever=0;requests=0;
  assert(steam_install_run(&b,root,123,&cb)&&result.downloaded==40*64&&requests<=40);
  assert(!steam_storage_uninstall(root,123,NULL,NULL));
  auth_required=0;requests=0;cancel_after_first=1;
  assert(!steam_install_run(&b,root,123,&cb)&&result.state==STEAM_DOWNLOAD_CANCELLED&&!steam_install_present(root,123));
  cancel_after_first=0;requests=0;
  assert(steam_install_run(&b,root,123,&cb)&&requests<=40&&result.downloaded==40*64&&steam_install_present(root,123));
  assert(!steam_storage_uninstall(root,123,NULL,NULL));
  /* Lookahead must preserve directory/empty/multi-chunk boundaries and duplicate
   * path order, not discard a manifest entry or let two writers race its file. */
  for(error=1;error<=3;++error){
  small_manifest(root,24,(int)error);requests=0;
  assert(steam_install_run(&b,root,123,&cb)&&result.batch_files==23&&result.downloaded==23*64&&requests==23);
  snprintf(path,sizeof(path),"%s/drive_c/Steam/Fixture_ game/big.dat",root);
  {FILE *f=fopen(path,"rb");unsigned char saved[128];assert(f&&fread(saved,1,128,f)==128&&!memcmp(saved,data,128)&&!fclose(f));}
  snprintf(path,sizeof(path),"%s/drive_c/Steam/Fixture_ game/overlap.dat",root);
  {FILE *f=fopen(path,"rb");unsigned char saved[64];assert(f&&fread(saved,1,64,f)==64&&!memcmp(saved,data+64,64)&&!fclose(f));}
  snprintf(path,sizeof(path),"%s/drive_c/Steam/Fixture_ game/dir/empty.dat",root);
  {struct stat st;assert(!stat(path,&st)&&!st.st_size);}
  assert(!steam_storage_uninstall(root,123,NULL,NULL));
  }
  small_manifest(root,40,0);
  requests=0;cancel_after_first=1;cb.discard_requested=fixture_discard;
  assert(!steam_install_run(&b,root,123,&cb)&&result.state==STEAM_DOWNLOAD_DISCARDED&&!result.cleanup_errno);
  snprintf(path,sizeof(path),"%s/steam/staging/123",root);assert(access(path,F_OK)&&errno==ENOENT);
  snprintf(path,sizeof(path),"%s/steam/manifests/123",root);assert(access(path,F_OK)&&errno==ENOENT);
  cancel_after_first=0;small_manifest(root,40,0);requests=0;
  assert(steam_install_run(&b,root,123,&cb)&&requests==40&&result.batch_files==40);
  assert(!steam_storage_uninstall(root,123,NULL,NULL));assert(!steam_storage_discard(root,123,&error));
  snprintf(path,sizeof(path),"%s/logs/steam-download.log",root);assert(!unlink(path));
  {const char *dirs[]={"logs","steam/staging","steam/manifests","steam/trash","steam/installed","steam","drive_c/Steam","drive_c"};
   for(i=0;i<sizeof(dirs)/sizeof(*dirs);++i){snprintf(path,sizeof(path),"%s/%s",root,dirs[i]);assert(!rmdir(path));}}
  assert(!rmdir(root));puts("40 small-file installs across metadata batches: concurrency 1/2/4/16, shared auth, exact SD bytes, executable registration, interruption/resume and explicit discard passed");
}
int main(void) {
  char root[] = "/tmp/steam-parallel-install-XXXXXX", stage[1024], final[1024],
       name[1152];
  FILE *f;
  struct steam_backend b = {.account = 99, .concurrency = 2};
  struct steam_install_callbacks cb = {.tick = fixture_tick,
                                       .progress = fixture_progress};
  unsigned char readback[128];
  unsigned int i;
  assert(!serial_fixture_main());
  small_installer_tests();
  assert(mkdtemp(root));
  fixtures(root);
  requests = auth_requests = 0;
  auth_required = 1;
  assert(steam_install_run(&b, root, 123, &cb) && result.peak_requests == 2 &&
         requests == 4 && auth_requests == 1);
  assert(steam_install_executable(root, 123, name));
  f = fopen(name, "rb");
  assert(f && fread(readback, 1, 128, f) == 128 &&
         !memcmp(readback, data, 128));
  fclose(f);
  snprintf(final, sizeof(final), "%s/drive_c/Steam/Fixture_ game", root);
  snprintf(stage, sizeof(stage), "%s/steam/staging/123", root);
  assert(!rename(final, stage));
  snprintf(name, sizeof(name), "%s/autorun-steam-install.ini", stage);
  assert(!unlink(name));
  snprintf(name, sizeof(name), "%s/game.exe", stage);
  f = fopen(name, "r+b");
  assert(f && fputc(255, f) != EOF && !fseek(f, 64, SEEK_SET) &&
         fputc(255, f) != EOF && !fclose(f));
  requests = 0;
  auth_required = 0;
  cancel_after_first = 1;
  assert(!steam_install_run(&b, root, 123, &cb) &&
         result.state == STEAM_DOWNLOAD_CANCELLED &&
         !steam_install_present(root, 123));
  assert(result.downloaded <= 128);
  cancel_after_first = 0;
  requests = 0;
  assert(steam_install_run(&b, root, 123, &cb) && requests <= 2 &&
         steam_install_present(root, 123));
  assert(!steam_storage_uninstall(root,123,NULL,NULL));
  requests=0;cancel_after_first=1;explicit_discard=0;cb.discard_requested=fixture_discard;
  assert(!steam_install_run(&b,root,123,&cb)&&result.state==STEAM_DOWNLOAD_DISCARDED&&!result.cleanup_errno);
  snprintf(name,sizeof(name),"%s/steam/staging/123",root);assert(access(name,F_OK)&&errno==ENOENT);
  snprintf(name,sizeof(name),"%s/steam/manifests/123",root);assert(access(name,F_OK)&&errno==ENOENT);
  fixtures(root);requests=0;cancel_after_first=explicit_discard=0;
  assert(steam_install_run(&b,root,123,&cb)&&requests==2&&steam_install_present(root,123));
  assert(!steam_storage_uninstall(root, 123, NULL, NULL));
  {
    const char *files[] = {"logs/steam-download.log", "steam/staging/123.plan",
                           "steam/manifests/123/124-999.zip",
                           "steam/manifests/123/124-999.manifest"};
    for (i = 0; i < sizeof(files) / sizeof(*files); ++i) {
      snprintf(name, sizeof(name), "%s/%s", root, files[i]);
      unlink(name);
    }
  }
  {
    const char *dirs[] = {"logs",
                          "steam/staging",
                          "steam/manifests/123",
                          "steam/manifests",
                          "steam/trash",
                          "steam/installed",
                          "steam",
                          "drive_c/Steam",
                          "drive_c"};
    for (i = 0; i < sizeof(dirs) / sizeof(*dirs); ++i) {
      snprintf(name, sizeof(name), "%s/%s", root, dirs[i]);
      rmdir(name);
    }
  }
  assert(!rmdir(root));
  puts("Parallel installer: out-of-order verified writes, shared CDN "
       "authorization, cancellation and reverified resume passed");
  return 0;
}
