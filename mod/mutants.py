"""Shared helpers for the game-free mutant tests: run the mutants side by side (one job per CPU, MUTANT_JOBS overrides)
and, for mutants selected by a macro, compile only the sources that can see the macro (the rest link from objects built
once). A mutant that fails to build, survives or times out fails the run exactly as in a serial loop; results are
reported in the listed order."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import os
import subprocess

JOBS = max(1, int(os.environ.get('MUTANT_JOBS', os.cpu_count() or 4)))
FLAGS = ['g++', '-std=c++17', '-O1', '-pthread']


def parallel(function, items):
    """function(item) for every item, JOBS at a time; results in order; the first failure is raised."""
    with ThreadPoolExecutor(max_workers=JOBS) as pool:
        yield from pool.map(function, items)


class MacroBuilds:
    """Builds `sources` with one -DMACRO each. A source is recompiled for a macro only when it, or any header beside
    the sources, mentions the macro; a macro mentioned in a header recompiles every source."""

    def __init__(self, directory, roots, flags=()):
        self.directory, self.flags = Path(directory), list(flags)
        self.headers = {p: p.read_text(errors='replace') for root in roots for p in Path(root).glob('*.h')}
        self.text, self.objects = {}, {}

    def _object(self, source, macro=None):
        out = self.directory / ((macro or 'base') + '-' + '_'.join(source.resolve().parts[-2:]) + '.o')
        subprocess.run(FLAGS + self.flags + (['-D' + macro] if macro else []) + ['-c', str(source), '-o', str(out)], check=True, capture_output=True)
        return out

    def prepare(self, sources):
        """Compile the unmutated objects once (in parallel)."""
        todo = sorted({Path(s).resolve() for s in sources} - set(self.objects))
        for source in todo: self.text[source] = source.read_text(errors='replace')
        for source, obj in zip(todo, parallel(self._object, todo)): self.objects[source] = obj

    def build(self, macro, sources, executable):
        sources = [Path(s).resolve() for s in sources]
        everywhere = any(macro in text for text in self.headers.values())
        parts = [self._object(s, macro) if everywhere or macro in self.text[s] else self.objects[s] for s in sources]
        assert everywhere or any(macro in self.text[s] for s in sources), f'{macro} appears in no source'
        subprocess.run(FLAGS + [str(p) for p in parts] + ['-o', str(executable)], check=True, capture_output=True)
        return executable


def main(argv=None):
    """python3 mutants.py --sources "a.cpp b.cpp" [--args "x"] [--prefix build/mutant-] NAME..
    Builds each -DMUTANT_NAME side by side and runs it: a mutant that exits 0 survived (exit 1 here)."""
    import argparse, sys
    p = argparse.ArgumentParser(description=main.__doc__)
    p.add_argument('--sources', required=True); p.add_argument('--args', default=''); p.add_argument('--prefix', default='build/mutant-')
    p.add_argument('names', nargs='+')
    a = p.parse_args(argv)

    def one(name):
        exe = a.prefix + name
        built = subprocess.run(['g++', '-std=c++17', '-DMUTANT_' + name, *a.sources.split(), '-o', exe], capture_output=True, text=True)
        if built.returncode: return name, 'does not build:\n' + built.stderr[-2000:]
        ran = subprocess.run(['./' + exe if '/' not in exe else exe, *a.args.split()], capture_output=True, text=True, timeout=300)
        return name, 'SURVIVED' if ran.returncode == 0 else ''
    bad = [f'{name}: {why}' for name, why in parallel(one, a.names) if why]
    for row in bad: print(row)
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
