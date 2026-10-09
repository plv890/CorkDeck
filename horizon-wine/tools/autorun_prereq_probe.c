/* Read-only Windows probe, launched as an ordinary executable by Autorun.
 * It never edits registry keys or claims an installer succeeded from exit 0.
 * No child process / fork / new Wine execution mechanism is required. */
#include <windows.h>
#include <winternl.h>
__declspec(dllimport) NTSTATUS NTAPI NtDisplayString(const UNICODE_STRING *);
void *memset(void *p, int c, size_t n) {
  unsigned char *b = p;
  while (n--)
    *b++ = (unsigned char)c;
  return p;
}
void *memcpy(void *p, const void *s, size_t n) {
  unsigned char *b = p;
  const unsigned char *a = s;
  while (n--)
    *b++ = *a++;
  return p;
}
#ifdef _WIN64
#define ARCH 1
#define LABEL "x64"
#define WLABEL L"x64"
#define FOLDER L"C:\\windows\\system32\\"
#define VIEW KEY_WOW64_64KEY
#else
#define ARCH 0
#define LABEL "x86"
#define WLABEL L"x86"
#define FOLDER L"C:\\windows\\system32\\"
#define VIEW KEY_WOW64_32KEY
#endif
static int file_machine(const WCHAR *path) {
  unsigned char dos[64] = {0}, pe[6];
  DWORD read;
  LONG offset;
  HANDLE f;
  f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0,
                  NULL);
  if (f == INVALID_HANDLE_VALUE)
    return 0;
  int ok = ReadFile(f, dos, 64, &read, NULL) && read == 64 && dos[0] == 'M' &&
           dos[1] == 'Z';
  offset = (LONG)((DWORD)dos[60] | ((DWORD)dos[61] << 8) |
                  ((DWORD)dos[62] << 16) | ((DWORD)dos[63] << 24));
  if (ok)
    ok = offset >= 64 &&
         SetFilePointer(f, offset, NULL, FILE_BEGIN) !=
             INVALID_SET_FILE_POINTER &&
         ReadFile(f, pe, 6, &read, NULL) && read == 6 && pe[0] == 'P' &&
         pe[1] == 'E' && !pe[2] && !pe[3] &&
         ((pe[4] | (pe[5] << 8)) == (ARCH ? 0x8664 : 0x14c));
  CloseHandle(f);
  return ok;
}
static int registered_view(DWORD view) {
  HKEY key;
  DWORD installed = 0, major = 0, type = 0, size = sizeof(DWORD);
  LONG status;
  status = RegOpenKeyExW(
      HKEY_LOCAL_MACHINE,
      L"SOFTWARE\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\" WLABEL, 0,
      KEY_QUERY_VALUE | view, &key);
  if (status)
    return 0;
  status = RegQueryValueExW(key, L"Installed", NULL, &type, (BYTE *)&installed,
                            &size);
  int ok = !status && type == REG_DWORD && size == 4 && installed == 1;
  size = sizeof(DWORD);
  status = RegQueryValueExW(key, L"Major", NULL, &type, (BYTE *)&major, &size);
  RegCloseKey(key);
  return ok && !status && type == REG_DWORD && size == 4 && major == 14;
}
static int registered(void) {
  return registered_view(VIEW) ||
         registered_view(VIEW == KEY_WOW64_32KEY ? KEY_WOW64_64KEY
                                                 : KEY_WOW64_32KEY);
}
static void failure(unsigned int code) {
  WCHAR text[] = L"[PREREQUISITES] Windows probe failed, code 00; reopen "
                 L"dependencies and inspect autorun_runtime.log\n";
  text[43] = (WCHAR)(code / 10 + L'0');
  text[44] = (WCHAR)(code % 10 + L'0');
  UNICODE_STRING line = {sizeof(text) - 2, sizeof(text) - 2, text};
  NtDisplayString(&line);
  ExitProcess(code);
}
static unsigned int append(char *out, unsigned int n, const char *text) {
  while (*text && n < 400)
    out[n++] = *text++;
  return n;
}
void __stdcall start(void) {
  const WCHAR *request =
      L"C:\\windows\\AutorunPrerequisites\\probe-" WLABEL L".request";
  const WCHAR *tmp =
      L"C:\\windows\\AutorunPrerequisites\\probe-" WLABEL L".result.tmp";
  const WCHAR *result =
      L"C:\\windows\\AutorunPrerequisites\\probe-" WLABEL L".result";
  char nonce[64], out[420];
  WCHAR report[420];
  DWORD count = 0, written;
  unsigned int n = 0, i;
  HANDLE f;
  f = CreateFileW(request, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                  0, NULL);
  if (f == INVALID_HANDLE_VALUE)
    failure(10);
  int ok = ReadFile(f, nonce, 63, &count, NULL) && count > 0 && count < 63;
  CloseHandle(f);
  if (!ok)
    failure(11);
  nonce[count] = 0;
  for (i = 0; i < count; ++i)
    if ((nonce[i] < '0' || nonce[i] > '9') && nonce[i] != '-')
      failure(12);
  int reg = registered(), files = file_machine(FOLDER L"vcruntime140.dll") &&
                                  file_machine(FOLDER L"msvcp140.dll");
  n = append(out, n, "autorun_prereq_probe=1\narch=");
  out[n++] = ARCH + '0';
  n = append(out, n, "\nrequest=");
  n = append(out, n, nonce);
  n = append(out, n, "\nregistry=");
  out[n++] = reg + '0';
  n = append(out, n, "\nfiles=");
  out[n++] = files + '0';
  out[n++] = '\n';
  f = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
  if (f == INVALID_HANDLE_VALUE)
    failure(13);
  ok = WriteFile(f, out, n, &written, NULL) && written == n &&
       FlushFileBuffers(f);
  CloseHandle(f);
  if (!ok || !MoveFileExW(tmp, result,
                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    failure(14);
  const char *msg =
      reg && files
          ? "[PREREQUISITES] VC14 " LABEL
            " registered and matching PE files found; game compatibility "
            "remains Unknown\n"
          : "[PREREQUISITES] VC14 " LABEL
            " missing or installation incomplete; check VC14 installer log\n";
  for (i = 0; msg[i] && i < 419; ++i)
    report[i] = (unsigned char)msg[i];
  UNICODE_STRING line = {i * 2, i * 2, report};
  NtDisplayString(&line);
  ExitProcess(reg && files ? 0 : 1);
}
