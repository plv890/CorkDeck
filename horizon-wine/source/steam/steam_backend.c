#include "steam_backend.h"
#include <string.h>
int steam_backend_cdn_auth(struct steam_backend *b, uint32_t app,
                           uint32_t depot, const char *host, char token[1024]);
static int raw(struct steam_backend *b, uint32_t type,
               const struct steam_wire_writer *w, struct steam_cm_message *m) {
  if (w->failed || !steam_cm_request(b->cm, type, b->account, b->session,
                                     ++b->job, w->data, w->size, m)) {
    b->error = 2000 + steam_cm_error(b->cm);
    return 0;
  }
  return 1;
}
static int service(struct steam_backend *b, enum steam_service type,
                   const struct steam_wire_writer *w,
                   struct steam_cm_message *m) {
  if (w->failed || !steam_cm_call(b->cm, type, b->account, b->session, ++b->job,
                                  w->data, w->size, m)) {
    b->error = 2000 + steam_cm_error(b->cm);
    return 0;
  }
  if (m->type != 147 || m->result != 1) {
    b->error = 1000 + m->result;
    return 0;
  }
  return 1;
}
int steam_backend_app(struct steam_backend *b, uint32_t id,
                      struct steam_app *out) {
  unsigned char body[128], app[64];
  struct steam_wire_writer w = {body, sizeof(body), 0, 0},
                           a = {app, sizeof(app), 0, 0};
  struct steam_cm_message m;
  struct steam_wire_reader r, s;
  struct steam_wire_field f, v;
  uint64_t token = 0;
  int n;
  b->error = 3001;
  steam_wire_uint(&w, 2, id);
  if (!raw(b, 8905, &w, &m) || m.type != 8906)
    return 0;
  r = (struct steam_wire_reader){m.body, m.body_size, 0, 0};
  while ((n = steam_wire_next(&r, &f)) == 1)
    if (f.number == 3) {
      uint64_t returned = 0, t = 0;
      unsigned int seen = 0;
      if (f.type != 2)
        return 0;
      s = (struct steam_wire_reader){f.data, f.size, 0, 0};
      while (steam_wire_next(&s, &v) == 1) {
        if (v.number == 1) {
          if (v.type != 0 || seen & 1)
            return 0;
          seen |= 1;
          returned = v.integer;
        }
        if (v.number == 2) {
          if (v.type != 0 || seen & 2)
            return 0;
          seen |= 2;
          t = v.integer;
        }
      }
      if (s.failed)
        return 0;
      if (returned == id)
        token = t;
    }
  if (n < 0)
    return 0;
  w = (struct steam_wire_writer){body, sizeof(body), 0, 0};
  steam_wire_uint(&a, 1, id);
  steam_wire_uint(&a, 2, token);
  steam_wire_bytes(&w, 2, app, a.size);
  if (!raw(b, 8903, &w, &m) || m.type != 8904)
    return 0;
  r = (struct steam_wire_reader){m.body, m.body_size, 0, 0};
  while ((n = steam_wire_next(&r, &f)) == 1)
    if (f.number == 1) {
      uint64_t returned = 0, missing = 0;
      const unsigned char *kv = NULL;
      size_t size = 0;
      unsigned int seen = 0;
      if (f.type != 2)
        return 0;
      s = (struct steam_wire_reader){f.data, f.size, 0, 0};
      while (steam_wire_next(&s, &v) == 1) {
        unsigned int bit = v.number == 1   ? 1
                           : v.number == 3 ? 2
                           : v.number == 5 ? 4
                                           : 0;
        if (!bit)
          continue;
        if (seen & bit)
          return 0;
        seen |= bit;
        if (bit == 1) {
          if (v.type != 0)
            return 0;
          returned = v.integer;
        }
        if (bit == 2) {
          if (v.type != 0)
            return 0;
          missing = v.integer;
        }
        if (bit == 4) {
          if (v.type != 2)
            return 0;
          kv = v.data;
          size = v.size;
        }
      }
      if (s.failed)
        return 0;
      if (returned == id) {
        if (missing) {
          b->error = 3002;
          return 0;
        }
        if (!kv || !steam_app_parse_language(kv, size, id, b->language[0] ? b->language : "english", out))
          return 0;
        b->error = 0;
        return 1;
      }
    }
  return 0;
}
int steam_backend_depot(struct steam_backend *b, uint32_t app,
                        const struct steam_depot *depot, unsigned char key[32],
                        uint64_t *code) {
  unsigned char body[128];
  struct steam_wire_writer w = {body, sizeof(body), 0, 0};
  struct steam_cm_message m;
  struct steam_wire_reader r;
  struct steam_wire_field f;
  unsigned int seen = 0;
  uint64_t result = 2, id = 0;
  int n;
  b->error = 3003;
  memset(key, 0, 32);
  *code = 0;
  steam_wire_uint(&w, 1, depot->id);
  steam_wire_uint(&w, 2, app);
  if (!raw(b, 5438, &w, &m) || m.type != 5439)
    return 0;
  r = (struct steam_wire_reader){m.body, m.body_size, 0, 0};
  while ((n = steam_wire_next(&r, &f)) == 1) {
    if (f.number > 3)
      continue;
    if (seen & (1u << f.number))
      goto fail;
    seen |= 1u << f.number;
    if (f.number == 1) {
      if (f.type != 0)
        goto fail;
      result = f.integer;
    }
    if (f.number == 2) {
      if (f.type != 0)
        goto fail;
      id = f.integer;
    }
    if (f.number == 3) {
      if (f.type != 2 || f.size != 32)
        goto fail;
      memcpy(key, f.data, 32);
    }
  }
  if (result != 1) {
    b->error = 1000 + (unsigned int)result;
    goto fail;
  }
  if (n < 0 || seen != 14 || id != depot->id)
    goto fail;
  w = (struct steam_wire_writer){body, sizeof(body), 0, 0};
  steam_wire_uint(&w, 1, app);
  steam_wire_uint(&w, 2, depot->id);
  steam_wire_uint(&w, 3, depot->manifest);
  if (!service(b, STEAM_SERVICE_MANIFEST_CODE, &w, &m))
    goto fail;
  r = (struct steam_wire_reader){m.body, m.body_size, 0, 0};
  seen = 0;
  while ((n = steam_wire_next(&r, &f)) == 1)
    if (f.number == 1) {
      if (seen++ || f.type != 0)
        goto fail;
      *code = f.integer;
    }
  if (n < 0 || !*code)
    goto fail;
  b->error = 0;
  return 1;
fail:
  steam_secret_clear(key, 32);
  *code = 0;
  return 0;
}
/* A content server is whichever host Steam names over the signed-in session:
 * Valve's own under steamcontent.com in some regions, a contracted network's
 * (another domain) in others. It is taken for what it is, a plain DNS name;
 * what it serves is encrypted with the depot key and every chunk is checked
 * against the manifest, so the name is not what the content is trusted by. */
