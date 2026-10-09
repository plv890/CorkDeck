"""Apply a small reproducible overlay to an exact official v2.5 checkout."""
from pathlib import Path
import shutil
import subprocess
import sys

PIN = '45b6a6419624ed1a1f34aabc9f37336d5f130e61'
root = Path(sys.argv[1]).resolve()
head = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
if head != PIN:
    raise SystemExit('Unexpected upstream commit')
main = root / 'sys-botbase/source/main.c'
original = subprocess.check_output(['git', '-C', str(root), 'show', 'HEAD:sys-botbase/source/main.c']).decode()
text = original.replace('#include "freeze.h"', '#include "freeze.h"\n#include <sys/time.h>\nint remote_files(int argc, char **argv);')
text = text.replace('    //peek <address', '    if (!strncmp(argv[0], "ar", 2)) return remote_files(argc, argv);\n\n    //peek <address', 1)
text = text.replace('add_to_pfds(&pfds, newfd, &fd_count, &fd_size);', '''struct timeval receiveTimeout = { .tv_sec = 2, .tv_usec = 0 };
                        setsockopt(newfd, SOL_SOCKET, SO_RCVTIMEO, &receiveTimeout, sizeof(receiveTimeout));
                        add_to_pfds(&pfds, newfd, &fd_count, &fd_size);''')
text = text.replace('int len = recv(pfds[i].fd, &linebuf[readBytesSoFar], 1, 0);', '''int len = readBytesSoFar < MAX_LINE_LENGTH - 1
                            ? recv(pfds[i].fd, &linebuf[readBytesSoFar], 1, 0) : -1;''')
text = text.replace('if (echoCommands) {', 'if (echoCommands && strncmp(linebuf, "ar", 2)) {')
text = text.replace('printf("%s\\n", linebuf);\n                                }', 'printf("%s\\n", linebuf);\n                                }\n                                fflush(stdout);')
if text == original or 'remote_files(argc, argv)' not in text:
    raise SystemExit('Overlay anchor missing')
main.write_text(text, encoding='utf-8', newline='\n')
shutil.copyfile(Path(__file__).with_name('remote_files.c'), main.with_name('remote_files.c'))
print('Pinned overlay prepared; build and hardware validation still required')
