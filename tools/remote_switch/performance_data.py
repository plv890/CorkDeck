"""Pure bounded parsing of numeric passive collector reports, no device access."""
import re

def parse(text):
    lines = text[-20480:].splitlines()
    headers = [i for i, line in enumerate(lines) if line.startswith('[PERF] ')]
    result = {}
    if headers:
        start = headers[-1]
        values = dict(re.findall(r'\b(v|generation|active|elapsed_ms|window_ms|continuous|buckets|dropped)=(\d{1,20})\b', lines[start]))
        result = {key: int(value) for key, value in values.items()}
        result['calls'] = []
        for line in lines[start+1:]:
            if not line.startswith('[PERF-CALL] '):
                continue
            kind = re.search(r'\bkind=([a-z-]{1,24})\b', line)
            values = dict(re.findall(r'\b(tid|caller|saved_eip)=([0-9a-f]{1,16})\b', line))
            counts = dict(re.findall(r'\b(n|ns|max_ns|ge1ms|errors)=(\d{1,20})\b', line))
            if not kind or len(values) != 3 or len(counts) != 5:
                continue
            row = {'kind': kind[1], **{key: int(value,16) for key,value in values.items()},
                   **{key: int(value) for key,value in counts.items()}}
            cpu = dict(re.findall(r'\b(cpu_n|cpu_ns)=(\d{1,20})\b',line))
            if len(cpu) == 2:
                row.update({key:int(value) for key,value in cpu.items()})
                row['cpu_available'] = int(cpu['cpu_n']) > 0
            result['calls'].append(row)
            if len(result['calls']) == 8:
                break
        result['partial_or_top_only'] = result.get('buckets',0) > len(result['calls'])
        result['scope'] = 'sampled native wall time; sums overlap and sampling ratios differ'
    clocks = {}
    for line in lines:
        match = re.fullmatch(r'\[PERF-CLOCK\] module=(cpu|gpu|emc) hz=(\d{1,10}) result=([0-9a-f]{8}) source=clkrst-read',line)
        if match:
            clocks[match[1]] = {'hz': int(match[2]), 'result': int(match[3],16),
                               'available': int(match[3],16)==0}
    if clocks:
        result['clocks'] = clocks
    return result
