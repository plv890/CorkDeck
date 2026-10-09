#define _POSIX_C_SOURCE 200809L
#include "steam_settings.h"
#include "steam_file.h"
#include "../system_language.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
const struct steam_language steam_languages[] = {
    {"english", "English"}, {"russian", "Russian"}, {"german", "German"},
    {"french", "French"}, {"spanish", "Spanish - Spain"},
    {"latam", "Spanish - Latin America"}, {"italian", "Italian"},
    {"portuguese", "Portuguese"}, {"brazilian", "Portuguese - Brazil"},
    {"polish", "Polish"}, {"ukrainian", "Ukrainian"}, {"czech", "Czech"},
    {"turkish", "Turkish"}, {"dutch", "Dutch"}, {"swedish", "Swedish"},
    {"danish", "Danish"}, {"norwegian", "Norwegian"}, {"finnish", "Finnish"},
    {"romanian", "Romanian"}, {"hungarian", "Hungarian"},
    {"greek", "Greek"}, {"bulgarian", "Bulgarian"}, {"arabic", "Arabic"},
    {"japanese", "Japanese"}, {"koreana", "Korean"},
    {"schinese", "Chinese - Simplified"}, {"tchinese", "Chinese - Traditional"},
    {"thai", "Thai"}, {"vietnamese", "Vietnamese"}, {"indonesian", "Indonesian"}
};
const size_t steam_language_count = sizeof(steam_languages) / sizeof(*steam_languages);
uint64_t steam_language_mask(const char *csv) {
    uint64_t mask = 0;
    while (*csv) {
        const char *end = strchr(csv, ',');
        const char *stop = end ? end : csv + strlen(csv);
        size_t i;
        while (csv < stop && (unsigned char)*csv <= 32) ++csv;
        while (stop > csv && (unsigned char)stop[-1] <= 32) --stop;
        for (i = 0; i < steam_language_count && i < 63; ++i) {
            const char *id = steam_languages[i].id;
            size_t j, n = (size_t)(stop - csv);
            if (strlen(id) != n) continue;
            for (j = 0; j < n; ++j) {
                unsigned char c = (unsigned char)csv[j];
                if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
                if (c != (unsigned char)id[j]) break;
            }
            if (j == n) mask |= UINT64_C(1) << i;
        }
        if (!end) break;
        csv = end + 1;
    }
    return mask;
}
int steam_language_valid(const char *id) {
    size_t i;
    for (i = 0; i < steam_language_count; ++i)
        if (!strcmp(id, steam_languages[i].id)) return 1;
    return 0;
}
static int filename(const char *root, char out[768], int temporary) {
    int n = snprintf(out, 768, "%s/steam-settings.txt%s", root, temporary ? ".tmp" : "");
    struct stat st;
    if (n < 0 || n >= 768) return 0;
    return lstat(out, &st) ? errno == ENOENT : S_ISREG(st.st_mode) && !S_ISLNK(st.st_mode);
}
int steam_settings_load(const char *root, char language[32]) {
    char p[768], line[64];
    FILE *f;
    strcpy(language, nx_system_language().steam);
    if (!filename(root, p, 0) || !steam_file_recover(p) || !(f = fopen(p, "rb"))) return 0;
    if (!fgets(line, sizeof(line), f)) { fclose(f); return 0; }
    line[strcspn(line, "\r\n")] = 0;
    fclose(f);
    if (!strcmp(line, "language=system")) return 2;
    if (strncmp(line, "language=", 9) || !steam_language_valid(line + 9)) return 0;
    strcpy(language, line + 9);
    return 1;
}
unsigned int steam_settings_concurrency(const char *root){
    char p[768],line[64];unsigned int count=STEAM_CONNECTIONS_DEFAULT;FILE *f;
    if(!filename(root,p,0)||!steam_file_recover(p)||!(f=fopen(p,"rb")))return count;
    while(fgets(line,sizeof(line),f)){
        unsigned int n;char extra;
        if(sscanf(line,"download-connections=%u %c",&n,&extra)==1&&steam_connections_valid(n))count=n;
    }
    fclose(f);return count;
}
static int save(const char *root,const char *language,unsigned int concurrency) {
    char p[768], tmp[768];
    FILE *f;
    int ok;
    if ((!steam_language_valid(language) && strcmp(language,"system")) || !steam_connections_valid(concurrency) || !filename(root, p, 0) || !filename(root, tmp, 1)) return 0;
    f = fopen(tmp, "wb");
    if (!f) return 0;
    ok = fprintf(f, "language=%s\ndownload-connections=%u\n", language,concurrency) > 0 && !fflush(f) && !fsync(fileno(f));
    if (fclose(f)) ok = 0;
    if (ok) ok = steam_file_replace(tmp, p);
    if (!ok) unlink(tmp);
    return ok;
}
int steam_settings_save(const char *root,const char *language){return save(root,language,steam_settings_concurrency(root));}
int steam_settings_save_concurrency(const char *root,unsigned int count){char language[32];if(steam_settings_load(root,language)!=1)strcpy(language,"system");return save(root,language,count);}
