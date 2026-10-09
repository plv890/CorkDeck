#include "steam_probe.h"
#include <curl/curl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define RESPONSE_MAX 65536
struct request {
    unsigned char *body;
    size_t used;
    int overflow;
    steam_cancel_fn cancel;
    void *context;
};
static size_t receive(void *data, size_t size, size_t count, void *opaque)
{
    struct request *r = opaque;
    size_t bytes;
    if (size && count > SIZE_MAX / size) { r->overflow = 1; return 0; }
    bytes = size * count;
    if (bytes > RESPONSE_MAX - r->used) { r->overflow = 1; return 0; }
    memcpy(r->body + r->used, data, bytes);
    r->used += bytes;
    return bytes;
}
static int progress(void *opaque, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d)
{
    struct request *r = opaque;
    (void)a; (void)b; (void)c; (void)d;
    return r->cancel && r->cancel(r->context);
}
static void fetch(struct steam_probe_result *out, struct steam_servers *servers, steam_cancel_fn cancel, void *context)
{
    struct request r = {0};
    CURL *curl;
    CURLcode code;
    memset(out, 0, sizeof(*out));
    out->code = STEAM_MEMORY;
    if (cancel && cancel(context)) { out->code = STEAM_CANCELLED; return; }
    if (!(r.body = malloc(RESPONSE_MAX))) return;
    if (!(curl = curl_easy_init())) { free(r.body); return; }
    r.cancel = cancel; r.context = context;
    out->code = STEAM_NETWORK;
    /* Fixed official origin; no redirects, proxies, cookies, verbose logging,
     * user-supplied URL or error buffer containing remote text. */
#define OPTION(k,v) do { code = curl_easy_setopt(curl, k, v); if (code) goto done; } while (0)
    OPTION(CURLOPT_URL, "https://api.steampowered.com/ISteamDirectory/GetCMListForConnect/v1/?cellid=0&maxcount=8&format=json");
    OPTION(CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
    OPTION(CURLOPT_FOLLOWLOCATION, 0L);
    OPTION(CURLOPT_PROXY, "");
    OPTION(CURLOPT_NETRC, (long)CURL_NETRC_IGNORED);
    OPTION(CURLOPT_SSL_VERIFYPEER, 1L);
    OPTION(CURLOPT_SSL_VERIFYHOST, 2L);
    OPTION(CURLOPT_CONNECTTIMEOUT, 10L);
    OPTION(CURLOPT_TIMEOUT, 20L);
    OPTION(CURLOPT_NOSIGNAL, 1L);
    OPTION(CURLOPT_USERAGENT, "Autorun-Steam-Probe/1");
    OPTION(CURLOPT_WRITEFUNCTION, receive);
    OPTION(CURLOPT_WRITEDATA, &r);
    OPTION(CURLOPT_XFERINFOFUNCTION, progress);
    OPTION(CURLOPT_XFERINFODATA, &r);
    OPTION(CURLOPT_NOPROGRESS, 0L);
    code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &out->http_status);
    if (cancel && cancel(context)) out->code = STEAM_CANCELLED;
    else if (r.overflow) out->code = STEAM_RESPONSE_LIMIT;
    else if (code == CURLE_PEER_FAILED_VERIFICATION || code == CURLE_SSL_CONNECT_ERROR ||
             code == CURLE_SSL_CACERT_BADFILE) out->code = STEAM_TLS;
    else if (code != CURLE_OK) out->code = STEAM_NETWORK;
    else if (out->http_status != 200) out->code = STEAM_HTTP;
    else out->code = (servers ? steam_directory_servers(r.body, r.used, servers) : steam_directory_valid(r.body, r.used)) ? STEAM_OK : STEAM_PROTOCOL;
done:
    out->transport_code = (unsigned int)code;
    curl_easy_cleanup(curl);
    free(r.body);
#undef OPTION
}
void steam_probe(struct steam_probe_result *out, steam_cancel_fn cancel, void *context)
{ fetch(out, NULL, cancel, context); }
void steam_discover(struct steam_probe_result *out, struct steam_servers *servers, steam_cancel_fn cancel, void *context)
{ memset(servers, 0, sizeof(*servers)); fetch(out, servers, cancel, context); }
const char *steam_result_message(enum steam_result code)
{
    switch (code) {
    case STEAM_OK: return "Steam API reachable over verified HTTPS. Not signed in.";
    case STEAM_CANCELLED: return "Connection test cancelled.";
    case STEAM_TLS: return "TLS verification or handshake failed. Check console date and network.";
    case STEAM_HTTP: return "Steam API returned an HTTP error. Try again later.";
    case STEAM_RESPONSE_LIMIT: return "Steam response exceeded the safety limit.";
    case STEAM_PROTOCOL: return "Steam returned an unexpected response.";
    case STEAM_MEMORY: return "Not enough memory to test Steam connection.";
    default: return "Unable to reach Steam. Check Wi-Fi and try again.";
    }
}
