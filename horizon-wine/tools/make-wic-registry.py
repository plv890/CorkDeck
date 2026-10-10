#!/usr/bin/env python3
"""The registry entries windowscodecs.dll writes for itself, as a hive the runtime loads.

Windows' imaging component finds its decoders, encoders, converters and pixel
formats by lists in the registry. windowscodecs.dll writes those lists in code,
when it is registered (dlls/windowscodecs/regsvr.c); nothing registers DLLs on
the console, and the classes.reg that comes with the Windows components holds
only what widl's registration scripts say -- which DLL serves a class -- so the
lists were not there. A program asking for a picture to be decoded was told the
format was unknown: D3DX could load no PNG or JPEG, and Age of Empires II HD
stopped at its first texture ("Failed to load image ...png").

This reads the tables of regsvr.c and writes what its registration functions
would: decoders with their patterns, encoders, format converters, pixel formats
and the categories. Metadata readers and writers are left out.

    make-wic-registry.py WINE_SOURCE OUTPUT.h     the hive as a C string, for the runtime
    make-wic-registry.py WINE_SOURCE --check OUTPUT.h   fail when OUTPUT.h is not what would be written
"""
import re, sys
from pathlib import Path


def strip_comments(text):
    return re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)


def guids(source):
    found = {}
    files = list((source / 'include').glob('wincodec*.idl')) + list((source / 'include').glob('wincodec*.h')) + \
            list((source / 'dlls/windowscodecs').glob('*.idl')) + list((source / 'dlls/windowscodecs').glob('*.h')) + \
            list((source / 'dlls/windowscodecs').glob('*.c')) + [source / 'include/guiddef.h']
    define = re.compile(r'DEFINE_GUID\s*\(\s*(\w+)\s*,\s*((?:0x[0-9a-fA-F]+\s*,\s*){10}0x[0-9a-fA-F]+)\s*\)')
    coclass = re.compile(r'uuid\(\s*([0-9a-fA-F-]{36})\s*\)[^\]]*\]\s*coclass\s+(\w+)', re.S)
    for path in files:
        if not path.is_file():
            continue
        text = path.read_text(errors='replace')
        for name, numbers in define.findall(text):
            n = [int(x, 16) for x in re.findall(r'0x[0-9a-fA-F]+', numbers)]
            found[name] = '{%08X-%04X-%04X-%02X%02X-%s}' % (n[0], n[1], n[2], n[3], n[4], ''.join('%02X' % b for b in n[5:]))
        for uuid, name in coclass.findall(text):
            found.setdefault('CLSID_' + name, '{' + uuid.upper() + '}')
    return found


def number(text):
    text = text.strip()
    if re.fullmatch(r'0[0-7]+', text):
        return int(text, 8)
    return int(text, 0)


def fields(entry):
    """The members of one initializer, split at the commas outside strings."""
    out, depth, current, quoted = [], 0, '', False
    for c in entry:
        if c == '"':
            quoted = not quoted
        if not quoted and c == ',' and depth == 0:
            out.append(current.strip())
            current = ''
            continue
        if not quoted and c in '{(':
            depth += 1
        if not quoted and c in '})':
            depth -= 1
        current += c
    if current.strip():
        out.append(current.strip())
    return out


def entries(body):
    """The brace groups at the top of a list's body."""
    out, depth, start = [], 0, None
    for i, c in enumerate(body):
        if c == '{':
            if depth == 0:
                start = i + 1
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                out.append(body[start:i])
    return out


def string(text):
    text = text.strip()
    if text == 'NULL':
        return None
    return ''.join(re.findall(r'"((?:[^"\\]|\\.)*)"', text))


