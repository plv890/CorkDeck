#include "steam_family.h"
#include <string.h>
unsigned int steam_family_refresh(struct steam_backend *b, struct steam_library *library) {
    unsigned char body[128];
    struct steam_wire_writer w = {body, sizeof(body), 0, 0};
    struct steam_cm_message m;
    struct steam_wire_reader r;
    struct steam_wire_field f;
    uint64_t group = 0;
    int n, seen = 0;
    steam_wire_uint(&w, 1, b->account);
    steam_wire_uint(&w, 2, 0);
    if (!steam_cm_call(b->cm, STEAM_SERVICE_FAMILY_GROUP, b->account, b->session, ++b->job, body, w.size, &m))
        return 2000 + steam_cm_error(b->cm);
    if (m.result != 1) return 1000 + m.result;
    r = (struct steam_wire_reader){m.body, m.body_size, 0, 0};
    while ((n = steam_wire_next(&r, &f)) == 1) if (f.number == 1) {
        if (seen++ || f.type != 0) return 3301;
        group = f.integer;
    }
    if (n < 0) return 3301;
    if (!group) return 0;
    w = (struct steam_wire_writer){body, sizeof(body), 0, 0};
    steam_wire_fixed64(&w, 1, group);
    steam_wire_uint(&w, 2, 0); /* Own apps were already fetched separately. */
    steam_wire_uint(&w, 3, 0); /* Exclude ineligible games. */
    steam_wire_bytes(&w, 5, b->language, strlen(b->language));
    steam_wire_uint(&w, 7, 0);
    steam_wire_fixed64(&w, 8, b->account);
    if (!steam_cm_call(b->cm, STEAM_SERVICE_FAMILY_LIBRARY, b->account, b->session, ++b->job, body, w.size, &m))
        return 2000 + steam_cm_error(b->cm);
    if (m.result != 1) return 1000 + m.result;
    n = steam_library_merge_family(&m, library);
    return n ? 3300 + (unsigned int)n : 0;
}
