#define _POSIX_C_SOURCE 200809L
#include "../source/steam/steam_storage.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static void mark(const char *directory, unsigned int app) {
  char p[1024];
  FILE *f;
  snprintf(p, sizeof(p), "%s/autorun-steam-install.ini", directory);
  f = fopen(p, "w");
  assert(f);
  assert(fprintf(f, "autorun_steam_install=1\nappid=%u\n", app) > 0);
  assert(!fclose(f));
}
static void empty(const char *p) {
  FILE *f = fopen(p, "w");
  assert(f);
  assert(!fclose(f));
}
static int interrupted(void *p) {
  int *left = p;
  return --*left > 0;
}
static void discard_tests(const char *root) {
  char p[1100], other[1100], outside[1100], installed[1024];
  unsigned int error;
  const char *dirs[]={"steam/staging","steam/staging/111","steam/staging/222",
    "steam/manifests","steam/manifests/111","steam/manifests/222"};
  const char *files[]={"steam/staging/111/data.bin","steam/staging/111.plan",
    "steam/staging/111.plan.tmp","steam/staging/111.plan.previous",
    "steam/manifests/111/old.manifest","steam/manifests/111/download.zip",
    "steam/manifests/111/new.manifest","steam/manifests/111/new.manifest.tmp",
    "steam/staging/222/sentinel","steam/staging/222.plan","steam/manifests/222/sentinel"};
  size_t i;
  /* Installed data for the same AppID is outside the discard namespace. */
  assert(steam_storage_plan(root,111,"Installed sentinel",installed));assert(!mkdir(installed,0700));
  mark(installed,111);assert(steam_storage_record(root,111,installed));
  snprintf(p,sizeof(p),"%s/data.bin",installed);empty(p);
  snprintf(outside,sizeof(outside),"%s/do-not-delete",root);empty(outside);
  for(i=0;i<sizeof(dirs)/sizeof(*dirs);++i){snprintf(p,sizeof(p),"%s/%s",root,dirs[i]);assert(!mkdir(p,0700));}
  for(i=0;i<sizeof(files)/sizeof(*files);++i){snprintf(p,sizeof(p),"%s/%s",root,files[i]);empty(p);}
  snprintf(p,sizeof(p),"%s/steam/staging/111/link",root);assert(!symlink(outside,p));
  assert(!steam_storage_discard(root,111,&error)&&!error);
  assert(!steam_storage_discard(root,111,&error)&&!error);
  for(i=0;i<8;++i){snprintf(p,sizeof(p),"%s/%s",root,files[i]);assert(access(p,F_OK)&&errno==ENOENT);}
  for(i=8;i<sizeof(files)/sizeof(*files);++i){snprintf(p,sizeof(p),"%s/%s",root,files[i]);assert(!access(p,F_OK));}
  snprintf(p,sizeof(p),"%s/data.bin",installed);assert(!access(p,F_OK));assert(!access(outside,F_OK));
  /* An ancestor link must fail before touching staging, including on retry. */
  snprintf(p,sizeof(p),"%s/steam/manifests",root);snprintf(other,sizeof(other),"%s/steam/manifest-backup",root);
  assert(!rename(p,other));assert(!symlink(other,p));
  assert(steam_storage_discard(root,222,&error)==3112&&error==EINVAL);
  snprintf(p,sizeof(p),"%s/steam/staging/222/sentinel",root);assert(!access(p,F_OK));
  snprintf(p,sizeof(p),"%s/steam/manifests",root);assert(!unlink(p)&&!rename(other,p));
  /* Unsupported leaf type forces partial cleanup failure; a retry is idempotent. */
  snprintf(p,sizeof(p),"%s/steam/staging/222/fifo",root);assert(!mkfifo(p,0600));
  assert(steam_storage_discard(root,222,&error)==3112&&error==EINVAL);assert(!unlink(p));
  assert(!steam_storage_discard(root,222,&error)&&!error);
  assert(!steam_storage_discard(root,999,&error)&&!error);
  assert(steam_storage_discard(root,0,&error)==3112&&error==EINVAL);
  snprintf(p,sizeof(p),"%s/data.bin",installed);assert(!unlink(p));
  assert(!steam_storage_uninstall(root,111,NULL,NULL));assert(!unlink(outside));
  snprintf(p,sizeof(p),"%s/steam/staging",root);assert(!rmdir(p));
  snprintf(p,sizeof(p),"%s/steam/manifests",root);assert(!rmdir(p));
}
int main(void) {
  char root[] = "/tmp/steam-storage-XXXXXX", planned[1024], found[512], p[1100],
       other[1024];
  int left;
  assert(mkdtemp(root));
  assert(steam_storage_plan(root, 123, "Example: game / title?", planned));
  assert(strstr(planned, "Example_ game _ title_"));
  assert(!mkdir(planned, 0700));
  mark(planned, 123);
  assert(steam_storage_record(root, 123, planned));
  assert(steam_storage_directory(root, 123, found) && !strcmp(found, planned));
  assert(!steam_storage_directory(root, 999, found));
  assert(steam_storage_plan(root, 456, "Example: game / title?", other) &&
         strstr(other, "(456)"));
  assert(steam_storage_plan(root, 789, "CON.exe", other) &&
         strstr(other, "_CON.exe"));
  assert(!steam_storage_record(root, 123, "/tmp/outside"));
  snprintf(other, sizeof(other), "%s/outside", root);
  empty(other);
  snprintf(p, sizeof(p), "%s/link", planned);
  assert(!symlink(other, p));
  assert(steam_storage_uninstall(root, 123, NULL, NULL) == 3104);
  assert(!access(other, F_OK));
  assert(steam_storage_directory(root, 123, found));
  assert(!unlink(p));
  snprintf(p, sizeof(p), "%s/sub", planned);
  assert(!mkdir(p, 0700));
  snprintf(p, sizeof(p), "%s/sub/data", planned);
  empty(p);
  /* Force an interrupted deletion after the safe rename. The marker survives,
   * and retry removes only the matching trash tree. */
  left = 9;
  assert(steam_storage_uninstall(root, 123, interrupted, &left) != 0);
  assert(steam_storage_pending(root, 123, found) && !strcmp(found, planned));
  assert(!access(other, F_OK));
  assert(!steam_storage_uninstall(root, 123, NULL, NULL));
  assert(!steam_storage_directory(root, 123, found));
  snprintf(p, sizeof(p), "%s/drive_c/Steam/321", root);
  assert(!mkdir(p, 0700));
  mark(p, 321);
  assert(steam_storage_directory(root, 321, found));
  assert(!steam_storage_uninstall(root, 321, NULL, NULL));
  snprintf(p, sizeof(p), "%s/drive_c/Steam/manual", root);
  assert(!mkdir(p, 0700));
  assert(steam_storage_uninstall(root, 654, NULL, NULL) == 3104);
  assert(!access(p, F_OK));
  assert(!rmdir(p));
  discard_tests(root);
  assert(!unlink(other));
  snprintf(p, sizeof(p), "%s/drive_c/Steam", root);
  assert(!rmdir(p));
  snprintf(p, sizeof(p), "%s/drive_c", root);
  assert(!rmdir(p));
  snprintf(p, sizeof(p), "%s/steam/installed", root);
  assert(!rmdir(p));
  snprintf(p, sizeof(p), "%s/steam/trash", root);
  assert(!rmdir(p));
  snprintf(p, sizeof(p), "%s/steam", root);
  assert(!rmdir(p));
  assert(!rmdir(root));
  puts("Named/colliding/reserved folders, legacy AppID paths, ownership "
       "marker, safe full cache discard/retry, symlink rejection and interrupted uninstall retry passed");
  return 0;
}