class Tables:
    def __init__(self, source):
        self.text = strip_comments((source / 'dlls/windowscodecs/regsvr.c').read_text())
        self.guid = guids(source)
        self.bytes = {name: [number(x) for x in body.split(',') if x.strip()]
                      for name, body in re.findall(r'static\s+(?:const\s+BYTE|BYTE\s+const)\s+(\w+)\s*\[\d*\]\s*=\s*\{([^{}]*)\}', self.text)}
        # Some are written as text: "DDS ", "GIF87a".
        for name, literal in re.findall(r'static\s+(?:const\s+BYTE|BYTE\s+const)\s+(\w+)\s*\[\d*\]\s*=\s*"((?:[^"\\]|\\.)*)"', self.text):
            self.bytes[name] = list(literal.encode('latin-1').decode('unicode_escape').encode('latin-1'))
        self.pointers = {name: [x.strip().lstrip('&') for x in body.split(',') if x.strip() and x.strip() != 'NULL']
                         for name, body in re.findall(r'static\s+(?:GUID|BYTE)\s+const\s*\*\s*const\s+(\w+)\s*\[\]\s*=\s*\{([^{}]*)\}', self.text)}
        self.patterns = {}
        for name, body in re.findall(r'static\s+struct\s+decoder_pattern\s+const\s+(\w+)\s*\[\]\s*=\s*\{(.*?)\}\s*;', self.text, re.S):
            rows = []
            for entry in entries(body):
                f = fields(entry)
                if len(f) < 5:
                    continue   # the terminator
                rows.append((number(f[0]), number(f[1]), f[2], f[3], number(f[4])))
            self.patterns[name] = rows

    def list(self, kind):
        match = re.search(r'static\s+struct\s+regsvr_%s\s+const\s+\w+\s*\[\]\s*=\s*\{(.*?)\n\}\s*;' % kind, self.text, re.S)
        if not match:
            raise SystemExit('regsvr.c has no list of ' + kind)
        return [fields(e) for e in entries(match.group(1)) if fields(e) and fields(e)[0] != 'NULL']

    def id(self, text):
        name = text.strip().lstrip('&')
        if name not in self.guid:
            raise SystemExit('no value found for ' + name)
        return self.guid[name]


def quote(text):
    return text.replace('\\', '\\\\').replace('"', '\\"')


