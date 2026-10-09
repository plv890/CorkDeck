#define _POSIX_C_SOURCE 200809L
#include "lan_log.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#ifdef __SWITCH__
#include <switch.h>
#else
#include <sys/random.h>
#endif
#define TAIL 16384
#define BODY (TAIL + 4096)
static pthread_t worker;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int running, stop_requested, listener = -1;
static char folder[768], key[33], address[32], status[2048];
static unsigned short bound_port;
static time_t published;
static int resuming, game_phase;
static char resume_key[33];
static int (*live_reader)(char *, size_t);
static int (*performance_control)(const char *, char *, size_t);
static const char page[] =
    "<!doctype html><meta charset=utf-8><meta name=viewport "
    "content='width=device-width,initial-scale=1'>"
    "<title>Autorun "
    "diagnostics</title><style>body{background:#171d25;color:#dce5ef;font:16px "
    "system-ui;margin:24px}"
    "button,select{font:inherit;padding:8px}pre{white-space:pre-wrap;overflow-"
    "wrap:anywhere;background:#10151c;padding:16px}"
    "small{color:#aab9c9}</style><h1>Autorun diagnostics</h1>"
    "<p>Read-only LAN view. Refreshes every 2 seconds. Refresh the log list "
    "after starting a game.</p>"
    "<small>Briefly disconnects when networking restarts before Play. After "
    "Autorun exits or restarts, enable again to view saved game logs. "
    "Secrets are filtered, but logs can contain game names and local paths. "
    "Use a trusted LAN.</small>"
    "<h2>Steam transfer</h2><progress id=progress max=100 "
    "value=0></progress><pre id=status>Connecting...</pre>"
    "<h2>Logs</h2><select id=files><option value=''>Choose a "
    "log</option><option value='@live' selected>Live runtime</option></select> "
    "<button id=refresh>Refresh list</button><pre id=log>Select a log. Only "
    "the last 16 KiB is read.</pre>"
    "<script>const token=location.hash.slice(1);"
    "const "
    "files=document.getElementById('files'),s=document.getElementById('status')"
    ",l=document.getElementById('log');"
    "async function get(p){const r=await "
    "fetch(p,{headers:{'X-Autorun-Key':token},cache:'no-store'});"
    "const t=await r.text();if(!r.ok)throw Error(t);return t;}"
    "async function list(){try{let old=files.value,t=await "
    "get('/files');files.replaceChildren(new Option('Live runtime','@live'));"
    "for(const n of t.trim().split('\\n'))if(n)files.add(new "
    "Option(n,n));files.value=old;}catch(e){l.textContent=String(e);}}"
    "let busy=false;async function poll(){if(busy)return;busy=true;try{const "
    "t=await get('/status');s.textContent=t;const v=t.match(/verified=(\\d+) "
    "total=(\\d+)/"
    ");document.getElementById('progress').value=v&&Number(v[2])>0?100*Number("
    "v[1])/Number(v[2]):0;"
    "if(files.value)l.textContent=await "
    "get(files.value==='@live'?'/live':'/tail/'+encodeURIComponent(files.value));}"
    "catch(e){s.textContent='Disconnected or unavailable: '+e+'. Re-enable LAN "
    "diagnostics on the console after restart.';}"
    "finally{busy=false;}}document.getElementById('refresh').onclick=list;"
    "files.onchange=poll;list();poll();setInterval(poll,2000);</script>";
