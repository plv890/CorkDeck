"""Initial process inventory only. No attach, execution, memory or system writes."""
import subprocess
from transport import lan_ip


def processes(host):
    command = ['wsl', '-d', 'AvatarLab', '--', 'docker', 'run', '--rm',
               '--network', 'host', 'autorun/switch-dev:2026.09.28.4-fixed',
               '/opt/devkitpro/devkitA64/bin/aarch64-none-elf-gdb', '--nx', '--batch']
    for statement in ('set auto-load off', 'set pagination off', 'set remotetimeout 5',
                      'target extended-remote '+lan_ip(host)+':22225',
                      'info os processes', 'disconnect'):
        command.extend(['-ex', statement])
    result = subprocess.run(command, capture_output=True, timeout=25)
    if len(result.stdout) + len(result.stderr) > 65536:
        raise ValueError('Debugger inventory exceeds limit')
    if result.returncode:
        raise ConnectionError('Debugger inventory failed; no attach attempted')
    return result.stdout + result.stderr
