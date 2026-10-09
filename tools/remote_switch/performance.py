"""Bounded authenticated performance control using the existing LAN service."""
import argparse
import http.client
import json
from pathlib import Path
from credentials import http_key
from transport import lan_ip
from performance_data import parse

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    parser.add_argument('--lan-key-file', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('action', choices=('read', 'start', 'stop'))
    args = parser.parse_args()
    token = http_key(args.lan_key_file)
    connection = http.client.HTTPConnection(lan_ip(args.host), 8181, timeout=5)
    try:
        route = '/perf' if args.action == 'read' else '/perf/' + args.action
        connection.request('GET' if args.action == 'read' else 'POST', route,
                           body=None, headers={'X-Autorun-Key': token})
        response = connection.getresponse()
        body = response.read(20481)
        if response.status != 200 or len(body) > 20480:
            raise RuntimeError('Performance endpoint failed, HTTP status ' + str(response.status))
        text = body.decode('ascii')
        if not text.startswith('[PERF]') or token.lower() in text.lower():
            raise RuntimeError('Unexpected diagnostic response')
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding='ascii')
        args.output.with_suffix('.json').write_text(json.dumps(parse(text),indent=2)+'\n', encoding='ascii')
        print('Performance ' + args.action + ' completed; saved bounded report.')
    finally:
        connection.close()

if __name__ == '__main__':
    main()
