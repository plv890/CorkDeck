"""The console's clocks are its owner's: nothing in the application may set them.

Fails when the fork's own sources name a call or a service that changes a clock,
a governor or the system's performance mode. Reading them (clkrstGetClockRate,
Horizon OC's report) is not forbidden; the read-only uses are in performance.c.
"""
import re, sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
forbidden = [
    r'clkrstSetClockRate', r'pcvSetClockRate', r'pcvSetVoltage', r'\bpcvSet\w+',
    r'appletSetCpuBoostMode', r'apmSetPerformanceConfiguration', r'apmSetPerformanceMode', r'apmSysSet\w+',
    r'ApmCpuBoostMode_', r'sysclkIpcSet\w+', r'hocclkIpcSet\w+', r'rgltrSetVoltage\w*',
]
# Horizon OC's service may be asked what it reports and nothing else: its commands
# that change a profile or an override (6 and up, apart from the list of clocks, 11).
service_write = re.compile(r'serviceDispatch(?:In|InOut)?\s*\(\s*&\w+\s*,\s*(\d+)')
allowed_commands = {'0', '2', '5', '11'}
found = []
for path in sorted((root / 'source').rglob('*')):
    if path.suffix not in ('.c', '.h', '.cpp', '.inc', '.S') or not path.is_file():
        continue
    text = path.read_text(encoding='utf-8', errors='replace')
    for pattern in forbidden:
        for match in re.finditer(pattern, text):
            line = text.count('\n', 0, match.start()) + 1
            found.append('%s:%d: %s' % (path.relative_to(root), line, match.group(0)))
    if '"hoc:clk"' in text or '"sys:clk"' in text:
        for match in service_write.finditer(text):
            if match.group(1) not in allowed_commands:
                line = text.count('\n', 0, match.start()) + 1
                found.append('%s:%d: a command of the clock service that is not a reading (%s)' % (
                    path.relative_to(root), line, match.group(1)))
if found:
    print('the application must not set the console\'s clocks:')
    print('\n'.join(found))
    sys.exit(1)
print('No clock, governor, voltage or performance mode is set anywhere in source/.')
