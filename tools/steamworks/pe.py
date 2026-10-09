"""Small bounded PE import/export reader for ABI/package validation."""
import struct
from pathlib import Path

def inspect(path):
    data=Path(path).read_bytes()
    if len(data)>32*1024*1024 or data[:2]!=b'MZ':raise ValueError('Not a bounded PE')
    def u16(p):return struct.unpack_from('<H',data,p)[0]
    def u32(p):return struct.unpack_from('<I',data,p)[0]
    pe=u32(0x3c)
    if data[pe:pe+4]!=b'PE\0\0':raise ValueError('Invalid PE header')
    machine=u16(pe+4); count=u16(pe+6); optional=pe+24; magic=u16(optional)
    width=8 if magic==0x20b else 4
    directories=optional+(112 if width==8 else 96)
    section=optional+u16(pe+20)
    sections=[(u32(section+i*40+12),max(u32(section+i*40+8),u32(section+i*40+16)),u32(section+i*40+20)) for i in range(count)]
    def offset(rva):
        for base,size,raw in sections:
            if base<=rva<base+size:
                p=raw+rva-base
                if p>=len(data):raise ValueError('Out of file RVA')
                return p
        raise ValueError('Unmapped RVA')
    def string(rva):
        p=offset(rva);end=data.index(0,p,min(len(data),p+512));return data[p:end].decode('ascii')
    imports={};rva=u32(directories+8)
    if rva:
        p=offset(rva)
        for i in range(256):
            ilt,stamp,forward,name,iat=struct.unpack_from('<IIIII',data,p+i*20)
            if not any((ilt,stamp,forward,name,iat)):break
            names=[];q=offset(ilt or iat)
            for j in range(8192):
                value=struct.unpack_from('<Q' if width==8 else '<I',data,q+j*width)[0]
                if not value:break
                names.append('#'+str(value&0xffff) if value>>(width*8-1) else string(value+2))
            imports[string(name)]=names
    exports=[];rva=u32(directories)
    if rva:
        p=offset(rva);n=u32(p+24);names=offset(u32(p+32)) if n else 0
        if n>16384:raise ValueError('Too many exports')
        exports=[string(u32(names+4*i)) for i in range(n)]
    return {'machine':machine,'imports':imports,'exports':exports}

if __name__=='__main__':
    import sys,json
    for path in sys.argv[1:]:print(json.dumps({'path':path,**inspect(path)},indent=2))