def build(source):
    t = Tables(source)
    lines = ['WINE REGISTRY Version 2',
             ';; What windowscodecs.dll writes when it is registered: the lists Windows\' imaging',
             ';; component finds its decoders, encoders, converters and pixel formats by.',
             ';; Written by horizon-wine/tools/make-wic-registry.py from dlls/windowscodecs/regsvr.c.',
             '']
    root = 'Software\\\\Classes\\\\CLSID\\\\'

    def key(*parts):
        lines.append('[' + root + '\\\\'.join(parts) + ']')

    def text_value(name, value):
        if value is not None:
            lines.append('"%s"="%s"' % (name, quote(value)))

    def instance(category, clsid, friendly):
        key(t.id(category), 'Instance', clsid)
        text_value('CLSID', clsid)
        lines.append('')

    def common(f, clsid):
        key(clsid)
        text_value('Author', string(f[1]))
        text_value('FriendlyName', string(f[2]))

    counts = {}
    for f in t.list('decoder'):
        clsid = t.id(f[0])
        instance('CATID_WICBitmapDecoders', clsid, string(f[2]))
        common(f, clsid)
        text_value('Vendor', t.id(f[4]))
        text_value('ContainerFormat', t.id(f[5]))
        text_value('Version', string(f[3]))
        text_value('MimeTypes', string(f[6]))
        text_value('FileExtensions', string(f[7]))
        lines.append('')
        for name in t.pointers[f[8]]:
            key(clsid, 'Formats', t.id(name))
            lines.append('')
        for i, (length, position, pattern, mask, end) in enumerate(t.patterns.get(f[9], [])):
            key(clsid, 'Patterns', str(i))
            lines.append('"Length"=dword:%08x' % length)
            lines.append('"Position"=dword:%08x' % position)
            lines.append('"Pattern"=hex:' + ','.join('%02x' % b for b in t.bytes[pattern][:length]))
            lines.append('"Mask"=hex:' + ','.join('%02x' % b for b in t.bytes[mask][:length]))
            lines.append('"EndOfStream"=dword:%08x' % end)
            lines.append('')
        counts['decoders'] = counts.get('decoders', 0) + 1
    for f in t.list('encoder'):
        clsid = t.id(f[0])
        instance('CATID_WICBitmapEncoders', clsid, string(f[2]))
        common(f, clsid)
        text_value('Vendor', t.id(f[4]))
        text_value('ContainerFormat', t.id(f[5]))
        text_value('Version', string(f[3]))
        text_value('MimeTypes', string(f[6]))
        text_value('FileExtensions', string(f[7]))
        lines.append('')
        for name in t.pointers[f[8]]:
            key(clsid, 'Formats', t.id(name))
            lines.append('')
        counts['encoders'] = counts.get('encoders', 0) + 1
    for f in t.list('converter'):
        clsid = t.id(f[0])
        instance('CATID_WICFormatConverters', clsid, string(f[2]))
        common(f, clsid)
        text_value('Vendor', t.id(f[4]))
        text_value('Version', string(f[3]))
        lines.append('')
        for name in t.pointers[f[5]]:
            key(clsid, 'PixelFormats', t.id(name))
            lines.append('')
        counts['converters'] = counts.get('converters', 0) + 1
    representation = dict(re.findall(r'(WICPixelFormatNumericRepresentation\w+)\s*=\s*(0x[0-9a-fA-F]+|\d+)',
                                     (source / 'include/wincodec.idl').read_text(errors='replace')))
    for f in t.list('pixelformat'):
        clsid = t.id(f[0])
        instance('CATID_WICPixelFormats', clsid, string(f[2]))
        common(f, clsid)
        text_value('Vendor', t.id(f[4]))
        text_value('Version', string(f[3]))
        lines.append('"BitLength"=dword:%08x' % number(f[5]))
        lines.append('"ChannelCount"=dword:%08x' % number(f[6]))
        lines.append('"NumericRepresentation"=dword:%08x' % number(representation[f[8]]))
        lines.append('"SupportsTransparency"=dword:%08x' % number(f[9]))
        lines.append('')
        if f[7] != 'NULL':
            size = (number(f[5]) + 7) // 8
            key(clsid, 'ChannelMasks')
            for i, mask in enumerate(t.pointers[f[7]][:number(f[6])]):
                data = (t.bytes[mask] + [0] * size)[:size]
                lines.append('"%d"=hex:%s' % (i, ','.join('%02x' % b for b in data)))
            lines.append('')
        counts['pixel formats'] = counts.get('pixel formats', 0) + 1
    for name in ('CATID_WICBitmapDecoders', 'CATID_WICBitmapEncoders', 'CATID_WICFormatConverters', 'CATID_WICPixelFormats'):
        key(t.id('CLSID_WICImagingCategories'), 'Instance', t.id(name))
        text_value('CLSID', t.id(name))
        lines.append('')
    return '\n'.join(lines) + '\n', counts


def header(hive):
    out = ['/* Written by tools/make-wic-registry.py from dlls/windowscodecs/regsvr.c: do not edit.',
           ' * The hive is what windowscodecs.dll would write on being registered. */',
           'static const char wic_registry_hive[] =']
    for line in hive.split('\n')[:-1]:
        out.append('    "' + line.replace('\\', '\\\\').replace('"', '\\"') + '\\n"')
    out.append(';')
    return '\n'.join(out) + '\n'


if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if a != '--check']
    source, output = Path(args[0]), Path(args[1])
    hive, counts = build(source)
    text = header(hive)
    if '--check' in sys.argv:
        if output.read_text(encoding='utf-8') != text:
            raise SystemExit(str(output) + ' is not what regsvr.c gives now: run make-wic-registry.py again')
        print('wic registry: up to date,', ', '.join('%d %s' % (n, k) for k, n in counts.items()))
    else:
        output.write_text(text, encoding='utf-8', newline='\n')
        print('wic registry:', ', '.join('%d %s' % (n, k) for k, n in counts.items()), '-> %d bytes' % len(hive))
