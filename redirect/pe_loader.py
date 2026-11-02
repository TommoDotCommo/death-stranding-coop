"""Read-only PE32+ import/export inventory and compiler forwarding definition."""
import json
from pathlib import Path
import struct
import sys


def inspect(path):
    d = Path(path).read_bytes()
    def u(fmt, at): return struct.unpack_from('<'+fmt, d, at)
    if d[:2] != b'MZ': raise ValueError('not PE')
    pe, = u('I', 60)
    if d[pe:pe+4] != b'PE\0\0' or u('H', pe+4)[0] != 0x8664:
        raise ValueError('expected AMD64 PE')
    opt = pe+24
    if u('H', opt)[0] != 0x20b: raise ValueError('expected PE32+')
    sec = opt+u('H', pe+20)[0]
    def off(rva):
        for i in range(u('H', pe+6)[0]):
            vs, va, rs, raw = u('IIII', sec+i*40+8)
            if va <= rva < va+min(vs, rs): return raw+rva-va
        raise ValueError('unmapped RVA')
    def string(rva):
        at = off(rva)
        return d[at:d.index(b'\0', at)].decode('ascii')
    imports = {}
    irva, = u('I', opt+120)
    if irva:
        at = off(irva)
        while any(u('5I', at)):
            original, _, _, name, first = u('5I', at)
            thunk = off(original or first); names = []
            while True:
                v, = u('Q', thunk); thunk += 8
                if not v: break
                names.append('#'+str(v & 65535) if v >> 63 else string(v+2))
            imports[string(name)] = names; at += 20
    exports = []
    er, = u('I', opt+112)
    if er:
        fields = u('IIHH7I', off(er))
        base, count, nn, functions, names, ordinals = fields[5:]
        byordinal = {}
        for i in range(nn):
            nr, = u('I', off(names)+4*i)
            oi, = u('H', off(ordinals)+2*i)
            byordinal[oi] = string(nr)
        for i in range(count):
            if u('I', off(functions)+4*i)[0]:
                exports.append({'name': byordinal.get(i), 'ordinal': base+i})
    return {'imports': imports, 'exports': exports}


def definition(facts):
    lines = ['LIBRARY d3dcompiler_47', 'EXPORTS']
    for e in facts['exports']:
        name, ordinal = e['name'], e['ordinal']
        if not name or not all(c.isalnum() or c in '_@?' for c in name):
            raise ValueError('unnamed/unsupported export requires explicit review')
        lines.append(f'  {name}=d3dcompiler_47_original.{name} @{ordinal}')
    if not facts['exports']: raise ValueError('empty exports')
    return '\n'.join(lines)+'\n'


# Default Windows 10/11 KnownDLLs (HKLM\\...\\Session Manager\\KnownDLLs) that matter for the game's modules. They load
# from System32 whatever the search flags say. Other system DLLs (msvcp140, vcruntime140_1) do not.
KNOWN_DLLS = {n.lower() for n in (
    'advapi32.dll', 'clbcatq.dll', 'combase.dll', 'comdlg32.dll', 'coml2.dll', 'difxapi.dll', 'gdi32.dll', 'gdiplus.dll',
    'imagehlp.dll', 'imm32.dll', 'kernel32.dll', 'msctf.dll', 'msvcrt.dll', 'normaliz.dll', 'nsi.dll', 'ole32.dll',
    'oleaut32.dll', 'psapi.dll', 'rpcrt4.dll', 'sechost.dll', 'setupapi.dll', 'shcore.dll', 'shell32.dll', 'shlwapi.dll',
    'user32.dll', 'wldap32.dll', 'ws2_32.dll', 'wow64.dll', 'wow64cpu.dll', 'wow64win.dll')}


def app_dir_only_missing(imports, instance_names, loaded_names):
    """Static imports a DLL cannot resolve when loaded with LOAD_LIBRARY_SEARCH_APPLICATION_DIR alone.

    That flag also restricts the search for the DLL's own dependencies to the application directory
    (documented LoadLibraryEx behaviour). Virtual API-set names, KnownDLLs, files beside the executable and
    modules already loaded in the process are found; anything else gives ERROR_MOD_NOT_FOUND (126)."""
    have = {n.lower() for n in instance_names} | {n.lower() for n in loaded_names} | KNOWN_DLLS
    return sorted(name for name in imports
                  if not name.lower().startswith(('api-ms-win-', 'ext-ms-win-')) and name.lower() not in have)


if __name__ == '__main__':
    facts = inspect(sys.argv[1])
    if len(sys.argv) == 3:
        with open(sys.argv[2], 'x') as f: f.write(definition(facts))
    print(json.dumps(facts, indent=2))
