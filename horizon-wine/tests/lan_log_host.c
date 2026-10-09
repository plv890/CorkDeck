#define _POSIX_C_SOURCE 200809L
#include "../source/diagnostics/lan_log.h"
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
static unsigned int port;
static char key[40];
static int live_reads, live_busy;
static int perf_controls;
static int perf_fixture(const char *action, char *out, size_t capacity) {
  perf_controls++;
  snprintf(out, capacity, "[PERF] action=%s\n", action);
  return 0;
}
static int live_fixture(char *out, size_t capacity) {
  live_reads++;
  if (live_busy) return EBUSY;
  snprintf(out, capacity, "[GAME] live memory\npassword=DO_NOT_SHARE\n"
           "[REMOTE-STATE] active=1 stage=5 fatal_kind=1\n");
  return 0;
}
static void request(const char *route, const char *token, const char *method,
                    char out[32768]) {
  struct sockaddr_in a = {.sin_family = AF_INET,
                          .sin_port = htons((unsigned short)port),
                          .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
  char req[2048];
  size_t used = 0;
  int fd = socket(AF_INET, SOCK_STREAM, 0), n;
  assert(fd >= 0 && !connect(fd, (struct sockaddr *)&a, sizeof(a)));
  n = snprintf(req, sizeof(req),
               "%s %s HTTP/1.1\r\nHost: localhost\r\nx-autorun-key: %s\r\n\r\n",
               method, route, token);
  assert(send(fd, req, (size_t)n, 0) == n);
  while ((n = (int)recv(fd, out + used, 32767 - used, 0)) > 0) {
    used += (size_t)n;
    assert(used < 32767);
  }
  assert(n == 0);
  out[used] = 0;
  close(fd);
}
static void put(const char *path, const char *text) {
  FILE *f = fopen(path, "wb");
  assert(f);
  assert(fwrite(text, 1, strlen(text), f) == strlen(text));
  assert(!fclose(f));
}
int main(int argc, char **argv) {
  if (argc == 3 && !strcmp(argv[1], "--preview")) {
    char url[128];
    assert(!lan_log_start(argv[2], 8181));
    lan_log_publish(
        "Steam build 2026.10.02-2 (SYNTHETIC HOST PREVIEW)\nappid=123 state=3 "
        "error=0 diagnostic_error=0\nverified=62914560 total=104857600 "
        "network=20971520 free=1073741824 free_known=1 rate_bps=9437184\n");
    assert(lan_log_url(url, sizeof(url)));
    puts(url);
    fflush(stdout);
    sleep(180);
    lan_log_stop();
    return 0;
  }
  char root[] = "/tmp/autorun-lan-XXXXXX", folder[512], path[768], url[128],
       old[128], data[32768], clean[256];
  assert(mkdtemp(root));
  snprintf(folder, sizeof(folder), "%s/logs", root);
  assert(!mkdir(folder, 0700));
  snprintf(path, sizeof(path), "%s/game.log", folder);
  put(path, "[GAME] hello\npassword=DO_NOT_SHARE\nAuthorization: Bearer "
            "SECRET\nSteam Guard: 12345\n[GAME] frame=0\npartial-token");
  snprintf(path, sizeof(path), "%s/stdout.txt", folder);
  put(path, "stdout diagnostic\n");
  snprintf(path, sizeof(path), "%s/vault.bin", folder);
  put(path, "DO_NOT_SHARE\n");
  snprintf(path, sizeof(path), "%s/unsafe.log", folder);
  assert(!symlink("/etc/passwd", path));
  assert(!lan_log_start(root, 0));
  assert(lan_log_url(url, sizeof(url)));
  strcpy(old, url);
  assert(sscanf(url, "http://127.0.0.1:%u/#%32s", &port, key) == 2);
  lan_log_set_live_reader(live_fixture);
  lan_log_set_performance_control(perf_fixture);
  request("/perf/start", "wrong", "POST", data);
  assert(strstr(data,"403 Response") && !perf_controls);
  request("/perf/start", key, "GET", data);
  assert(strstr(data,"400 Response") && !perf_controls);
  request("/perf/start", key, "POST", data);
  assert(strstr(data,"200 Response") && strstr(data,"action=start") && perf_controls==1);
  request("/perf", key, "GET", data);
  assert(strstr(data,"200 Response") && strstr(data,"action=read"));
  request("/perf/stop", key, "POST", data);
  assert(strstr(data,"200 Response") && strstr(data,"action=stop"));
  request("/perf", key, "POST", data);
  assert(strstr(data,"400 Response"));
  request("/live", "wrong", "GET", data);
  assert(strstr(data, "403 Response") && !live_reads);
  request("/live", key, "GET", data);
  assert(strstr(data, "200 Response") && strstr(data, "live memory") &&
         strstr(data, "fatal_kind=1") && !strstr(data, "DO_NOT_SHARE"));
  live_busy = 1;
  request("/live", key, "GET", data);
  assert(strstr(data, "409 Response"));
  live_busy = 0;
  request("/status", "wrong", "GET", data);
  assert(strstr(data, "403 Response") && !strstr(data, "appid="));
  request("/files", key, "GET", data);
  assert(strstr(data, "200 Response") && strstr(data, "game.log") &&
         strstr(data, "stdout.txt"));
  assert(!strstr(data, "vault.bin") && !strstr(data, "unsafe.log"));
  request("/tail/game.log", key, "GET", data);
  assert(strstr(data, "[GAME] hello") && strstr(data, "[GAME] frame=0"));
  assert(!strstr(data, "DO_NOT_SHARE") && !strstr(data, "SECRET") &&
         !strstr(data, "12345") && !strstr(data, "partial-token"));
  request("/tail/../vault.bin", key, "GET", data);
  assert(strstr(data, "409 Response"));
  request("/tail/unsafe.log", key, "GET", data);
  assert(strstr(data, "409 Response"));
  request("/tail/%2e%2e%2fvault.bin", key, "GET", data);
  assert(strstr(data, "409 Response"));
  request("/tail/missing.log", key, "GET", data);
  assert(strstr(data, "409 Response") && strstr(data, "filesystem error"));
  request("/files", key, "POST", data);
  assert(strstr(data, "400 Response"));
  request("/", "", "GET", data);
  assert(strstr(data, "200 Response") && strstr(data, "textContent") &&
         strstr(data, "no-store") && !strstr(data, key));
  lan_log_publish("build=2026100202 appid=123 network=1048576\n");
  request("/status", key, "GET", data);
  assert(strstr(data, "appid=123") && strstr(data, "network=1048576"));
  lan_log_network_pause();
  assert(!lan_log_url(url, sizeof(url)));
  lan_log_network_resume();
  assert(lan_log_url(url, sizeof(url)) && !strcmp(url, old));
  lan_log_game_phase();
  request("/status", key, "GET", data);
  assert(strstr(data, "Game runtime active"));
  /* Tail of a large log must not expose a secret whose key was truncated. */
  snprintf(path, sizeof(path), "%s/game.log", folder);
  {
    FILE *f = fopen(path, "wb");
    assert(f);
    fputs("password=", f);
    for (int i = 0; i < 18000; i++)
      fputc('s', f);
    fputs("\n[GAME] complete\n", f);
    assert(!fclose(f));
  }
  request("/tail/game.log", key, "GET", data);
  assert(strstr(data, "[GAME] complete") && !strstr(data, "ssss"));
  assert(lan_log_redact(clean, sizeof(clean), "ACCESS_TOKEN=123\nhello\n",
                        strlen("ACCESS_TOKEN=123\nhello\n")) > 0);
  assert(strstr(clean, "REDACTED") && strstr(clean, "hello") &&
         !strstr(clean, "123"));
  {
    const char *snapshot = "[REMOTE-SNAPSHOT] run=000001387082fa7b sequence=976 tick=1346442284042 build=nx-amd64-fex-2640\n";
    lan_log_redact(clean, sizeof(clean), snapshot, strlen(snapshot));
    assert(!strcmp(clean, snapshot));
    const char *unsafe[] = {
      "2fa=123456\n",
      "[REMOTE-SNAPSHOT] run=000001387082fa7b sequence=1 password=test\n",
      "[REMOTE-SNAPSHOT] run=000001387082fa7b sequence=1 2fa=123456\n",
      "[REMOTE-SNAPSHOT] run=000001387082fa7X sequence=1\n"
    };
    for (size_t i=0;i<sizeof(unsafe)/sizeof(unsafe[0]);i++) {
      lan_log_redact(clean,sizeof(clean),unsafe[i],strlen(unsafe[i]));
      assert(strstr(clean,"REDACTED") && !strstr(clean,"run="));
    }
  }
  lan_log_stop();
  assert(!lan_log_url(url, sizeof(url)));
  assert(!lan_log_start(root, 0));
  assert(lan_log_url(url, sizeof(url)) && strcmp(url, old));
  lan_log_stop();
  snprintf(path, sizeof(path), "%s/game.log", folder);
  unlink(path);
  snprintf(path, sizeof(path), "%s/stdout.txt", folder);
  unlink(path);
  snprintf(path, sizeof(path), "%s/vault.bin", folder);
  unlink(path);
  snprintf(path, sizeof(path), "%s/unsafe.log", folder);
  unlink(path);
  rmdir(folder);
  rmdir(root);
  puts("LAN HTTP authentication, redaction, bounded tails, traversal refusal, "
       "network restart and shutdown passed.");
  return 0;
}
