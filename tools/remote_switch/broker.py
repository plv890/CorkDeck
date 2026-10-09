"""Local single-action CLI. Reports are local, contain no LAN token."""
import argparse
import http.client
import ftplib
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import sys
import time
import subprocess
import uuid
from controller import BUTTONS, XINPUT_FACE, Controller
from diagnostics import Diagnostics
from transport import BotTransport
from transfer import stage_nro, activate_test
from ftp_logs import collect as collect_ftp_logs
from debugger import processes as debug_processes
from direct_files import DirectFiles
from credentials import http_key
from navigation import dbi_ftp_sd, close_title


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True, help='Switch trusted LAN IPv4')
    parser.add_argument('--port', type=int, default=6000)
    parser.add_argument('--output', type=Path, default=Path(__file__).parent / 'runs')
    parser.add_argument('--timeout', type=float, default=5)
    parser.add_argument('--ftp-credentials', type=Path, help='Windows user DPAPI file path; never a password')
    parser.add_argument('--lan-key-file', type=Path, help='Windows user DPAPI file with temporary HTTP key')
    sub = parser.add_subparsers(dest='action', required=True)
    for action in ('probe', 'screenshot', 'logs', 'live-logs', 'detach', 'gdb-processes', 'direct-probe'):
        sub.add_parser(action)
    live_logs = sub.add_parser('direct-logs')
    live_logs.add_argument('--live', action='store_true', help='Only closed startup/progress snapshots; original sysmodule supported')
    direct_stage = sub.add_parser('direct-stage-test')
    direct_stage.add_argument('nro', type=Path)
    direct_activate = sub.add_parser('direct-activate-test')
    direct_activate.add_argument('sha256')
    sub.add_parser('album-homebrew', help='Fixed R+A chord; requires visibly selected HOME Album')
    navigation = sub.add_parser('navigate', help='Verified fixed route; screenshots at start/end only')
    navigation.add_argument('route', choices=('dbi-ftp-sd', 'close-title'))
    navigation.add_argument('--expected-title', required=True)
    navigation.add_argument('--start-confirmed', action='store_true', required=True,
                            help='Starting screen was confirmed; DBI route starts on main-menu first row')
    home = sub.add_parser('home-primed', help='Fixed upstream UNUSED + HOME workaround')
    home.add_argument('--expected-title', required=True)
    click = sub.add_parser('click')
    click.add_argument('button', choices=BUTTONS)
    click.add_argument('--ready', action='store_true', help='Wait for virtual device discovery before a bounded press')
    click.add_argument('--layout', choices=('nintendo', 'xinput'), default='nintendo',
                       help='Face-button names in the game prompt; HOME/launcher use nintendo')
    click.add_argument('--expected-title', required=True,
                       help='Exact title ID from probe, or none when no application is running')
    stage = sub.add_parser('stage-test')
    stage.add_argument('nro', type=Path)
    stage.add_argument('--ftp-port', type=int, default=5000)
    activate = sub.add_parser('activate-test')
    activate.add_argument('sha256')
    activate.add_argument('--dbi-title', required=True)
    activate.add_argument('--ftp-port', type=int, default=5000)
    ftp_logs = sub.add_parser('ftp-logs')
    ftp_logs.add_argument('--ftp-port', type=int, default=5001)
    ftp_logs.add_argument('--file', action='append', dest='log_files', help='Select up to four saved .log files')
    ftp_logs.add_argument('--whole', action='store_true', help='Read selected logs including startup; max 512 KiB each')
    args = parser.parse_args()
    if args.ftp_credentials:
        os.environ['AUTORUN_FTP_CREDENTIALS'] = str(args.ftp_credentials.resolve())
    transport = BotTransport(args.host, args.port, args.timeout)
    controller = Controller(transport)
    run_id = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ-') + uuid.uuid4().hex[:8]
    folder = args.output.resolve() / run_id
    folder.mkdir(parents=True)
    record = {'run_id': run_id, 'environment': 'Switch LAN (not host fixture)',
              'action': args.action, 'started_utc': datetime.now(timezone.utc).isoformat(),
              'hardware_result': 'Unknown'}
    started = time.monotonic()
    try:
        if args.action == 'probe':
            record['bot_version'] = controller.version()
            record['title_id'] = controller.title()
            record['hardware_result'] = 'Read response received'
        elif args.action == 'direct-probe':
            DirectFiles(transport).version()
            record['hardware_result'] = 'File extension version 1 received'
        elif args.action == 'album-homebrew':
            if controller.title() is not None:
                raise ValueError('Application running; launch chord stopped')
            (folder / 'before.jpg').write_bytes(controller.screenshot())
            controller.album_homebrew()
            (folder / 'after.jpg').write_bytes(controller.screenshot())
            record['hardware_result'] = 'Fixed R+A chord sent and release attempted; inspect screenshot'
        elif args.action == 'home-primed':
            expected = None if args.expected_title.lower() == 'none' else f'{int(args.expected_title,16):016X}'
            if controller.title() != expected:
                raise ValueError('Active title changed; HOME stopped')
            (folder / 'before.jpg').write_bytes(controller.screenshot())
            controller.home_primed()
            (folder / 'after.jpg').write_bytes(controller.screenshot())
            record['hardware_result'] = 'UNUSED + HOME sent on one device; inspect screenshot'
        elif args.action == 'navigate':
            expected = f'{int(args.expected_title, 16):016X}'
            if controller.title() != expected:
                raise ValueError('Active title changed; navigation stopped')
            (folder / 'before.jpg').write_bytes(controller.screenshot())
            if args.route == 'dbi-ftp-sd':
                dbi_ftp_sd(controller)
            elif args.route == 'close-title':
                close_title(controller, expected)
            (folder / 'after.jpg').write_bytes(controller.screenshot())
            record['route'] = args.route
            record['hardware_result'] = 'Bounded navigation completed; inspect final screenshot'
        elif args.action == 'direct-logs':
            record['collection'] = DirectFiles(transport).collect(folder, live=args.live)
            record['partial'] = any(item['status'] != 'received' for item in record['collection'])
            record['hardware_result'] = 'Bounded saved-log tails collected through sys-botbase'
        elif args.action == 'direct-stage-test':
            record['transfer'] = DirectFiles(transport).stage(args.nro)
            record['hardware_result'] = 'Separate test NRO staged and read-back verified'
        elif args.action == 'direct-activate-test':
            record['transfer'] = DirectFiles(transport).activate(args.sha256)
            record['hardware_result'] = 'Separate test NRO activated; previous target retained'
        elif args.action == 'screenshot':
            (folder / 'screen.jpg').write_bytes(controller.screenshot())
            record['hardware_result'] = 'JPEG response received; inspect image'
        elif args.action in ('logs', 'live-logs'):
            diagnostics = Diagnostics(args.host, http_key(args.lan_key_file), args.timeout)
            if args.action == 'live-logs':
                record['collection'] = diagnostics.collect_live(folder)
            else:
                record['collection'] = diagnostics.collect(folder)
            unavailable = sum(item['status'] != 'received' for item in record['collection'])
            record['hardware_result'] = f'Log collection completed; unavailable files: {unavailable}'
            record['partial'] = bool(unavailable)
        elif args.action == 'stage-test':
            record['transfer'] = stage_nro(args.host, args.nro, args.ftp_port, args.timeout)
            record['hardware_result'] = 'Test NRO staged and read back; not installed or launched'
        elif args.action == 'activate-test':
            expected = f'{int(args.dbi_title, 16):016X}'
            if controller.title() != expected:
                raise ValueError('Foreground DBI not confirmed; no replacement')
            record['transfer'] = activate_test(args.host, args.sha256, args.ftp_port, args.timeout)
            record['hardware_result'] = 'Separate probe target activated; previous test retained'
        elif args.action == 'ftp-logs':
            record['collection'] = collect_ftp_logs(args.host, folder, args.ftp_port, args.timeout,
                                                   selected=args.log_files, whole=args.whole)
            record['partial'] = any(item['status'] != 'received' for item in record['collection'])
            record['hardware_result'] = 'Direct bounded saved-log collection completed'
        elif args.action == 'gdb-processes':
            (folder / 'processes.txt').write_bytes(debug_processes(args.host))
            record['hardware_result'] = 'Process inventory received; no attach attempted'
        elif args.action == 'click':
            expected = None if args.expected_title.lower() == 'none' else f'{int(args.expected_title, 16):016X}'
            if controller.title() != expected:
                raise ValueError('Active title changed; input stopped')
            (folder / 'before.jpg').write_bytes(controller.screenshot())
            try:
                button = XINPUT_FACE.get(args.button, args.button) if args.layout == 'xinput' else args.button
                if args.ready:
                    controller.click_ready(button)
                else:
                    controller.click(button)
                record['device_ready_wait'] = args.ready
                record['requested_button'] = args.button
                record['button_layout'] = args.layout
                record['button'] = button
                time.sleep(0.7)
                (folder / 'after.jpg').write_bytes(controller.screenshot())
                record['hardware_result'] = 'Input sent; compare before/after images'
            finally:
                controller.detach()
        else:
            controller.detach()
            record['hardware_result'] = 'Detach sent; no execution acknowledgement'
        record['status'] = 'ok'
    except (OSError, ValueError, UnicodeError, http.client.HTTPException,
            ftplib.Error, EOFError, subprocess.SubprocessError) as exc:
        record['status'] = 'failed'
        # Do not persist arbitrary protocol responses, URLs, headers or env values.
        record['error_type'] = type(exc).__name__
        print(f'{args.action} failed ({type(exc).__name__}); queue stopped', file=sys.stderr)
    finally:
        record['elapsed_seconds'] = round(time.monotonic() - started, 3)
        (folder / 'action.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
        print(folder)
    return 0 if record['status'] == 'ok' else 1


if __name__ == '__main__':
    sys.exit(main())
