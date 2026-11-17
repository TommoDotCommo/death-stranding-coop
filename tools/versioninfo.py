#!/usr/bin/env python3
"""VERSIONINFO resources for every file the release ships (the launcher, the server, the proxy, the mod, Setup), so
Windows' file properties, diagnostics and Setup see the mod version (co-op mod standard J5). Adapted from the MDK2 co-op.

  versioninfo.py <version> <file name> <description> <out.rc>   writes the resource script (only when it changed)
  versioninfo.py --read <pe file> [field]                       prints a string field (default ProductVersion)
"""
import re
import sys
from pathlib import Path

PRODUCT = 'Death Stranding Co-op'


def numbers(version):
    """v1.2.3[-x] -> (1, 2, 3, 0); anything else (a git hash) -> zeros. The string keeps the exact version."""
    m = re.match(r'^v?(\d+)\.(\d+)\.(\d+)', version)
    return tuple(min(int(x), 65535) for x in m.groups()) + (0,) if m else (0, 0, 0, 0)


def script(version, name, description):
    if not re.match(r'^[A-Za-z0-9][A-Za-z0-9._+-]*$', version):
        raise ValueError('version must be letters, digits, dot, underscore, plus or hyphen: %r' % version)

    def q(s):
        return s.replace('"', '""')
    n = ','.join(str(x) for x in numbers(version))
    rows = [('CompanyName', 'TommoDotCommo'), ('FileDescription', description), ('FileVersion', version),
            ('InternalName', name), ('LegalCopyright', 'MIT licence'), ('OriginalFilename', name),
            ('ProductName', PRODUCT), ('ProductVersion', version)]
    values = '\n'.join('      VALUE "%s", "%s\\0"' % (k, q(v)) for k, v in rows)
    return ('1 VERSIONINFO\nFILEVERSION %s\nPRODUCTVERSION %s\nFILEOS 0x40004\nFILETYPE %s\nBEGIN\n'
            '  BLOCK "StringFileInfo"\n  BEGIN\n    BLOCK "040904B0"\n    BEGIN\n%s\n    END\n  END\n'
            '  BLOCK "VarFileInfo"\n  BEGIN\n    VALUE "Translation", 0x409, 1200\n  END\nEND\n'
            % (n, n, '2' if name.lower().endswith('.dll') else '1', values))


def read(data, field='ProductVersion'):
    """A string field of a PE file's VERSIONINFO, or None."""
    key = (field + '\0').encode('utf-16-le')
    i = data.find(key)
    if i < 0:
        return None
    i += len(key)
    while data[i:i + 2] == b'\0\0':     # padding to the 32-bit boundary where the value starts
        i += 2
    end = i
    while end + 1 < len(data) and data[end:end + 2] != b'\0\0':
        end += 2
    return data[i:end].decode('utf-16-le', 'replace')


if __name__ == '__main__':
    if len(sys.argv) in (3, 4) and sys.argv[1] == '--read':
        v = read(Path(sys.argv[2]).read_bytes(), *sys.argv[3:])
        print(v if v is not None else '')
        sys.exit(0 if v else 1)
    if len(sys.argv) != 5:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    out = Path(sys.argv[4])
    text = script(sys.argv[1], sys.argv[2], sys.argv[3])
    out.parent.mkdir(parents=True, exist_ok=True)
    if not out.exists() or out.read_text() != text:
        out.write_text(text)
