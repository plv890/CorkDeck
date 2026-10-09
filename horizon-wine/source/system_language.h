#ifndef AUTORUN_SYSTEM_LANGUAGE_H
#define AUTORUN_SYSTEM_LANGUAGE_H
#include <string.h>
#ifdef __SWITCH__
#include <switch.h>
#endif
struct nx_language { const char *console, *locale, *steam; };
static inline struct nx_language nx_system_language(void) {
    static const struct nx_language map[] = {
        {"ja","ja-JP","japanese"}, {"en-US","en-US","english"},
        {"en-GB","en-GB","english"}, {"fr","fr-FR","french"},
        {"fr-CA","fr-CA","french"}, {"de","de-DE","german"},
        {"it","it-IT","italian"}, {"es","es-ES","spanish"},
        {"es-419","es-MX","latam"}, {"zh-CN","zh-CN","schinese"},
        {"zh-Hans","zh-CN","schinese"}, {"zh-TW","zh-TW","tchinese"},
        {"zh-Hant","zh-TW","tchinese"}, {"ko","ko-KR","koreana"},
        {"nl","nl-NL","dutch"}, {"pt","pt-PT","portuguese"},
        {"pt-BR","pt-BR","brazilian"}, {"ru","ru-RU","russian"}
    };
    char code[9] = "en-US";
#ifdef __SWITCH__
    u64 value = 0;
    if (R_SUCCEEDED(setInitialize())) {
        if (R_SUCCEEDED(setGetSystemLanguage(&value))) memcpy(code, &value, 8);
        setExit();
    }
#endif
    for (unsigned i = 0; i < sizeof(map)/sizeof(map[0]); ++i)
        if (!strcmp(code, map[i].console)) return map[i];
    return map[1];
}
#endif
