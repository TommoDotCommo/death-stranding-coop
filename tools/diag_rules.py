"""Portable consumer of dist/diag-rules.json, also consumed by diag.ps1.

The shared table is the privacy policy. Binary substitutions preserve byte offsets;
text uses stable per-bundle tokens. No raw value-to-token map is exported.
"""
import json
from pathlib import Path
import re

RULES = json.loads((Path(__file__).resolve().parents[1] / 'dist/diag-rules.json').read_text())


class Redactor:
    def __init__(self, profile='', user='', machine='', secrets=(), rules=None):
        self.literals = dict(profile=profile, user=user, machine=machine)
        self.rules = RULES['rules'] if rules is None else rules
        self.secrets = [s for s in secrets if len(s) >= 4]
        self.tokens = {}

    def text(self, text, fixed=False):
        for secret in self.secrets:
            text = re.sub(re.escape(secret), lambda m: '#' * len(m[0]) if fixed else '<secret>', text)
        for rule in self.rules:
            pattern = rule.get('pattern')
            if 'literal' in rule:
                literal = self.literals.get(rule['literal'])
                if not literal:
                    continue
                pattern = '(?i)' + re.escape(literal)
            def replace(m):
                if fixed:
                    return '#' * len(m[0])
                if 'token' in rule:
                    key = (rule['token'], m[0].lower())
                    if key not in self.tokens:
                        self.tokens[key] = 1 + sum(k[0] == key[0] for k in self.tokens)
                    return '%s-%s' % (key[0], self.tokens[key])
                return re.sub(r'\$(\d+)', lambda g: m[int(g[1])], rule['replacement'])
            text = re.sub(pattern, replace, text)
        return text

    def binary(self, data):
        # Match ASCII and embedded UTF-16LE strings without decoding arbitrary dump
        # bytes as UTF-16 (which could replace invalid surrogates and damage offsets).
        text = self.text(data.decode('latin1'), fixed=True)
        def wide(m):
            value = m[0].encode('latin1').decode('utf-16le')
            return self.text(value, fixed=True).encode('utf-16le').decode('latin1')
        text = re.sub(r'(?:[\x20-\x7e]\x00){2,}', wide, text)
        literals = list(self.literals.values()) + self.secrets
        # As in Witness, also mask name prefixes cut off at a dump memory boundary.
        literals += [value[:3] for value in self.literals.values() if len(value) >= 4]
        for literal in literals:
            if literal:
                raw = literal.encode('utf-8').decode('latin1')
                text = re.sub(re.escape(raw), lambda m: '#' * len(m[0]), text, flags=re.I)
                raw = literal.encode('utf-16le').decode('latin1')
                text = re.sub(re.escape(raw), lambda m: '#\0' * (len(m[0]) // 2), text, flags=re.I)
        return text.encode('latin1')


def select_logs(files, budget):
    """(name, size, mtime) entries, newest first, strict total byte cap (including dumps)."""
    selected, skipped = [], []
    for name, size, mtime in sorted(files, key=lambda f: (-f[2], f[0])):
        if size > budget:
            skipped.append(name)
        else:
            selected.append(name)
            budget -= size
    return selected, skipped
