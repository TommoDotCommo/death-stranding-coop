#!/usr/bin/env python3
"""Conservative evidence report for read-only discovery JSONL (never accesses a game).

Action labels are verifier observations, not inferred from arbitrary changing bytes.
Example: analyse_discovery.py discovery-123.jsonl --actions open-menu walk run jump
"""
import argparse
import itertools
import json
import math
from pathlib import Path
import statistics
import struct
import sys
from find_signatures import PINNED_SHA256, CANDIDATES

ACTIONS = {
    'H1': ['open-menu', 'walk', 'reload'], 'H2': ['walk', 'reload'],
    'H3': ['walk', 'enter-vehicle', 'observe-npc'],
    'H4': ['walk', 'run', 'jump', 'teleport'],
    'H5': ['walk', 'turn-90', 'teleport', 'region-boundary'],
    'H6': ['turn-90', 'jump', 'walk'], 'H7': ['walk', 'run', 'jump', 'enter-vehicle'],
    'H8': ['walk', 'run', 'four-directions'], 'H9': ['walk', 'run', 'jump', 'crouch'],
    'H10': ['enter-vehicle', 'drive', 'exit-vehicle'],
    'H11': ['add-cargo', 'remove-cargo', 'rearrange-cargo'],
    'H12': ['soothe-bb', 'bb-crying'], 'H13': ['open-menu', 'save', 'reload'],
    'H14': ['cutscene', 'muted-cutscene', 'cutscene-exit'],
}
LIMITATIONS = {
    'H1': 'Shared signature target is static corroboration; manager selection, type and lifetime remain unproven.',
    'H2': 'Player+0x70 is only a candidate; no DS getter calls or typed ownership proof.',
    'H3': 'Stable local flags cannot distinguish NPC/remote semantics without independent observations.',
    'H4': 'Scalar layout, measured distance, units and axes need independent live validation.',
    'H5': 'Final transform word is not proven to be a region identifier.',
    'H6': 'WorldNode owner and matrix row/column convention need independent validation.',
    'H7': 'Virtual return values are unavailable without calls; configured vector roots are candidates only.',
    'H8': 'Input direction/timing annotations are needed to distinguish input from speed.',
    'H9': 'Slot names, valid bounds and owner are not established by readable pages.',
    'H10': 'Composite driving predicate and mode results are unavailable without calls.',
    'H11': 'Cargo owner, record path and independently observed item IDs remain unknown.',
    'H12': 'BB owner, displayed affection, range and suppression semantics remain unknown.',
    'H13': 'Save-dialog predicate is narrow and cannot establish general world loading.',
    'H14': 'Sound global bytes do not expose the called function result or a visual cutscene gate.',
}


def read_log(path):
    records = []
    with Path(path).open() as f:
        total = 0
        for i, line in enumerate(f, 1):
            total += len(line)
            if total > 64*1024*1024 or len(line) > 1024*1024:
                raise ValueError('log exceeds discovery bounds')
            try:
                records.append(json.loads(line, parse_constant=lambda s: (_ for _ in ()).throw(ValueError(s))))
            except (ValueError, json.JSONDecodeError) as e:
                raise ValueError(f'invalid JSON at line {i}: {e}') from e
    return records


