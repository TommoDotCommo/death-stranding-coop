"""Pure checks on the two-process probe reports (mod_probe), shared by the tests and the Windows runner."""


def validate(reports, case):
    if len(reports) != 2:
        raise AssertionError('two process reports required')
    for r in reports:
        if r['failed'] or not r['sent'] or not r['received']:
            raise AssertionError(f'{case}: failed transport or accepted forbidden packet: {r}')
        if case == 'good':
            if not (r['handshake'] and not r['wrong_accepted'] and r['accepted'] > 20 and r['samples'] > 50
                    and 0 <= r['rtt_ms'] < 250
                    and 0 <= r['timing']['first_pong_ms'] < 1000 and 0 <= r['error_p95'] < .30
                    and 0 <= r['error_p99'] < .45):
                raise AssertionError(f'good: handshake/error bounds failed: {r}')
        elif r['handshake'] or r['accepted'] or r['wrong_accepted']:
            raise AssertionError(f'{case}: refusal assertion failed: {r}')

    if case != 'good' and sum(r[case+'_refused'] for r in reports) == 0:
        raise AssertionError(f'{case}: refusal assertion failed: no explicit refusal')