static int contains(const char *p, size_t n, const char *word) {
  size_t m = strlen(word), i, j;
  for (i = 0; i + m <= n; ++i) {
    for (j = 0; j < m && tolower((unsigned char)p[i + j]) == word[j]; ++j) {
    }
    if (j == m)
      return 1;
  }
  return 0;
}
size_t lan_log_redact(char *out, size_t cap, const char *in, size_t size) {
  static const char *words[] = {"password",
                                "passwd",
                                "token",
                                "cookie",
                                "authorization",
                                "steamguard",
                                "steam guard",
                                "guard_code",
                                "guardcode",
                                "secret",
                                "session",
                                "bearer",
                                "credential",
                                "challenge_url",
                                "account_name",
                                "login",
                                "qr",
                                "eyj",
                                "auth",
                                "ticket",
                                "otp",
                                "2fa",
                                "guard",
                                "code=",
                                "code:",
                                "steamid",
                                "http://",
                                "https://"};
  size_t pos = 0, used = 0, i;
  if (!cap)
    return 0;
  while (pos < size) {
    size_t end = pos, n;
    int hide = 0;
    while (end < size && in[end] != '\n')
      ++end;
    /* An incomplete final line could conceal a split secret. */
    if (end == size)
      break;
    n = end - pos + 1;
    for (i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
      if (contains(in + pos, n, words[i])) {
        /* A numeric snapshot ID can coincidentally contain the hex digits
         * "2fa". Exempt only that 16-digit field, never a credential line or
         * a second occurrence elsewhere in the metadata. */
        static const char prefix[] = "[REMOTE-SNAPSHOT] run=";
        size_t start = sizeof(prefix) - 1, j;
        if (!strcmp(words[i], "2fa") && n >= start + 16 + 10 &&
            !memcmp(in + pos, prefix, start) &&
            !memcmp(in + pos + start + 16, " sequence=", 10)) {
          for (j = 0; j < 16 &&
               ((in[pos+start+j] >= '0' && in[pos+start+j] <= '9') ||
                (in[pos+start+j] >= 'a' && in[pos+start+j] <= 'f')); ++j) {}
          if (j == 16 && !contains(in + pos + start + 16, n - start - 16, "2fa"))
            continue;
        }
        hide = 1;
        break;
      }
    if (hide) {
      const char *replacement = "[REDACTED sensitive line]\n";
      n = strlen(replacement);
      if (n >= cap - used)
        break;
      memcpy(out + used, replacement, n);
    } else {
      if (n >= cap - used)
        break;
      for (i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)in[pos + i];
        out[used + i] =
            c == '\n' || c == '\t' || (c >= 32 && c < 127) ? (char)c : '?';
      }
    }
    used += n;
    pos = end + 1;
  }
  out[used] = 0;
  return used;
}
static int valid_name(const char *name) {
  size_t n = strlen(name), i;
  if (!n || n > 160 || name[0] == '.' || strstr(name, ".."))
    return 0;
  for (i = 0; i < n; ++i)
    if (!((name[i] >= 'a' && name[i] <= 'z') ||
          (name[i] >= 'A' && name[i] <= 'Z') ||
          (name[i] >= '0' && name[i] <= '9') || name[i] == '-' ||
          name[i] == '_' || name[i] == '.'))
      return 0;
  return (n > 4 && !strcmp(name + n - 4, ".log")) ||
         !strcmp(name, "stdout.txt") || !strcmp(name, "stderr.txt");
}
static int regular_log(const char *name, char path[960]) {
  struct stat st;
  if (!valid_name(name))
    return EINVAL;
  snprintf(path, 960, "%s/%s", folder, name);
  if (lstat(path, &st))
    return errno ? errno : EIO;
  return S_ISREG(st.st_mode) && !S_ISLNK(st.st_mode) ? 0 : EINVAL;
}
static int list_logs(char *out, size_t cap) {
  DIR *dir = opendir(folder);
  struct dirent *e;
  unsigned int scanned = 0, count = 0;
  size_t used = 0;
  if (!dir)
    return errno ? errno : EIO;
  while (scanned++ < 512 && count < 64 && (e = readdir(dir))) {
    char path[960];
    size_t n = strlen(e->d_name);
    /* Keep valid-but-unreadable names visible so /tail reports the error. */
    if (regular_log(e->d_name, path) == EINVAL)
      continue;
    if (used + n + 2 >= cap)
      break;
    memcpy(out + used, e->d_name, n);
    used += n;
    out[used++] = '\n';
    ++count;
  }
  out[used] = 0;
  closedir(dir);
  return 0;
}
static int tail_log(const char *name, char *out, size_t cap) {
  char path[960], raw[TAIL + 1];
  FILE *f;
  long end;
  size_t n, skip = 0;
  int error = regular_log(name, path);
  if (error)
    return error;
  f = fopen(path, "rb");
  if (!f)
    return errno ? errno : EIO;
  if (fseek(f, 0, SEEK_END) || (end = ftell(f)) < 0 ||
      fseek(f, end > TAIL ? end - TAIL : 0, SEEK_SET)) {
    error = errno ? errno : EIO;
    goto done;
  }
  n = fread(raw, 1, TAIL, f);
  if (ferror(f)) {
    error = errno ? errno : EIO;
    goto done;
  }
  /* Discard a truncated first line; its sensitive prefix may be outside
   * the bounded window. The incomplete final line is filtered separately. */
  if (end > TAIL) {
    while (skip < n && raw[skip] != '\n')
      ++skip;
    if (skip < n)
      ++skip;
  }
  lan_log_redact(out, cap, raw + skip, n - skip);
done:
  memset(raw, 0, sizeof(raw));
  if (fclose(f) && !error)
    error = errno ? errno : EIO;
  return error;
}
static int wait_fd(int fd, short event, int timeout) {
  struct pollfd p = {.fd = fd, .events = event};
  int n;
  do {
    n = poll(&p, 1, timeout);
  } while (n < 0 && errno == EINTR);
  return n > 0 && (p.revents & event);
}
static int send_all(int fd, const char *data, size_t n) {
  struct timespec started, now;
  clock_gettime(CLOCK_MONOTONIC, &started);
  while (n) {
    ssize_t written;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (now.tv_sec - started.tv_sec >= 2 ||
        __atomic_load_n(&stop_requested, __ATOMIC_ACQUIRE))
      return 0;
    if (!wait_fd(fd, POLLOUT, 1000))
      return 0;
#ifdef MSG_NOSIGNAL
    written = send(fd, data, n, MSG_NOSIGNAL);
#else
    written = send(fd, data, n, 0);
#endif
    if (written < 0 && (errno == EINTR || errno == EAGAIN))
      continue;
    if (written <= 0)
      return 0;
    data += written;
    n -= (size_t)written;
  }
  return 1;
}
static void respond(int fd, unsigned int code, const char *body, int html) {
  char header[768];
  int n = snprintf(header, sizeof(header),
                   "HTTP/1.1 %u Response\r\nContent-Type: %s; "
                   "charset=utf-8\r\nContent-Length: %llu\r\n"
                   "Connection: close\r\nCache-Control: "
                   "no-store\r\nX-Content-Type-Options: nosniff\r\n"
                   "Referrer-Policy: no-referrer\r\nContent-Security-Policy: "
                   "default-src 'none'; "
                   "script-src 'unsafe-inline'; style-src 'unsafe-inline'; "
                   "connect-src 'self'; frame-ancestors 'none'\r\n\r\n",
                   code, html ? "text/html" : "text/plain",
                   (unsigned long long)strlen(body));
  if (n > 0 && (size_t)n < sizeof(header) && send_all(fd, header, (size_t)n))
    send_all(fd, body, strlen(body));
}
static int authorized(const char *request) {
  const char *p = request;
  unsigned int different = 0;
  size_t i;
  while ((p = strstr(p, "\r\n"))) {
    p += 2;
    if (!strncasecmp(p, "X-Autorun-Key: ", 15))
      break;
  }
  if (!p)
    return 0;
  p += 15;
  if (strlen(p) < 34 || p[32] != '\r' || p[33] != '\n')
    return 0;
  for (i = 0; i < 32; ++i)
    different |= (unsigned char)p[i] ^ (unsigned char)key[i];
  return !different;
}
static void request_client(int fd) {
  char request[4096] = {0}, route[256], method[8], version[16], body[BODY];
  size_t used = 0;
  unsigned int error = 0;
  struct timespec started, now;
  clock_gettime(CLOCK_MONOTONIC, &started);
  while (used < sizeof(request) - 1 &&
         !__atomic_load_n(&stop_requested, __ATOMIC_ACQUIRE)) {
    ssize_t got;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (now.tv_sec - started.tv_sec >= 2)
      break;
    if (!wait_fd(fd, POLLIN, 250))
      continue;
    got = recv(fd, request + used, sizeof(request) - 1 - used, 0);
    if (got <= 0)
      return;
    used += (size_t)got;
    request[used] = 0;
    if (strstr(request, "\r\n\r\n"))
      break;
  }
  if (!strstr(request, "\r\n\r\n") ||
      sscanf(request, "%7s %255s %15s", method, route, version) != 3 ||
      (strcmp(method, "GET") && strcmp(method, "POST")) ||
      (strcmp(version, "HTTP/1.1") && strcmp(version, "HTTP/1.0"))) {
    respond(fd, 400, "Invalid request\n", 0);
    return;
  }
  if (!strcmp(route, "/") && !strcmp(method, "GET")) {
    respond(fd, 200, page, 1);
    return;
  }
  if (!authorized(request)) {
    respond(fd, 403,
            "Open the complete URL shown on the console. Access key missing or "
            "expired.\n",
            0);
    return;
  }
  body[0] = 0;
  if (!strcmp(method, "POST") && strcmp(route, "/perf/start") && strcmp(route, "/perf/stop")) {
    respond(fd, 400, "POST supported only for /perf/start and /perf/stop\n", 0);
    return;
  }
  if (!strcmp(route, "/perf") || !strcmp(route, "/perf/start") || !strcmp(route, "/perf/stop")) {
    int (*control)(const char *, char *, size_t);
    const char *action = !strcmp(route, "/perf") ? "read" : route + 6;
    if ((strcmp(action, "read") && strcmp(method, "POST")) ||
        (!strcmp(action, "read") && strcmp(method, "GET"))) {
      respond(fd, 400, "GET /perf or POST /perf/start /perf/stop required\n", 0);
      return;
    }
    pthread_mutex_lock(&lock); control = performance_control; pthread_mutex_unlock(&lock);
    error = control ? (unsigned int)control(action, body, sizeof(body)) : ENOSYS;
    respond(fd, error ? 409 : 200, error ? "Performance collector unavailable or busy\n" : body, 0);
    memset(body, 0, sizeof(body)); memset(request, 0, sizeof(request));
    return;
  }
  if (!strcmp(route, "/status")) {
    pthread_mutex_lock(&lock);
    snprintf(body, sizeof(body), "%s. Steam snapshot age: %lld seconds\n%s",
             game_phase
                 ? "Game runtime active; Steam transfer snapshot retained"
                 : "Launcher active",
             published ? (long long)(time(NULL) - published) : -1LL, status);
    pthread_mutex_unlock(&lock);
  } else if (!strcmp(route, "/live")) {
    char raw[BODY] = {0};
    int (*reader)(char *, size_t);
    pthread_mutex_lock(&lock);
    reader = live_reader;
    pthread_mutex_unlock(&lock);
    error = reader ? (unsigned int)reader(raw, sizeof(raw)) : ENOSYS;
    if (!error) lan_log_redact(body, sizeof(body), raw, strnlen(raw, sizeof(raw)));
    memset(raw, 0, sizeof(raw));
  } else if (!strcmp(route, "/files"))
    error = (unsigned int)list_logs(body, sizeof(body));
  else if (!strncmp(route, "/tail/", 6))
    error = (unsigned int)tail_log(route + 6, body, sizeof(body));
  else {
    respond(fd, 404, "Unknown endpoint\n", 0);
    return;
  }
  if (error)
    snprintf(body, sizeof(body),
             "Log read failed: filesystem error %u. File may be absent, "
             "locked, or the SD unavailable.\n",
             error);
  respond(fd, error ? 409 : 200, body, 0);
  memset(request, 0, sizeof(request));
  memset(body, 0, sizeof(body));
}
static void *serve(void *unused) {
  (void)unused;
  while (!__atomic_load_n(&stop_requested, __ATOMIC_ACQUIRE)) {
    int fd;
    if (!wait_fd(listener, POLLIN, 200))
      continue;
    fd = accept(listener, NULL, NULL);
    if (fd < 0)
      continue;
    if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
      close(fd);
      continue;
    }
    request_client(fd);
    close(fd);
  }
  return NULL;
}
void lan_log_publish(const char *text) {
  pthread_mutex_lock(&lock);
  snprintf(status, sizeof(status), "%s", text);
  published = time(NULL);
  pthread_mutex_unlock(&lock);
}
void lan_log_set_live_reader(int (*reader)(char *, size_t)) {
  pthread_mutex_lock(&lock);
  live_reader = reader;
  pthread_mutex_unlock(&lock);
}
void lan_log_set_performance_control(int (*control)(const char *, char *, size_t)) {
  pthread_mutex_lock(&lock);
  performance_control = control;
  pthread_mutex_unlock(&lock);
}
void lan_log_stop(void) {
  if (!running)
    return;
  __atomic_store_n(&stop_requested, 1, __ATOMIC_RELEASE);
  pthread_join(worker, NULL);
  close(listener);
  listener = -1;
  running = 0;
  memset(key, 0, sizeof(key));
}
void lan_log_network_pause(void) {
  resuming = running;
  if (resuming)
    memcpy(resume_key, key, sizeof(key));
  lan_log_stop();
}
void lan_log_network_resume(void) {
  char root[768];
  size_t n;
  if (!resuming)
    return;
  snprintf(root, sizeof(root), "%s", folder);
  n = strlen(root);
  if (n >= 5) {
    root[n - 5] = 0;
    /* The same short-lived browser key survives only this service reset. */
    unsigned int error = lan_log_start(root, bound_port);
    if (error) {
      char path[832];
      FILE *f;
      snprintf(path, sizeof(path), "%s/lan-service.log", folder);
      /* No request, IP, browser key, path or credential is recorded. */
      if (!(f = fopen(path, "ab")) ||
          fprintf(f, "lan_service_v1 restart_error=%u\n", error) < 0)
        fputs("[LAN] diagnostic server restart failed; error report "
              "unavailable\n",
              stderr);
      if (f)
        fclose(f);
    }
  }
  resuming = 0;
  memset(resume_key, 0, sizeof(resume_key));
}
void lan_log_game_phase(void) {
  pthread_mutex_lock(&lock);
  game_phase = 1;
  pthread_mutex_unlock(&lock);
}
unsigned int lan_log_start(const char *root, unsigned short port) {
  struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(port)};
  struct stat st;
  socklen_t size = sizeof(a);
  unsigned char random[16];
  unsigned int error;
  pthread_attr_t attr;
  int n, i;
  if (running)
    return 0;
  n = snprintf(folder, sizeof(folder), "%s/logs", root);
  if (n < 0 || (size_t)n >= sizeof(folder))
    return ENAMETOOLONG;
  if (lstat(folder, &st))
    return errno ? (unsigned int)errno : EIO;
  if (!S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode))
    return EINVAL;