def decoded(sample, name, kind='f32'):
    block = sample.get('regions', {}).get(name)
    if not block:
        return None
    try:
        b = bytes.fromhex(block['hex'])
        width, code = {'f32': (4, 'f'), 'f64': (8, 'd'), 'u32': (4, 'I'), 'u8': (1, 'B')}[kind]
        # Decode raw independently; do not trust precomputed JSON interpretations.
        return list(struct.unpack('<'+code*(len(b)//width), b[:len(b)//width*width]))
    except (ValueError, TypeError, KeyError, struct.error):
        return None


def finite(v):
    return v is not None and all(isinstance(x, (float, int)) and math.isfinite(x) and abs(x) < 1e7 for x in v)


def norm(v):
    return math.sqrt(sum(x*x for x in v))


def variation(samples, name, kind='f32'):
    values = [decoded(s, name, kind) for s in samples]
    good = [v for v in values if finite(v)]
    return {'readable_samples': len(good), 'changes': sum(a != b for a, b in zip(good, good[1:])),
            'min': min((min(v) for v in good if v), default=None),
            'max': max((max(v) for v in good if v), default=None)}


def position_report(samples, name, kind):
    values = [decoded(s, name, kind) for s in samples]
    good = [(s['t_us'], v[:3], s) for s, v in zip(samples, values) if v and len(v) >= 3 and finite(v[:3])]
    deltas, speeds, errors = [], [], []
    moving_input_agreement = [0, 0]
    permutations = list(itertools.permutations(range(3)))
    axis_errors = {p: [] for p in permutations}
    jumps = []
    for (ta, a, _), (tb, b, sb) in zip(good, good[1:]):
        dt = (tb-ta)/1e6
        if dt <= 0:
            continue
        delta = [(y-x)/dt for x, y in zip(a, b)]
        speed = norm(delta)
        if speed > 100:
            jumps.append({'t_us': tb, 'speed_units_s': speed})
            continue
        deltas.append([y-x for x, y in zip(a, b)])
        speeds.append(speed)
        move = decoded(sb, 'move')
        if norm(deltas[-1]) > .0001 and move and finite(move[:2]):
            moving_input_agreement[0 if norm(move[:2]) > .01 else 1] += 1
        velocity = decoded(sb, 'velocity')
        if velocity and len(velocity) >= 3 and finite(velocity[:3]):
            errors.append(abs(speed-norm(velocity[:3])))
            for p in permutations:
                axis_errors[p].append(norm([delta[i]-velocity[p[i]] for i in range(3)]))
    extent = [max(v[i] for _, v, _ in good)-min(v[i] for _, v, _ in good) for i in range(3)] if good else []
    moving = bool(deltas and norm(extent) > .01 and sum(norm(d) > .0001 for d in deltas) >= 2)
    best = min(permutations, key=lambda p: statistics.mean(axis_errors[p]) if axis_errors[p] else math.inf)
    identity = statistics.mean(axis_errors[(0, 1, 2)]) if axis_errors[(0, 1, 2)] else None
    best_error = statistics.mean(axis_errors[best]) if axis_errors[best] else None
    swapped = bool(moving and best != (0, 1, 2) and identity is not None and identity > .05 and best_error < identity*.5)
    return dict(region=name, interpretation=kind, plausible_samples=len(good), total_samples=len(samples),
                movement=moving, extent=extent,
                moving_with_input_samples=moving_input_agreement[0],
                moving_without_input_samples=moving_input_agreement[1], speed_median=statistics.median(speeds) if speeds else None,
                speed_velocity_absolute_error=statistics.mean(errors) if errors else None,
                best_velocity_axis_mapping=list(best) if best_error is not None else None,
                axis_warning='swapped axes / coordinate conversion required' if swapped else None,
                axis_mapping_error=best_error, discontinuities=jumps,
                units='unknown: measure a 10 m walk; speed is in candidate units/s')


def analyse(records, actions=()):
    if not records or records[0].get('type') != 'header':
        raise ValueError('validated header required')
    h = records[0]
    sigs = h.get('signatures', [])
    if h.get('schema') != 1 or h.get('sha256') != PINNED_SHA256 or len(sigs) != 6:
        raise ValueError('untrusted build/schema/signature header')
    if {s.get('name') for s in sigs} != {c.name for c in CANDIDATES} or any(s.get('match_count') != 1 for s in sigs):
        raise ValueError('six unique signatures required')
    if any(r.get('type') == 'error' for r in records):
        raise ValueError('discovery failed/stopped with error; incomplete evidence')
    samples = []
    previous = -1
    rejected = 0
    for s in records[1:]:
        if s.get('type') != 'sample':
            continue
        t = s.get('t_us')
        if not isinstance(t, int) or t <= previous:
            raise ValueError('non-monotonic sample timestamps')
        previous = t
        if s.get('coherent') is not True:
            rejected += 1
            continue
        samples.append(s)
    positions = [position_report(samples, name, kind) for name in ('position_c8', 'position_d8') for kind in ('f32', 'f64')]
    moving = any(p['movement'] for p in positions)
    regions = ['manager', 'player', 'entity_slot', 'local_flags', 'world_transform', 'worldnode_position',
               'worldnode_rotation', 'velocity', 'angular_velocity', 'move', 'vehicle_4cc', 'vehicle_63c',
               'vehicle_528', 'bb', 'save_manager', 'cutscene_sound', 'baggage']
    regions += sorted({k for s in samples for k in s.get('regions', {}) if k.startswith(('animation_', 'save_entry_'))})
    stats = {name: variation(samples, name, 'u8' if name == 'local_flags' else 'f32') for name in regions}
    stats['local_flags'] = variation(samples, 'local_flags', 'u8')
    for name in ('vehicle_4cc', 'vehicle_63c', 'vehicle_528', 'bb', 'save_manager', 'cutscene_sound', 'baggage'):
        stats[name] = variation(samples, name, 'u8')
    save_states = [v[0] for s in samples for k in s.get('regions', {}) if k.startswith('save_entry_') for v in [decoded(s, k, 'u32')] if v]
    predicates = []
    for s in samples:
        states = [decoded(s, k, 'u32') for k in s.get('regions', {}) if k.startswith('save_entry_')]
        # Absent/unreadable manager or invalid count must not become a false gate.
        if decoded(s, 'save_manager', 'u8') and 'save_array_invalid' not in s.get('regions', {}) and all(states):
            predicates.append(any(6 <= v[0] <= 15 for v in states))
    flags = [decoded(s, 'local_flags', 'u8') for s in samples]
    flags = [f for f in flags if f and len(f) == 2]
    stats['local_flags'].update(stable=bool(flags) and all(f == flags[0] for f in flags),
        observed_pairs=sorted({tuple(f) for f in flags}),
        local_human_predicate_samples=sum(f[0] != 0 and f[1] == 0 for f in flags))
    moves = [decoded(s, 'move') for s in samples]
    moves = [v[:2] for v in moves if v and len(v) >= 2 and finite(v[:2])]
    stats['move'].update(within_unit_components=bool(moves) and all(abs(x) <= 1.001 for v in moves for x in v),
                         neutral_samples=sum(norm(v) < .001 for v in moves))
    transforms = [decoded(s, 'world_transform', 'u32') for s in samples]
    finals = [v[14] for v in transforms if v and len(v) >= 15]
    stats['world_transform']['final_word_values'] = sorted(set(finals))[:64]
    stats['world_transform']['position_prefix_agrees_samples'] = sum(
        bool(s['regions'].get('world_transform') and s['regions'].get('position_c8') and
             s['regions']['world_transform']['hex'][:48] == s['regions']['position_c8']['hex']) for s in samples)
    rotations = [decoded(s, 'worldnode_rotation') for s in samples]
    ortho_errors = []
    for v in rotations:
        if not v or len(v) < 9 or not finite(v):
            continue
        rows = [v[i:i+3] for i in (0, 3, 6)]
        ortho_errors.append(max(abs(sum(a*b for a, b in zip(rows[i], rows[j]))-(i == j))
                                for i in range(3) for j in range(3)))
    stats['worldnode_rotation']['orthonormal_max_error'] = max(ortho_errors, default=None)
    bb_fields = []
    for s in samples:
        b = decoded(s, 'bb', 'u8')
        if b and len(b) >= 14:
            value = int.from_bytes(bytes(b[:4]), 'little')
            bb_fields.append((value, b[13], 0 if b[13] else value))
    stats['bb']['value_suppression_effective_candidates'] = sorted(set(bb_fields))[:64]
    evidence = {
        'H1': {'manager': stats['manager'], 'independent_signature_agreement': bool(sigs[0].get('targets', {}).get('player_manager_global')) and sigs[0].get('targets', {}).get('player_manager_global') == sigs[1].get('targets', {}).get('player_manager_global')},
        'H2': {k: stats[k] for k in ('player', 'entity_slot')},
        'H3': stats['local_flags'], 'H4': positions,
        'H5': stats['world_transform'], 'H6': {k: stats[k] for k in ('worldnode_position', 'worldnode_rotation')},
        'H7': {k: stats[k] for k in ('velocity', 'angular_velocity')}, 'H8': stats['move'],
        'H9': {k: v for k, v in stats.items() if k.startswith('animation_')},
        'H10': {k: v for k, v in stats.items() if k.startswith('vehicle_')},
        'H11': stats['baggage'], 'H12': stats['bb'],
        'H13': {'entry_states': sorted(set(save_states)), 'predicate_toggled': len(set(predicates)) > 1,
                'predicate_valid_samples': len(predicates)},
        'H14': {'global_bytes': stats['cutscene_sound'], 'sound_result': 'unavailable', 'visual_gate': 'unavailable'},
    }
    candidate_status = {h: 'insufficient evidence' for h in ACTIONS}
    candidate_status['H3'] = ('stable observed predicate' if flags and stats['local_flags']['stable'] else 'unstable or unavailable predicate')
    candidate_status['H4'] = ('motion supports a candidate; decoding/units still ambiguous' if moving else 'no movement evidence')
    if any(p['axis_warning'] for p in positions):
        candidate_status['H7'] = 'coordinate axes disagree with velocity candidate'
    elif any(p['movement'] and p['speed_velocity_absolute_error'] is not None and p['speed_velocity_absolute_error'] < .1 for p in positions):
        candidate_status['H7'] = 'finite-difference speed supports a velocity candidate'
    if moves:
        candidate_status['H8'] = 'consistent with bounded input' if stats['move']['within_unit_components'] else 'refutes normalized [-1,1] input interpretation'
    if ortho_errors:
        candidate_status['H6'] = 'consistent with orthonormal rotation' if max(ortho_errors) < .05 else 'refutes orthonormal rotation interpretation at these samples'
    if any(v['changes'] for k, v in stats.items() if k.startswith('animation_')):
        candidate_status['H9'] = 'variable slots observed changing; names unknown'
    if len(set(predicates)) > 1:
        candidate_status['H13'] = 'narrow save-dialog predicate observed toggling'
    verdicts = {}
    for hypothesis, needed in ACTIONS.items():
        missing = [a for a in needed if a not in actions]
        if not moving and 'walk' in needed and 'walk' not in missing:
            missing.insert(0, 'walk (repeat: no measured movement in log)')
        verdicts[hypothesis] = dict(verdict='unconfirmed', candidate_status=candidate_status[hypothesis], evidence=evidence[hypothesis],
            missing_live_actions=missing, remaining_evidence=LIMITATIONS[hypothesis])
    return dict(samples=len(samples), rejected_incoherent=rejected, movement_observed=moving,
                action_annotations=sorted(set(actions)), hypotheses=verdicts,
                warning='No semantic hypothesis is confirmed by readable/changing bytes alone; asynchronous snapshots are non-atomic.')


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('log', type=Path)
    p.add_argument('--actions', nargs='*', choices=sorted({a for v in ACTIONS.values() for a in v}), default=[])
    a = p.parse_args(argv)
    try:
        print(json.dumps(analyse(read_log(a.log), a.actions), indent=2, allow_nan=False))
    except (OSError, ValueError, TypeError, KeyError) as e:
        print(f'discovery analysis refused: {e}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
