#ifndef AUTORUN_LAN_LOG_H
#define AUTORUN_LAN_LOG_H
#include <stddef.h>
/* Console-owned lifecycle. No remote control, filesystem writes or credentials.
 * Stop and join before the launcher releases networking to the runtime. */
unsigned int lan_log_start(const char *root, unsigned short port);
void lan_log_stop(void);
void lan_log_network_pause(void);
void lan_log_network_resume(void);
void lan_log_game_phase(void);
int lan_log_url(char *out, size_t capacity);
void lan_log_publish(const char *numeric_status);
/* Optional bounded memory reader. No file handle or raw request is passed.
 * Register before enabling diagnostics; return errno or a NUL-terminated body. */
void lan_log_set_live_reader(int (*reader)(char *out, size_t capacity));
/* Authenticated bounded profiler control; no files, input or memory access. */
void lan_log_set_performance_control(int (*control)(const char *, char *, size_t));
/* Conservative line filter, also exercised by the offline host fixture. */
size_t lan_log_redact(char *out, size_t capacity, const char *in, size_t size);
#endif
