"""Bounded, read-only SDK reference collection from the two failed owned games.
DBI must already be on its selected FTP row. Always stop FTP after collection.
"""
from pathlib import Path
import ftplib
import hashlib
import json
import re
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'remote_switch'))
from controller import Controller
from transport import BotTransport
from navigation import sequence, DBI_TITLE

destination=Path(__file__).resolve().parents[3]/'artifacts/steamworks-minimal/reference'
destination.mkdir(parents=True,exist_ok=True)
controller=Controller(BotTransport('192.168.70.59',6000,15))
sequence(controller,DBI_TITLE,('A','DDOWN','DDOWN','A'))
records=[]
try:
    with ftplib.FTP() as ftp:
        ftp.connect('192.168.70.59',5000,15);ftp.login()
        for name,path in (
            ('Dishonored-steam_api.dll','/switch/wine/drive_c/Steam/Dishonored RHCP/Binaries/Win32/steam_api.dll'),
            ('Balatro-steam_api64.dll','/switch/wine/drive_c/Steam/Balatro/steam_api64.dll'),
            ('Balatro-luasteam.dll','/switch/wine/drive_c/Steam/Balatro/luasteam.dll')):
            data=bytearray()
            def add(block):
                if len(data)+len(block)>1024*1024:raise ValueError('SDK reference exceeds bound')
                data.extend(block)
            ftp.retrbinary('RETR '+path,add)
            (destination/name).write_bytes(data)
            versions=sorted(set(x.decode() for x in re.findall(rb'(?:Steam(?:User|Apps|Utils|Friends|Client)\d{3}|STEAM(?:REMOTESTORAGE|USERSTATS)_INTERFACE_VERSION\d{3})',data)))
            records.append({'file':name,'path':path,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'interfaces':versions})
finally:
    sequence(controller,DBI_TITLE,('B',))
(destination/'references.json').write_text(json.dumps(records,indent=2)+'\n')
for r in records:print(r['file'],r['bytes'],r['interfaces'])
