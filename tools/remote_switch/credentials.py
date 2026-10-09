"""Windows per-user DPAPI store. No credentials in repo, CLI args or reports."""
import ctypes
from ctypes import wintypes
import json
import os
import re
from pathlib import Path


def _crypt(data, decrypt=False):
    if os.name != 'nt':
        raise OSError('DPAPI credentials require the owning Windows user')
    class Blob(ctypes.Structure):
        _fields_ = [('size', wintypes.DWORD), ('data', ctypes.POINTER(ctypes.c_ubyte))]
    buffer = ctypes.create_string_buffer(data)
    source = Blob(len(data), ctypes.cast(buffer, ctypes.POINTER(ctypes.c_ubyte)))
    output = Blob()
    crypt32 = ctypes.WinDLL('crypt32', use_last_error=True)
    function = crypt32.CryptUnprotectData if decrypt else crypt32.CryptProtectData
    function.argtypes = [ctypes.POINTER(Blob), ctypes.c_void_p, ctypes.c_void_p,
                         ctypes.c_void_p, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(Blob)]
    function.restype = wintypes.BOOL
    if not function(ctypes.byref(source), None, None, None, None, 1, ctypes.byref(output)):
        raise OSError('DPAPI credential operation failed')
    try:
        return ctypes.string_at(output.data, output.size)
    finally:
        kernel32 = ctypes.WinDLL('kernel32')
        kernel32.LocalFree.argtypes = [ctypes.c_void_p]
        kernel32.LocalFree(ctypes.cast(output.data, ctypes.c_void_p))


def save(path, user, password):
    Path(path).write_bytes(_crypt(json.dumps({'user': user, 'password': password}).encode()))


def login(ftp):
    path = os.environ.get('AUTORUN_FTP_CREDENTIALS')
    if not path:
        return ftp.login()  # Explicit DBI temporary transport.
    data = json.loads(_crypt(Path(path).read_bytes(), decrypt=True))
    return ftp.login(data['user'], data['password'])


def http_key(path=None):
    value = (_crypt(Path(path).read_bytes(), decrypt=True).decode('ascii')
             if path else os.environ.get('AUTORUN_LAN_KEY',''))
    if not re.fullmatch('[0-9a-fA-F]{32}',value):
        raise ValueError('Valid temporary LAN key required; use DPAPI file or environment')
    return value