#ifdef __SWITCH__
  {
    Result rc = csrngInitialize();
    if (R_FAILED(rc))
      return EIO;
    rc = csrngGetRandomBytes(random, sizeof(random));
    csrngExit();
    if (R_FAILED(rc))
      return EIO;
  }
  a.sin_addr.s_addr = gethostid();
  if (!a.sin_addr.s_addr || a.sin_addr.s_addr == INADDR_NONE)
    return ENETDOWN;
#else
  if (getrandom(random, sizeof(random), 0) != sizeof(random))
    return EIO;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
#endif
  if (!inet_ntop(AF_INET, &a.sin_addr, address, sizeof(address)))
    return EIO;
  if (resuming)
    memcpy(key, resume_key, sizeof(key));
  else
    for (i = 0; i < 16; ++i)
      snprintf(key + i * 2, 3, "%02x", random[i]);
  memset(random, 0, sizeof(random));
  listener = socket(AF_INET, SOCK_STREAM, 0);
  if (listener < 0) {
    error = errno ? (unsigned int)errno : EIO;
    goto failed;
  }
  {
    int reuse = 1;
    if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse))) {
      error = errno ? (unsigned int)errno : EIO;
      goto failed;
    }
  }
  if (bind(listener, (struct sockaddr *)&a, sizeof(a)) || listen(listener, 2) ||
      getsockname(listener, (struct sockaddr *)&a, &size) ||
      fcntl(listener, F_SETFL, O_NONBLOCK) < 0) {
    error = errno ? (unsigned int)errno : EIO;
    goto failed;
  }
  bound_port = ntohs(a.sin_port);
  __atomic_store_n(&stop_requested, 0, __ATOMIC_RELEASE);
  n = pthread_attr_init(&attr);
  if (n) {
    error = (unsigned int)n;
    goto failed;
  }
  n = pthread_attr_setstacksize(&attr, 128 * 1024);
  if (!resuming)
    game_phase = 0;
  if (!n)
    n = pthread_create(&worker, &attr, serve, NULL);
  pthread_attr_destroy(&attr);
  if (n) {
    error = (unsigned int)n;
    goto failed;
  }
  running = 1;
  return 0;
failed:
  if (listener >= 0)
    close(listener);
  listener = -1;
  memset(key, 0, sizeof(key));
  return error;
}
int lan_log_url(char *out, size_t cap) {
  int n;
  if (!running) {
    if (cap)
      out[0] = 0;
    return 0;
  }
  n = snprintf(out, cap, "http://%s:%u/#%s", address, bound_port, key);
  return n > 0 && (size_t)n < cap;
}
