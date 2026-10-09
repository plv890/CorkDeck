"""Parse only the fixed numeric RPC diagnostic schema, never arbitrary log data.

The old native formatter called its generation a 'token', so generic log
redaction removed these non-secret rows. This parser keeps typed metadata only;
it must not be used to exempt raw token-bearing lines from normal redaction.
"""
import re

ROW = re.compile(
    r'\[REQUEST-PENDING\] (?:token|rpc_seq)=(\d+) role=([cs]) tid=([0-9a-f]+) '
    r'handle=([0-9a-f]+) req=(\d+) name=([A-Za-z0-9_]{0,39}) age_ms=(\d+) '
    r'flags=([0-9a-f]+) timeout=(-?\d+) words=([0-9a-f]+(?:,[0-9a-f]+){4}) '
    r'caller=([0-9a-f]+) dropped=(\d+)'
)

def parse_row(line):
    if len(line) > 512:
        return None
    match = ROW.fullmatch(line.rstrip('\r\n'))
    if not match:
        return None
    seq,role,tid,handle,req,name,age,flags,timeout,words,caller,dropped=match.groups()
    result={'call_id':int(seq),'role':role,'tid':int(tid,16),'handle':int(handle,16),
            'request':int(req),'name':name,'age_ms':int(age),'flags':int(flags,16),
            'timeout':int(timeout),'words':[int(x,16) for x in words.split(',')],
            'caller':int(caller,16),'dropped':int(dropped)}
    if not 0 < result['call_id'] < 2**64 or not -(2**63) <= result['timeout'] < 2**63:
        return None
    if result['age_ms'] >= 2**64 or result['caller'] >= 2**64 or result['dropped'] >= 2**64:
        return None
    if any(result[k] >= 2**32 for k in ['tid','handle','request','flags']) or any(x>=2**32 for x in result['words']):
        return None
    return result

def latest_requests(data):
    """Last retained observation for each role/TID/kernel handle; <=256 rows."""
    rows={}
    for raw in data.splitlines():
        if len(raw)>512:
            continue
        try:line=raw.decode('ascii')
        except UnicodeDecodeError:continue
        row=parse_row(line)
        if row:
            key=(row['role'],row['tid'],row['handle'])
            if key in rows or len(rows)<256:rows[key]=row
    return sorted(rows.values(),key=lambda row:(row['tid'],row['role'],row['handle']))
