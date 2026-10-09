"""Reversible two-game hardware trial of our source-built minimal SDK layer.
Reads only owned installation markers/original SDK DLLs. No vault/key access.
"""
from pathlib import Path
import datetime
import ftplib
import hashlib
import io
import json
import re
import sys
from pe import inspect
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'remote_switch'))
from controller import Controller
from transport import BotTransport
from navigation import sequence, DBI_TITLE

root=Path(__file__).resolve().parents[3]/'artifacts/steamworks-minimal'
manifest=json.loads((root/'build.json').read_text())
if manifest.get('state')!='complete':raise RuntimeError('Build incomplete')
for name,digest in manifest['source_hashes'].items():
    if hashlib.sha256((Path(__file__).parent/name).read_bytes()).hexdigest()!=digest:raise RuntimeError('Stale build')
references=json.loads((root/'reference/references.json').read_text())
sdk={r['file']:r for r in references}
for r in manifest['files']:
    if hashlib.sha256((root/r['file']).read_bytes()).hexdigest()!=r['sha256']:raise RuntimeError('Wrong payload hash')
exports=set(inspect(root/'steam_api64.dll')['exports'])
missing=set(inspect(root/'reference/Balatro-luasteam.dll')['imports']['steam_api64.dll'])-exports
if missing:raise RuntimeError('Missing imports: '+str(missing))

def read(ftp,path,limit=2*1024*1024):
    data=bytearray()
    def add(b):
        if len(data)+len(b)>limit:raise ValueError('Bound exceeded')
        data.extend(b)
    ftp.retrbinary('RETR '+path,add);return bytes(data)

def absent(ftp,path):
    try:read(ftp,path)
    except ftplib.error_perm as e:
        if str(e).startswith('550'):return
        raise
    raise RuntimeError('Existing file must be preserved: '+path)

records=[]
controller=Controller(BotTransport('192.168.70.59',6000,15))
sequence(controller,DBI_TITLE,('A','DDOWN','DDOWN','A'))
try:
    with ftplib.FTP() as ftp:
        ftp.connect('192.168.70.59',5000,15);ftp.login()
        for label,refname,basename,game in (
            ('Balatro','Balatro-steam_api64.dll','steam_api64.dll','/switch/wine/drive_c/Steam/Balatro'),
            ('Dishonored','Dishonored-steam_api.dll','steam_api.dll','/switch/wine/drive_c/Steam/Dishonored RHCP')):
            ref=sdk[refname];path=ref['path'];parent=path.rsplit('/',1)[0]
            marker=read(ftp,game+'/autorun-steam-install.ini',16384).decode('utf-8')
            fields=dict(line.split('=',1) for line in marker.splitlines() if '=' in line)
            if fields.get('autorun_steam_install')!='1':raise RuntimeError('Not a registered Steam installation')
            appid=int(fields['appid']);account=int(fields['account'])
            if appid<=0 or account<=0:raise RuntimeError('Invalid owner/AppID context')
            old=read(ftp,path)
            if hashlib.sha256(old).hexdigest()!=ref['sha256']:raise RuntimeError('Original SDK changed since reference')
            backup=path+'.autorun-original';config=parent+'/autorun-steam.ini'
            absent(ftp,backup);absent(ftp,config)
            new=(root/basename).read_bytes()
            lines=['[autorun]',f'app_id={appid}',f'steam_id={account}']
            prefixes={'user':'SteamUser','apps':'STEAMAPPS_INTERFACE_VERSION','utils':'SteamUtils','friends':'SteamFriends',
                      'client':'SteamClient','userstats':'STEAMUSERSTATS_INTERFACE_VERSION','remotestorage':'STEAMREMOTESTORAGE_INTERFACE_VERSION',
                      'matchmaking':'SteamMatchMaking','matchmakingservers':'SteamMatchMakingServers','networking':'SteamNetworking','gameserver':'SteamGameServer'}
            for key,prefix in prefixes.items():
                options=[v for v in ref['interfaces'] if re.fullmatch(re.escape(prefix)+r'\d{3}',v)]
                if options:lines.append(key+'='+max(options))
            config_data=('\r\n'.join(lines)+'\r\n').encode('ascii')
            part=path+'.autorun-part'
            ftp.storbinary('STOR '+part,io.BytesIO(new));assert read(ftp,part)==new
            ftp.storbinary('STOR '+config,io.BytesIO(config_data));assert read(ftp,config)==config_data
            ftp.rename(path,backup)
            try:
                ftp.rename(part,path)
                if read(ftp,path)!=new or read(ftp,backup)!=old:raise RuntimeError('SDK readback mismatch')
            except Exception:
                try:ftp.delete(path)
                except ftplib.error_perm:pass
                ftp.rename(backup,path);ftp.delete(config);raise
            records.append({'game':label,'appid':appid,'path':path,'backup':backup,'config':config,
                            'original_sha256':ref['sha256'],'installed_sha256':hashlib.sha256(new).hexdigest(),
                            'bytes':len(new),'readback_verified':True,'hardware_compatibility':'pending'})
            (root/'installation.json').write_text(json.dumps({'scope':'two failed games; reversible shared SDK trial','records':records},indent=2)+'\n')
            print(label,'AppID',appid,'SDK bytes',len(new),'backup/readback verified')
finally:
    sequence(controller,DBI_TITLE,('B',))
