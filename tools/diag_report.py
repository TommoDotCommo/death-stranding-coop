#!/usr/bin/env python3
"""Read Death Stranding Co-op diagnostics without extracting archives.

    python3 tools/diag_report.py host.zip guest.zip --timeline merged.txt --align
    python3 tools/diag_report.py player.zip --map matching.map --dll matching.dll
    python3 tools/diag_report.py logs --raw --json

Chains in a zip were checked before redaction; manifest SHA-256 verifies the
redacted bytes. Raw logs use FNV-1a/32. BUGMARK alignment is opt-in: use a marker
number present exactly once in each source; repeated numbers across runs are
ambiguous and are never silently matched. A match estimates clock offset, which
can include network delay or the delay between two players pressing F2.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import statistics
import struct
import sys
import zipfile

import map_symbols
import minidump

FNV_BASIS, FNV_PRIME, MASK = 2166136261, 16777619, 0xffffffff
LINE = re.compile(rb'^(\d+) (\d+) ([0-9a-f]{8}) (.*)$')
MARK = re.compile(r'\bBUGMARK (\d+)\b')
CRASH = re.compile(r'(?:crash:\s*)?(\w[\w-]*) exception (0x[0-9a-f]+) at ([^\s+]+)\+0x([0-9a-f]+)', re.I)
MAX_BUNDLE_BYTES = 1024 * 1024 * 1024


def format_line(prev, n, utc_ms, text):
    text = text.replace(b'\r', b' ').replace(b'\n', b' ')
    payload = b'%d %d %s' % (n, utc_ms, text)
    for byte in payload:
        prev = ((prev ^ byte) * FNV_PRIME) & MASK
    return b'%d %d %08x %s\n' % (n, utc_ms, prev, text), prev


def parse_log(data):
    lines, prev, problem = [], FNV_BASIS, None
    for raw in data.split(b'\n')[:-1]:  # a final partial write does not break the chain
        m = LINE.fullmatch(raw)
        if not m or int(m[1]) != len(lines):
            return lines, 'malformed or missing line %d' % len(lines)
        n, utc, text = int(m[1]), int(m[2]), m[4]
        _line, prev = format_line(prev, n, utc, text)
        if m[3] != b'%08x' % prev:
            problem = problem or 'chain mismatch at line %d' % n
        lines.append((n, utc, text.decode('utf-8', 'replace')))
    return lines, problem


def log_lines(data):
    return [(int(m[1]), int(m[2]), m[4].decode('utf-8', 'replace'))
            for raw in data.split(b'\n') if (m := LINE.fullmatch(raw))]


class Source:
    def __init__(self, path, raw=False):
        path = Path(path)
        self.name, self.files, self.problems, self.manifest = path.name, {}, [], None
        self.raw = raw or path.is_dir()
        if path.is_dir():
            self.files = {'logs/' + p.name: p.read_bytes() for p in path.iterdir() if p.is_file()}
        else:
            with zipfile.ZipFile(path) as archive:
                total = 0
                for info in archive.infolist():
                    if info.is_dir():
                        continue
                    name = info.filename.replace('\\', '/')
                    if name.startswith('/') or '..' in name.split('/') or name in self.files:
                        raise ValueError('unsafe or duplicate archive entry')
                    total += info.file_size
                    if total > MAX_BUNDLE_BYTES:
                        raise ValueError('bundle exceeds 1 GiB reader limit')
                    self.files[name] = archive.read(info)
            if 'manifest.json' in self.files:
                self.manifest = json.loads(self.files['manifest.json'].decode('utf-8-sig'))
            else:
                self.problems.append('no manifest.json')
        self.check_manifest()

    def check_manifest(self):
        if self.manifest is None:
            return
        listed = set()
        for entry in self.manifest.get('files', []):
            name = entry['file']
            if name in listed:
                self.problems.append('duplicate manifest entry: ' + name)
            listed.add(name)
            data = self.files.get(name)
            if data is None:
                self.problems.append('missing: ' + name)
            elif len(data) != entry['bytes'] or hashlib.sha256(data).hexdigest() != entry['sha256']:
                self.problems.append('size/SHA-256 mismatch: ' + name)
        for name in self.files.keys() - listed - {'manifest.json'}:
            self.problems.append('unlisted: ' + name)
        self.problems.extend('collection: ' + p for p in self.manifest.get('problems', []))

    def chain_info(self, name):
        if self.manifest is not None and not self.raw:
            for entry in self.manifest.get('files', []):
                if entry['file'] == name:
                    return entry.get('chain', {'status': 'unknown', 'problem': 'no pre-redaction chain result'})
            return {'status': 'unknown', 'problem': 'not in manifest'}
        lines, problem = parse_log(self.files[name])
        return {'status': 'broken' if problem else 'ok', 'problem': problem,
                'clean_exit': bool(lines) and lines[-1][2] == 'session end: clean exit'}


def crash_info(source, stem, text, symbols=None, stamp=None, module='avrt.dll'):
    info = {'file': stem, 'code': None, 'module': None, 'offset': None, 'where': None, 'stack': []}
    m = CRASH.search(text)
    if m:
        info.update(kind=m[1], code=m[2], module=m[3], offset=int(m[4], 16))
    if not m:
        legacy = re.search(r'CRASH\s+(unhandled|first chance)\s+(?:0x)?([0-9a-f]{8})[^\r\n]*? at ([^\s+]+)\+0x([0-9a-f]+)', text, re.I)
        if legacy:
            info.update(kind=legacy[1], code='0x'+legacy[2].lower(), module=legacy[3], offset=int(legacy[4], 16))
    dump = source.files.get(stem + '.dmp')
    if dump is not None:
        try:
            d = minidump.Minidump(dump)
            ex = d.exception
            if not ex:
                raise ValueError('no exception stream')
            target = d.module_for(ex['address'])
            info.update(code='0x%08x' % ex['code'], where=d.locate(ex['address']),
                        stack=[loc for _address, loc in d.code_pointers(96)][:12])
            if target:
                info.update(module=target['name'].replace('\\', '/').rsplit('/', 1)[-1], offset=ex['address'] - target['base'])
                if stamp is not None and stamp != target['timestamp']:
                    info['map_warning'] = 'DLL timestamp differs; map is from another build'
        except (ValueError, struct.error, KeyError, IndexError) as error:
            info['dump_error'] = str(error)
    if symbols and (info['module'] or '').lower() == module.lower() and info['offset'] is not None:
        if not info.get('map_warning'):
            info['symbol'] = symbols.describe(info['offset'])
    return info


def summarise(source, symbols=None, stamp=None, module='avrt.dll'):
    result = {'source': source.name, 'problems': list(source.problems), 'sessions': [], 'marks': [], 'crashes': [], 'legacy': [], 'flight': []}
    stems = set()
    for name, data in sorted(source.files.items()):
        if name.endswith('.log') and (name.startswith('logs/') or LINE.match(data)):
            rows = log_lines(data)
            chain = source.chain_info(name)
            result['sessions'].append(dict(file=name, lines=len(rows), chain=chain,
                first_utc_ms=rows[0][1] if rows else None, last_utc_ms=rows[-1][1] if rows else None))
            for _n, utc, text in rows:
                if m := MARK.search(text):
                    result['marks'].append(dict(number=int(m[1]), utc_ms=utc, file=name))
                if CRASH.search(text):
                    stems.add(name[:-4])
        if name.endswith(('.crash', '.dmp')):
            stems.add(name.rsplit('.', 1)[0])
        if name.startswith('legacy/'):
            result['legacy'].append(name)
            if '/crash-' in name and name.endswith('.log'):
                stems.add(name[:-4])
        if re.search(r'\.flight-\d+\.txt$', name):
            result['flight'].append(name)
    for stem in sorted(stems):
        text = '\n'.join(source.files.get(stem + ext, b'').decode('utf-8', 'replace') for ext in ('.log', '.crash'))
        result['crashes'].append(crash_info(source, stem, text, symbols, stamp, module))
    return result


def alignment(reports):
    """Offsets versus first input, using the median of uniquely shared markers."""
    marks, warnings = {}, []
    for report in reports:
        by_number = {}
        for mark in report['marks']:
            by_number.setdefault(mark['number'], []).append(mark['utc_ms'])
        marks[report['source']] = {n: times[0] for n, times in by_number.items() if len(times) == 1}
        if any(len(times) > 1 for times in by_number.values()):
            warnings.append(report['source'] + ': repeated BUGMARK numbers ignored')
    offsets = {r['source']: 0 for r in reports}
    base = marks[reports[0]['source']]
    for report in reports[1:]:
        name = report['source']
        differences = [base[n] - utc for n, utc in marks[name].items() if n in base]
        if differences:
            offsets[name] = int(statistics.median(differences))
        else:
            warnings.append(name + ': no unique shared BUGMARK; UTC unchanged')
    return offsets, warnings


def timeline(sources, reports, path, align=False):
    offsets, warnings = alignment(reports) if align else ({s.name: 0 for s in sources}, [])
    rows = []
    for source in sources:
        for name, data in source.files.items():
            if name.endswith('.log'):
                rows.extend((utc + offsets[source.name], source.name, name, text) for _n, utc, text in log_lines(data))
    rows.sort()
    with open(path, 'w', encoding='utf-8') as stream:
        for warning in warnings:
            stream.write('# ' + warning + '\n')
        if align:
            stream.write('# BUGMARK alignment estimates clock offset; includes marker/network delay.\n')
            stream.write('# offsets ms: ' + json.dumps(offsets, sort_keys=True) + '\n')
        for utc, source, file, text in rows:
            time = datetime.fromtimestamp(utc / 1000, timezone.utc).isoformat(timespec='milliseconds')
            stream.write('%s [%s %s] %s\n' % (time, source, file, text))
    return len(rows)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('inputs', nargs='+')
    parser.add_argument('--raw', action='store_true')
    parser.add_argument('--timeline', metavar='FILE')
    parser.add_argument('--align', action='store_true')
    parser.add_argument('--json', action='store_true')
    parser.add_argument('--map', help='GNU linker map from the exact crashed build')
    parser.add_argument('--dll', help='matching DLL, for the dump timestamp check')
    parser.add_argument('--module', default='avrt.dll', help='module described by --map (for example mod.dll)')
    args = parser.parse_args(argv)
    try:
        symbols = map_symbols.SymbolMap(Path(args.map).read_text(errors='replace')) if args.map else None
        stamp = map_symbols.pe_timestamp(args.dll) if args.dll else None
        sources = [Source(p, args.raw) for p in args.inputs]
        if len({s.name for s in sources}) != len(sources):
            raise ValueError('input basenames must be distinct to label both players')
        reports = [summarise(s, symbols, stamp, args.module) for s in sources]
        if args.json:
            print(json.dumps(reports, indent=2))
        else:
            for report in reports:
                print(report['source'])
                for problem in report['problems']:
                    print('  !! ' + problem)
                for session in report['sessions']:
                    print('  %s: %d lines; chain %s; clean exit %s' % (session['file'], session['lines'], session['chain']['status'], session['chain'].get('clean_exit', 'unknown')))
                for crash in report['crashes']:
                    print('  CRASH ' + json.dumps(crash))
                print('  %d BUGMARKs; %d flight files; %d legacy files (unclocked legacy text is kept in the zip)' % (len(report['marks']), len(report['flight']), len(report['legacy'])))
        if args.timeline:
            count = timeline(sources, reports, args.timeline, args.align)
            print('timeline: %d lines in %s' % (count, args.timeline), file=sys.stderr)
        return int(any(r['problems'] or any(s['chain']['status'] != 'ok' for s in r['sessions']) for r in reports))
    except (OSError, ValueError, KeyError, TypeError, AttributeError, zipfile.BadZipFile, struct.error) as error:
        print('diagnostics: ' + str(error), file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