static int hostname(const char *s) {
  size_t n = strlen(s), i, dots = 0;
  if (n < 4 || n >= 128 || s[0] == '.' || s[0] == '-' || s[n - 1] == '.' ||
      s[n - 1] == '-')
    return 0;
  for (i = 0; i < n; ++i) {
    if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9') ||
          s[i] == '.' || s[i] == '-'))
      return 0;
    if (s[i] == '.' && (++dots, s[i - 1] == '.'))
      return 0;
  }
  return dots > 0;
}
int steam_backend_servers(struct steam_backend *b, char hosts[8][128],
                          size_t *count) {
  unsigned char body[16];
  struct steam_wire_writer w = {body, sizeof(body), 0, 0};
  struct steam_cm_message m;
  struct steam_wire_reader r, s;
  struct steam_wire_field f, v;
  int n;
  unsigned int plain = 0, tls = 0;
  *count = 0;
  b->error = 3004;
  /* Routing hint issued by authenticated CM logon, never a license. */
  steam_wire_uint(&w, 1, b->cell_id);
  steam_wire_uint(&w, 2, 20);
  if (!service(b, STEAM_SERVICE_SERVERS, &w, &m))
    return 0;
  r = (struct steam_wire_reader){m.body, m.body_size, 0, 0};
  while ((n = steam_wire_next(&r, &f)) == 1)
    if (f.number == 1) {
      char host[128] = {0}, virtual[128] = {0}, type[16] = {0}, https[16] = {0};
      unsigned int seen = 0;
      int unusable = 0;
      size_t i;
      if (f.type != 2)
        return 0;
      s = (struct steam_wire_reader){f.data, f.size, 0, 0};
      /* An entry this cannot use is passed over; it does not spoil the list.
       * Passed over: a server for Steam China only, a proxy, one kept for a
       * list of applications, and one reached under another name than its
       * address (the request here carries one name). */
      while (steam_wire_next(&s, &v) == 1) {
        unsigned int bit = v.number == 1    ? 1
                           : v.number == 8  ? 2
                           : v.number == 12 ? 4
                           : v.number == 9  ? 8
                                            : 0;
        char *out;
        size_t cap;
        if ((v.number == 7 || v.number == 10) && v.type == 0 && v.integer)
          unusable = 1;
        if (v.number == 13)
          unusable = 1;
        if (!bit)
          continue;
        if (v.type != 2 || seen & bit) {
          unusable = 1;
          continue;
        }
        seen |= bit;
        out = bit == 1 ? type : bit == 2 ? host : bit == 4 ? https : virtual;
        cap = bit == 2 || bit == 8 ? sizeof(host) : sizeof(type);
        if (v.size >= cap || memchr(v.data, 0, v.size)) {
          unusable = 1;
          continue;
        }
        memcpy(out, v.data, v.size);
      }
      if (s.failed)
        return 0;
      for (i = 0; host[i]; ++i)
        if (host[i] >= 'A' && host[i] <= 'Z')
          host[i] += 'a' - 'A';
      for (i = 0; virtual[i]; ++i)
        if (virtual[i] >= 'A' && virtual[i] <= 'Z')
          virtual[i] += 'a' - 'A';
      if (virtual[0] && strcmp(virtual, host))
        unusable = 1;
      /* Valve's own servers are "SteamCache", a contracted network's "CDN";
       * the Steam client downloads from both.
       * Depot content is encrypted with the depot key and every chunk is
       * checked against the manifest, so the Steam client fetches it over
       * plain HTTP wherever the server allows; so does this installer. */
      if (!unusable && (!strcmp(type, "CDN") || !strcmp(type, "SteamCache")) &&
          hostname(host) && *count < 8) {
        if (strcmp(https, "mandatory"))
          plain |= 1u << *count;
        if (!strcmp(https, "mandatory") || !strcmp(https, "optional"))
          tls |= 1u << *count;
        strcpy(hosts[(*count)++], host);
      }
    }
  if (n < 0 || !*count)
    return 0;
  b->plain_hosts = plain;
  b->tls_hosts = tls;
  b->error = 0;
  return 1;
}
int steam_backend_cdn_auth(struct steam_backend *b, uint32_t app,
                           uint32_t depot, const char *host, char token[1024]) {
  unsigned char body[256];
  struct steam_wire_writer w = {body, sizeof(body), 0, 0};
  struct steam_cm_message m;
  struct steam_wire_reader r;
  struct steam_wire_field f;
  int seen = 0, n;
  token[0] = 0;
  if (!hostname(host)) {
    b->error = 3004;
    return 0;
  }
  steam_wire_uint(&w, 1, depot);
  steam_wire_bytes(&w, 2, host, strlen(host));
  steam_wire_uint(&w, 3, app);
  if (!service(b, STEAM_SERVICE_CDN_AUTH, &w, &m))
    return 0;
  r = (struct steam_wire_reader){m.body, m.body_size, 0, 0};
  while ((n = steam_wire_next(&r, &f)) == 1)
    if (f.number == 1) {
      if (seen++ || f.type != 2 || f.size >= 1024 ||
          memchr(f.data, 0, f.size)) {
        b->error = 3004;
        return 0;
      }
      memcpy(token, f.data, f.size);
      token[f.size] = 0;
    }
  if (n < 0 || !seen) {
    steam_secret_clear(token, 1024);
    b->error = 3004;
    return 0;
  }
  return 1;
}
