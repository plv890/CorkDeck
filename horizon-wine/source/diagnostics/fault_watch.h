#ifndef WINE_NX_FAULT_WATCH_H
#define WINE_NX_FAULT_WATCH_H
#include <stddef.h>
#include <ctype.h>
#include <string.h>
/* Recognized explicit runtime failures only; ordinary err/fixme messages are
 * not fatal. Numeric kinds avoid retaining dialog contents or credentials. */
static inline int wine_nx_fault_contains(const char *line, const char *word)
{
    size_t i, j, n = strlen(line), m = strlen(word);
    for (i = 0; i + m <= n; i++)
    {
        for (j = 0; j < m && tolower((unsigned char)line[i+j]) == word[j]; j++) {}
        if (j == m) return 1;
    }
    return 0;
}
static inline int wine_nx_fault_kind(const char *line)
{
    if (wine_nx_fault_contains(line, "fatal error in gc")) return 1;
    if (wine_nx_fault_contains(line, "rendering thread exception")) return 2;
    if (wine_nx_fault_contains(line, "failed to initialize, aborting")) return 3;
    if (wine_nx_fault_contains(line, "unhandled exception") ||
        wine_nx_fault_contains(line, "unhandled page fault")) return 4;
    {
        static const char prefix[] = "[EXC] unhandled status=0x";
        if (!strncmp(line, prefix, sizeof(prefix)-1)) {
            const char *code = line + sizeof(prefix)-1;
            size_t i;
            for (i=0; i<8 && isxdigit((unsigned char)code[i]); i++) {}
            if (i==8 && !strncmp(code+8, "; parking thread", 16)) return 4;
        }
    }
    if (!strncmp(line, "[FAIL]", 6)) return 5;
    if (wine_nx_fault_contains(line, "err:dialog:") &&
        wine_nx_fault_contains(line, "fatal error")) return 6;
    {
        static const char prefix[] = "[EXIT] NtTerminateProcess(self) exit_code=";
        static const char *codes[] = {"0xc0000005", "0xc000001d", "0xc00000fd",
                                     "0xc0000409", "0xc0000094", "0xc000008e",
                                     "0xc0000135", "0xc0000142"};
        size_t i;
        if (!strncmp(line, prefix, sizeof(prefix)-1))
        {
            const char *status = line + sizeof(prefix)-1;
            for (i=0;i<sizeof(codes)/sizeof(codes[0]);i++)
                if (!strncmp(status,codes[i],10) && !isxdigit((unsigned char)status[10])) return 7;
        }
    }
    return 0;
}
#endif
