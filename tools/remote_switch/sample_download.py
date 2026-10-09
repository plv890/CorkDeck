"""Bounded read-only download benchmark; no screenshots or credentials in output."""
import argparse,json,re,time
from pathlib import Path
from credentials import http_key
from diagnostics import Diagnostics,DiagnosticsHTTPError

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',required=True)
    parser.add_argument('--lan-key-file',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--seconds',type=int,default=600)
    args=parser.parse_args()
    if not 1<=args.seconds<=1800:parser.error('seconds must be 1..1800')
    args.output.mkdir(parents=True,exist_ok=True)
    client=Diagnostics(args.host,http_key(args.lan_key_file),15)
    started=time.monotonic(); sequence=0
    with (args.output/'samples.jsonl').open('a',encoding='utf-8') as target:
        while time.monotonic()-started<args.seconds:
            status=client.get('/status').decode('ascii')
            target.write(json.dumps({'time':time.time(),'status':status})+'\n');target.flush()
            # Numeric history already filters secrets on the server. Keep the
            # latest tail and snapshots so rollover cannot erase our evidence.
            if sequence%5==0 or re.search(r'state=(6|7|8|10)\b',status):
                try:
                    history=client.get('/tail/steam-download.log')
                except DiagnosticsHTTPError as error:
                    if error.status != 404: raise
                else:
                    (args.output/f'history-{sequence:04d}.log').write_bytes(history)
                print(status,flush=True)
            if re.search(r'appid=[1-9]\d* state=(6|7|8|10)\b',status):break
            sequence+=1;time.sleep(2)
if __name__=='__main__':main()
