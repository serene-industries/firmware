#!/usr/bin/env python3
"""Tests of the HE boards' real firmware source, run on a host (see README.md and harness.c).
usage: run.py [<path to the bundled emu-driver.cjs from si-configurator>]
With the driver, every packet is also sent to the configurator's emulated board and the replies compared."""
import sys, os, subprocess, json, random, struct, tempfile

here = os.path.dirname(os.path.abspath(__file__))
repo = os.path.dirname(os.path.dirname(here))
emu = sys.argv[1] if len(sys.argv) > 1 else None
tmp = tempfile.mkdtemp(prefix='fwhost-')
passed = failed = 0

def check(name, cond, detail=''):
    global passed, failed
    if cond: passed += 1
    else:
        failed += 1
        print('  FAIL', name, detail)

class Out:
    def __init__(self, text):
        self.replies, self.matrices, self.keys, self.travel, self.warn, self.travels = [], [], {}, {}, [], []
        self.mode = self.pair = self.layout = self.cal = None; self.pairs = {}; self.events = []; self.reports = []; self.outs = []; self.nows = []; self.lights = []
        for ln in text.splitlines():
            p = ln.split()
            if not p: continue
            if p[0] == 'R': self.replies.append([int(x, 16) for x in p[1:]])
            elif p[0] == 'M': self.matrices.append([int(x, 16) for x in p[1:]])
            elif p[0] == 'K': self.keys[int(p[1])] = dict(zip(('row', 'col', 'act', 'rel', 'dead', 'eng', 'lift', 'floor', 'ceil'), map(int, p[2:])))
            elif p[0] == 'MODE': self.mode = int(p[1])
            elif p[0] == 'PAIR':
                self.pairs[int(p[1])] = (int(p[2]), int(p[3]))
                if p[1] == '0': self.pair = (int(p[2]), int(p[3]))
            elif p[0] == 'CAL': self.cal = int(p[1])
            elif p[0] == 'V': self.travel[int(p[1])] = int(p[2]); self.travels.append((int(p[1]), int(p[2])))
            elif p[0] == 'LAYOUT': self.layout = {p[i]: int(p[i + 1]) for i in range(1, len(p), 2)}
            elif p[0] == 'WARN': self.warn.append(ln)
            elif p[0] == 'E' and p[1] != '.': self.events.append((p[1], int(p[2]), int(p[3]), int(p[4])))
            elif p[0] == 'H' and p[1] != '.': self.reports.append((int(p[1]), [int(x, 16) for x in p[2:]]))
            elif p[0] == 'O' and p[1] != '.': self.outs.append((int(p[1]), [int(x, 16) for x in p[2:]]))
            elif p[0] == 'NOW': self.nows.append(int(p[1]))
            elif p[0] == 'G': self.lights.append((int(p[1]), int(p[2]), int(p[3]), p[4]))

def boot(binary, ee, lines):
    """One power cycle: run the script, return what the firmware printed."""
    r = subprocess.run([binary, ee], input='\n'.join(lines) + '\n', capture_output=True, text=True, timeout=60)
    if r.returncode != 0: raise RuntimeError('harness exit %d: %s' % (r.returncode, r.stderr[-400:]))
    return Out(r.stdout)

def S(*b): return 'S ' + ' '.join('%02x' % x for x in b)
SET, GET = 0x07, 0x08
def kset(r, c, act=0xFF, rel=0xFF, dead=0xFF, eng=0xFF, lift=0xFF): return S(SET, 0, 23, r, c, act, rel, dead, eng, lift)
def kget(r, c): return S(GET, 0, 23, r, c)

def fresh(path, fill=0xFF):
    open(path, 'wb').write(bytes([fill]) * 4096)

def old_image(path, L, post_flash=1, mode=2, act=44, rel=26, bad_key=None, falling=False):
    """An EEPROM as the firmware before per-key settings left it: 3-byte keyboard block, per-key block straight after."""
    ee = bytearray([0x00]) * 4096
    struct.pack_into('<I', ee, L['kbdword'], 3)
    struct.pack_into('<I', ee, L['userdword'], L['usersize'])
    ee[L['base']:L['base'] + 3] = bytes([0, post_flash, mode])
    ceil = []
    for i in range(L['sensors']):
        a, r = (act, rel) if i != bad_key else (20, 80)
        c = (40 + i) if falling else (640 + i)
        ceil.append(c)
        struct.pack_into('<BBHH', ee, L['base'] + 3 + 6 * i, a, r, L['floor'], c)
    end = L['base'] + 3 + L['usersize']
    ee[end:end + 600] = bytes([0xAA]) * 600   # what was the keymap area
    open(path, 'wb').write(ee)
    return ceil

def run_board(board, title):
    print('==', title)
    binary = os.path.join(tmp, board)
    b = subprocess.run([sys.executable, os.path.join(here, 'build.py'), repo, board, binary, '--uid'], capture_output=True, text=True)
    if b.returncode != 0:
        print(b.stderr[-2000:]); check('builds', False); return
    ee = os.path.join(tmp, board + '.ee')
    W, A, Sk, D, F, G = (1, 2), (2, 1), (2, 2), (2, 3), (2, 4), (2, 5)

    # ---- 1. a fresh board
    for fill in (0xFF, 0x00):
        fresh(ee, fill)
        o = boot(binary, ee, ['L', S(GET, 0, 22), kget(*A), S(GET, 0, 24, 0), S(GET, 0, 25), S(GET, 0, 6), 'D'])
        L = o.layout
        check('no two keys share a mux channel', not o.warn, str(o.warn[:2]))
        info = o.replies[0]
        check('info: protocol, sensors, rows, cols', info[3:7] == [1, L['sensors'], L['rows'], L['cols']], str(info[:9]))
        check('info: features per-key, pairs, stored, id, calibration, synth, fast stream, moving stream; 8 pairs', info[7] == 0xFF and info[8] == 8, '%s %s' % (hex(info[7]), info[8]))
        check('fresh key reads 50 30 15 10 10', o.replies[1][5:11] == [1, 50, 30, 15, 10, 10], str(o.replies[1][:11]))
        check('fresh pair is A and D', o.replies[2][4:9] == [1, 2, 1, 2, 3], str(o.replies[2][:9]))
        check('board id has 12 bytes', any(o.replies[3][3:15]) and not any(o.replies[3][15:]), str(o.replies[3][:16]))
        check('fresh mode is normal', o.replies[4][3] == 0 and o.mode == 0)
        check('fresh: every key 50/30, ceiling default', all(k['act'] == 50 and k['rel'] == 30 and k['ceil'] == L['ceiling'] for k in o.keys.values()))
        check('kb block size is 5 + 2 per pair + 3 per sensor', L['kbsize'] == 5 + 2 * 8 + 3 * L['sensors'])
    sensor = {(k['row'], k['col']): s for s, k in o.keys.items()}
    sW, sA, sS, sD, sF, sG = (sensor[x] for x in (W, A, Sk, D, F, G))
    sQ, sE = sensor[(1, 1)], sensor[(1, 3)]
    def bit(m, pos): return (m[pos[0]] >> pos[1]) & 1
    empty = next((r, c) for r in range(L['rows'] - 1, -1, -1) for c in range(L['cols'] - 1, -1, -1) if (r, c) not in sensor)

    # ---- 2. one key's levels: set, read back, used by the scan at once, kept over a power cycle
    fresh(ee)
    o = boot(binary, ee, [kset(*W, 22, 12), kget(*W), kget(*A), 'D', 'T 2500'])
    check('set W 22/12 reads back', o.replies[1][5:8] == [1, 22, 12], str(o.replies[1][:11]))
    check('A untouched', o.replies[2][5:8] == [1, 50, 30])
    check('the scan uses W 22/12 at once', (o.keys[sW]['act'], o.keys[sW]['rel']) == (22, 12))
    o = boot(binary, ee, [kget(*W), kget(*A), 'D'])
    check('W 22/12 after a power cycle', o.replies[0][5:8] == [1, 22, 12] and (o.keys[sW]['act'], o.keys[sW]['rel']) == (22, 12), str(o.replies[0][:11]))
    check('the others still 50/30 after a power cycle', all((k['act'], k['rel']) == (50, 30) for s, k in o.keys.items() if s != sW))

    # ---- 3. what the firmware refuses or clamps
    fresh(ee)
    o = boot(binary, ee, [kset(*W, 5, 3), kget(*W), kset(*W, 40, 40), kget(*W), kset(*W, 95, 30), kget(*W), kset(*W, 0xFF, 20), kget(*W),
                          kset(*W, 0xFF, 0xFF, 200, 0, 99), kget(*W), kset(*W, 0xFF, 0xFF, 1, 0xFF, 0xFF), kget(*W),
                          kset(*empty, 22, 12), kget(*empty), kset(9, 40, 22, 12), kget(9, 40), 'D'])
    r = o.replies
    check('actuate 5 refused', r[1][6:8] == [50, 30])
    check('release not under actuate refused', r[3][6:8] == [50, 30])
    check('actuate 95 refused', r[5][6:8] == [50, 30])
    check('0xFF leaves actuate, release 20 taken', r[7][6:8] == [50, 20], str(r[7][:11]))
    check('rapid-trigger numbers clamped 60 / 2 / 50', r[9][8:11] == [60, 2, 50], str(r[9][:11]))
    check('deadzone 1 raised to 15, others left', r[11][8:11] == [15, 2, 50], str(r[11][:11]))
    check('a position with no sensor says so', r[13][5] == 0 and r[15][5] == 0, str(r[13][:8]))
    check('nothing else moved', all((k['act'], k['rel']) == (50, 30) for s, k in o.keys.items() if s != sW))

    # ---- 4. the key-cancel pairs: eight slots, a key in one pair only
    NONE = [0xFF] * 4
    def pget(i): return S(GET, 0, 24, i)
    def pset(i, a, b): return S(SET, 0, 24, i, *(a or (0xFF, 0xFF)), *(b or (0xFF, 0xFF)))
    fresh(ee)
    o = boot(binary, ee, [pset(0, W, Sk), pget(0), pset(0, A, None), pget(0), pset(0, A, A), pget(0), pset(0, empty, A), pget(0),
                          pget(8), pset(8, A, D), pget(7),
                          pset(1, A, D), pget(1), pget(0), pset(2, W, F), pget(2), pset(1, Sk, D), pget(1), pset(1, D, A), pget(1), 'T 2500'])
    r = o.replies
    check('pair 0 = W + S reads back', r[1][4:9] == [1, *W, *Sk], str(r[1][:9]))
    check('half a pair refused', r[3][5:9] == [*W, *Sk])
    check('a key with itself refused', r[5][5:9] == [*W, *Sk])
    check('a position with no key refused', r[7][5:9] == [*W, *Sk])
    check('there is no ninth pair', r[8][4] == 0 and r[10][4:9] == [1, *NONE], str(r[10][:9]))
    check('a second pair A + D beside the first', r[12][4:9] == [1, *A, *D] and r[13][5:9] == [*W, *Sk], str(r[12][:9]))
    check('a key already in a pair is refused for another', r[15][5:9] == NONE and r[17][5:9] == [*A, *D], '%s %s' % (r[15][:9], r[17][:9]))
    check('a pair can be rewritten in its own slot', r[19][5:9] == [*D, *A], str(r[19][:9]))
    o = boot(binary, ee, [pget(0), pget(1), 'D', pset(0, None, None), pget(0), pget(1), pset(2, W, F), pget(2), 'T 2500'])
    check('both pairs after a power cycle', o.replies[0][5:9] == [*W, *Sk] and o.replies[1][5:9] == [*D, *A] and o.pairs[0] == (sW, sS) and o.pairs[1] == (sD, sA), str(o.pairs))
    check('an emptied slot frees its keys for another pair', o.replies[3][4:9] == [1, *NONE] and o.replies[4][5:9] == [*D, *A] and o.replies[6][5:9] == [*W, *F], str(o.replies[6][:9]))
    o = boot(binary, ee, [pget(0), pget(1), pget(2), 'D'])
    check('empty slot and the others after a power cycle', o.replies[0][5:9] == NONE and o.replies[1][5:9] == [*D, *A] and o.replies[2][5:9] == [*W, *F] and o.pairs[0] == (0xFF, 0xFF), str(o.pairs))
    pair_none = S(SET, 0, 24, 0, 0xFF, 0xFF, 0xFF, 0xFF)

    # ---- 5. key cancel in the scan, with the pair the user chose
    def cancel_run(pre):
        fresh(ee)
        return boot(binary, ee, pre + [S(SET, 0, 6, 2),
                    'A %d 100' % sW, 'N 8', 'M', 'A %d 100' % sS, 'N 8', 'M', 'A %d 0' % sS, 'N 8', 'M', 'A %d 0' % sW, 'N 8', 'M',
                    'A %d 100' % sA, 'N 8', 'M', 'A %d 100' % sD, 'N 8', 'M', 'A %d 0' % sD, 'N 8', 'M', 'A %d 0' % sA, 'N 8', 'M']).matrices
    m = cancel_run([S(SET, 0, 24, 0, *W, *Sk)])
    check('pair W+S: W down', bit(m[0], W) == 1 and bit(m[0], Sk) == 0)
    check('pair W+S: S pressed, W let go', bit(m[1], W) == 0 and bit(m[1], Sk) == 1, str(m[1]))
    check('pair W+S: S lifted, W comes back', bit(m[2], W) == 1 and bit(m[2], Sk) == 0, str(m[2]))
    check('pair W+S: both up', m[3] == [0] * L['rows'])
    check('pair W+S: A and D no longer cancel', bit(m[5], A) == 1 and bit(m[5], D) == 1 and bit(m[6], A) == 1 and bit(m[6], D) == 0 and m[7] == [0] * L['rows'], str(m[5]))
    m = cancel_run([])
    check('default pair: W and S do not cancel', bit(m[1], W) == 1 and bit(m[1], Sk) == 1)
    check('default pair: D pressed, A let go', bit(m[5], A) == 0 and bit(m[5], D) == 1, str(m[5]))
    check('default pair: D lifted, A comes back', bit(m[6], A) == 1 and bit(m[6], D) == 0)
    m = cancel_run([pair_none])
    check('no pair: nothing cancels', bit(m[1], W) == 1 and bit(m[1], Sk) == 1 and bit(m[5], A) == 1 and bit(m[5], D) == 1)
    check('no pair: releases still work', m[3] == [0] * L['rows'] and m[7] == [0] * L['rows'])

    fresh(ee)
    def held(*sensors): return ['A -1 0'] + ['A %d 100' % x for x in sensors] + ['N 8', 'M']
    o = boot(binary, ee, [pset(1, W, Sk), S(SET, 0, 6, 2), *held(sA), *held(sA, sW), *held(sA, sW, sD), *held(sA, sW, sD, sS), *held(sA, sW, sS), *held(sA, sW), *held(),
                          *held(sA), *held(sA, sF), *held(sA, sF, sD), *held(sA, sF), *held()])
    m = o.matrices
    def downs(mm, *pos): return sorted((r, c) for r in range(L['rows']) for c in range(L['cols']) if (mm[r] >> c) & 1) == sorted(pos)
    check('two pairs: A, then W: both down', downs(m[1], A, W), str(m[1]))
    check('two pairs: D pressed lets A go, W stays', downs(m[2], D, W), str(m[2]))
    check('two pairs: S pressed lets W go', downs(m[3], D, Sk), str(m[3]))
    check('two pairs: D lifted, A comes back; S still wins over W', downs(m[4], A, Sk), str(m[4]))
    check('two pairs: S lifted, W comes back', downs(m[5], A, W), str(m[5]))
    check('two pairs: all up', downs(m[6]))
    check('a key outside the pair pressed in between does not disturb it', downs(m[8], A, F) and downs(m[9], F, D) and downs(m[10], A, F) and downs(m[11]), str(m[9]))

    # ---- 6. per-key levels in the scan, normal mode and rapid trigger
    fresh(ee)
    o = boot(binary, ee, [kset(*W, 22, 12), 'A -1 30', 'N 8', 'M', 'V %d' % sW, 'A -1 10', 'N 8', 'M'])
    only_w = [0] * L['rows']; only_w[W[0]] = 1 << W[1]
    check('normal: at 30 % only W (22) is down', o.matrices[0] == only_w, str(o.matrices[0]))
    check('the scan saw 30 %', o.travel[sW] == 30, str(o.travel))
    check('normal: at 10 % W (release 12) is up', o.matrices[1] == [0] * L['rows'])
    fresh(ee)
    o = boot(binary, ee, [S(SET, 0, 6, 1), kset(*W, 0xFF, 0xFF, 30), 'A -1 28', 'N 8', 'M', 'A -1 45', 'N 8', 'M', 'A -1 0', 'N 8', 'M'])
    m = o.matrices
    enc = L['sensors'] == 69   # the Icebreaker's encoder click sits in the sensor list without a sensor
    others_down = all(bit(m[0], (k['row'], k['col'])) == 1 for s, k in o.keys.items() if s != sW and not (enc and s == 68))
    check('rapid trigger: at 28 % every key but W (deadzone 30) is down', others_down and bit(m[0], W) == 0, str(m[0]))
    check('rapid trigger: at 45 % W is down too', bit(m[1], W) == 1)
    check('rapid trigger: all up at rest', m[2] == [0] * L['rows'], str(m[2]))

    # ---- 6b. rapid trigger lets a key go at deadzone minus 8, so the firmware keeps the deadzone at 15 or more
    for rest in (0, 3, 6):
        fresh(ee)
        o = boot(binary, ee, [S(SET, 0, 6, 1), kset(*W, 0xFF, 0xFF, 5), S(SET, 0, 7, 5), S(GET, 0, 7), kget(*W),
                              'A -1 %d' % rest, 'N 8', 'A -1 80', 'N 8', 'M', 'A -1 %d' % rest, 'N 12', 'M'])
        check('a deadzone of 5 is stored as 15 (all keys and one key)', o.replies[3][3:5] == [0, 15] and o.replies[4][8] == 15, str(o.replies[4][:11]))
        check('rapid trigger at rest level %d: every key comes back up' % rest, any(o.matrices[0]) and o.matrices[1] == [0] * L['rows'], str(o.matrices[1]))

    # ---- 6b. a level change is felt by a held key: no save, no power cycle, no wait for the one-key command
    fresh(ee)
    o = boot(binary, ee, ['A %d 40' % sW, 'X 40 20', 'M',                    # W held at 40 % of its travel: under the default 50, so up
                          kset(*W, 30, 20), 'X 40 20', 'M',                  # one key's levels 30 / 20: down within 20 ms
                          kset(*W, 80, 60), 'X 40 20', 'M',                  # 80 / 60: up again
                          S(SET, 0, 2, 20), S(SET, 0, 1, 30), 'X 40 20', 'M',  # the all-keys commands (release first when going down): not yet
                          'T 1500', 'X 40 20', 'M', 'T 1000', 'X 40 20', 'M',  # not at 1.5 s; there by 2.5 s
                          S(SET, 0, 1, 80), S(SET, 0, 2, 60), 'T 2500', 'X 40 20', 'M'])
    m = [bit(x, W) for x in o.matrices]
    check('a key held at 40 %: up at the default 50', m[0] == 0, str(m))
    check('one key set to 30 / 20: the held key goes down at once', m[1] == 1, str(m))
    check('one key set to 80 / 60: the held key comes up at once', m[2] == 0, str(m))
    check('all keys set to 30 / 20: nothing yet, nothing at 1.5 s', m[3] == 0 and m[4] == 0, str(m))
    check('...the held key is down once the 2 s have passed', m[5] == 1, str(m))
    check('all keys set to 80 / 60: the held key comes up after 2 s', m[6] == 0, str(m))
    check('no other key moved through any of it', all(not any((x[r] >> c) & 1 for (r, c) in sensor if (r, c) != W) for x in o.matrices))
    # going down with actuate first is not the same: actuate may not pass a key's release, so it stops one above it
    fresh(ee)
    o = boot(binary, ee, [kset(*W, 80, 60), S(SET, 0, 1, 30), 'T 2500', 'D'])
    check('all-keys actuate 30 on a key whose release is 60 lands at 61; a key at 50 / 30 lands at 31', o.keys[sW]['act'] == 61 and o.keys[sA]['act'] == 31, '%s %s' % (o.keys[sW]['act'], o.keys[sA]['act']))

    # ---- 7. the all-keys commands still work, and land 2 s later
    fresh(ee)
    o = boot(binary, ee, [kset(*W, 22, 12), S(SET, 0, 1, 60), S(GET, 0, 1), kget(*W), 'T 2500', S(GET, 0, 1), 'D',
                          S(SET, 0, 7, 20), S(SET, 0, 8, 12), S(SET, 0, 9, 8), S(SET, 0, 6, 1), 'T 2500'])
    check('all-keys actuate 60: the getter still says 50', o.replies[2][3] == 50)
    check('...and says 60 after 2 s', o.replies[4][3] == 60)
    check('all-keys actuate overrides the per-key value', all(k['act'] == 60 for k in o.keys.values()))
    o = boot(binary, ee, [S(GET, 0, 6), S(GET, 0, 7), S(GET, 0, 8), S(GET, 0, 9), kget(*W), 'D'])
    check('mode kept over a power cycle', o.replies[0][3] == 1 and o.mode == 1)
    check('deadzone answers in two bytes, kept', o.replies[1][3:5] == [0, 20])
    check('down and up kept', o.replies[2][3] == 12 and o.replies[3][3] == 8)
    check('levels kept', o.replies[4][5:11] == [1, 60, 12, 20, 12, 8], str(o.replies[4][:11]))

    # ---- 8. a board coming from the firmware before per-key settings keeps its levels and calibration
    falling = L['ceiling'] < L['floor']
    ceil = old_image(ee, L, falling=falling)
    o = boot(binary, ee, ['D', S(GET, 0, 24, 0), S(GET, 0, 22)])
    check('carried: levels 44/26 on every key', all((k['act'], k['rel']) == (44, 26) for k in o.keys.values()), str(o.keys[0]))
    check('carried: calibration of every key', all(o.keys[i]['ceil'] == ceil[i] for i in range(L['sensors'])), '%s vs %s' % (o.keys[3]['ceil'], ceil[3]))
    check('carried: starts in normal, pair A and D', o.mode == 0 and o.replies[0][5:9] == [2, 1, 2, 3])
    img = open(ee, 'rb').read()
    check('carried: keyboard block rewritten in the new layout', struct.unpack_from('<I', img, L['kbdword'])[0] == L['kbsize'] and img[L['base'] + 3:L['base'] + 5] == bytes([0x48, 0xE2]))
    o2 = boot(binary, ee, ['D'])
    check('carried: the same after the next power cycle', all(o2.keys[i]['ceil'] == ceil[i] and o2.keys[i]['act'] == 44 for i in range(L['sensors'])))
    old_image(ee, L, post_flash=0, falling=falling)
    o = boot(binary, ee, ['D'])
    check('old board that never finished a first boot: defaults', all((k['act'], k['rel'], k['ceil']) == (50, 30, L['ceiling']) for k in o.keys.values()))
    ceil = old_image(ee, L, bad_key=7, falling=falling)
    o = boot(binary, ee, ['D'])
    check('carried: a key with nonsense levels gets defaults, its calibration stays', (o.keys[7]['act'], o.keys[7]['rel'], o.keys[7]['ceil']) == (50, 30, ceil[7]) and o.keys[8]['act'] == 44, str(o.keys[7]))

    # ---- 9. calibration. A key is done when its press bottoms out: still at its deepest point for a tenth of a
    #         second, far enough from rest, and not far shallower than the other keys. Only done keys are saved.
    def cstate(): return S(GET, 0, 26, 0)
    def ckey(pos): return S(GET, 0, 26, 1, *pos)
    def bits(r, base): return sorted((row, c) for row in range(L['rows']) for c in range(16) if ((r[base + 2 * row] | (r[base + 1 + 2 * row] << 8)) >> c) & 1)
    def moved_keys(r): return bits(r, 5)
    def done_keys(r): return bits(r, 5 + 2 * L['rows'])
    def u16(r, i): return (r[i] << 8) | r[i + 1]
    sgn = -1 if falling else 1
    span = abs(L['ceiling'] - L['floor'])
    def at(frac): return L['floor'] + sgn * int(span * frac)        # a raw level that far into the expected travel
    def hold(sensor, raw, ms=130): return ['RAW %d %d' % (sensor, raw), 'N 2', 'T %d' % ms, 'N 2']   # press and stay
    def tap(sensor, raw): return ['RAW %d %d' % (sensor, raw), 'N 2', 'RAW %d %d' % (sensor, L['floor']), 'N 2', 'T 130', 'N 2']   # down and straight back up
    def let_go(sensor): return ['RAW %d %d' % (sensor, L['floor']), 'N 2']
    bF, bG = at(0.9), at(0.8)
    if not falling:
        fresh(ee)
        o3 = boot(binary, ee, ['RAW %d 660' % sF, 'N 2', 'V %d' % sF])
        check('default calibration: a press that bottoms at 660 reads 75 %', o3.travel[sF] == 75, str(o3.travel))
    ceil = old_image(ee, L, falling=falling)       # a board that has a calibration already
    o = boot(binary, ee, [cstate(), ckey(F), S(SET, 0, 4), cstate(), 'RAW %d %d' % (sF, bF), 'N 2', 'V %d' % sF, cstate(), ckey(F), 'T 130', 'N 2', cstate(), ckey(F), ckey(A),
                          *hold(sG, bG), cstate(), S(SET, 0, 26, 1, *G), cstate(), ckey(G), *let_go(sF), *let_go(sG), cstate(), S(SET, 0, 5), cstate(), 'D'])
    r = o.replies
    check('calibration: off at first, nothing moved', r[0][4] == 0 and moved_keys(r[0]) == [] and done_keys(r[0]) == [], str(r[0][:26]))
    check('calibration: a key reports rest, bottom and stored bottom', r[1][6] == 1 and u16(r[1], 7) == L['floor'] and u16(r[1], 9) == ceil[sF] and u16(r[1], 11) == ceil[sF] and r[1][13] == 0, str(r[1][:14]))
    check('calibration: started, still nothing moved', r[3][4] == 1 and moved_keys(r[3]) == [], str(r[3][:16]))
    want_t = abs(bF - L['floor']) * 100 // abs(ceil[sF] - L['floor'])
    check('calibration: travel is shown against the stored level meanwhile', o.travel[sF] == min(100, want_t), '%s want %s' % (o.travel, want_t))
    check('calibration: F just arrived at the bottom: moved, not done yet', moved_keys(r[4]) == [F] and done_keys(r[4]) == [] and r[5][13] == 1, str(r[5][:14]))
    check('calibration: F still there a tenth of a second later: done', done_keys(r[6]) == [F] and r[7][13] == 3 and u16(r[7], 9) == bF and u16(r[7], 11) == ceil[sF], str(r[7][:14]))
    check('calibration: a key not pressed says so', r[8][13] == 0 and u16(r[8], 11) == ceil[sA], str(r[8][:14]))
    check('calibration: two keys done', done_keys(r[9]) == sorted([F, G]), str(done_keys(r[9])))
    check('calibration: one key put back', moved_keys(r[11]) == [F] and done_keys(r[11]) == [F] and r[12][13] == 0 and u16(r[12], 11) == ceil[sG], str(r[12][:14]))
    check('calibration: letting go changes nothing', done_keys(r[13]) == [F])
    check('calibration: saved, mode off', r[15][4] == 0 and o.cal == 0)
    check('calibration: the done key has its new bottom', o.keys[sF]['ceil'] == bF, str(o.keys[sF]))
    check('calibration: every other key keeps the bottom it had', all(o.keys[i]['ceil'] == ceil[i] for i in range(L['sensors']) if i != sF), str(o.keys[sG]))
    o = boot(binary, ee, ['D', 'RAW %d %d' % (sF, bF), 'N 2', 'V %d' % sF])
    check('calibration: kept over a power cycle, and the same press now reads 100', o.keys[sF]['ceil'] == bF and o.keys[sG]['ceil'] == ceil[sG] and o.travel[sF] == 100, '%s %s' % (o.keys[sF], o.travel))
    # what must not count as the bottom
    o = boot(binary, ee, [S(SET, 0, 4), *tap(sG, bG), cstate(), ckey(G),
                          'RAW %d %d' % (sA, at(0.3)), 'N 2', 'T 60', 'RAW %d %d' % (sA, at(0.5)), 'N 2', 'T 60', 'RAW %d %d' % (sA, at(0.7)), 'N 2', 'T 60', 'N 2', cstate(),
                          *let_go(sA), *hold(sD, at(0.15)), cstate(), S(SET, 0, 5), 'D'])
    r = o.replies
    check('calibration: a tap is not the bottom, however deep', done_keys(r[1]) == [] and moved_keys(r[1]) == [G] and r[2][13] == 1, str(r[2][:14]))
    check('calibration: a key still creeping down is not done', A not in done_keys(r[3]), str(done_keys(r[3])))
    check('calibration: a brush held still is not a press', D not in done_keys(r[4]), str(done_keys(r[4])))
    check('calibration: none of those is stored', all(o.keys[i]['ceil'] == (bF if i == sF else ceil[i]) for i in range(L['sensors'])), str([o.keys[x]['ceil'] for x in (sG, sA, sD)]))
    # a light press held still passes for a bottom only until the board has seen what a bottom is
    light, full = at(0.35), at(0.9)
    o = boot(binary, ee, [S(SET, 0, 4), *hold(sG, light), cstate(), *let_go(sG), *hold(sQ, full), *let_go(sQ), cstate(), *hold(sE, full), *let_go(sE), cstate(), ckey(G),
                          *hold(sA, light), cstate(), *let_go(sA), *hold(sA, at(0.6)), *let_go(sA), *hold(sA, full), cstate(), ckey(A), S(SET, 0, 5), 'D'])
    r = o.replies
    check('calibration: a light press held still is taken at first', done_keys(r[1]) == [G], str(done_keys(r[1])))
    check('calibration: once other keys have bottomed out it goes back to not pressed', G not in done_keys(r[3]) and G not in moved_keys(r[3]) and r[4][13] == 0 and done_keys(r[3]) == sorted([(1, 1), (1, 3)]), '%s %s' % (done_keys(r[2]), done_keys(r[3])))
    check('calibration: after that a light press held still is not taken at all', A not in done_keys(r[5]), str(done_keys(r[5])))
    check('calibration: a deeper press replaces a shallower one', A in done_keys(r[6]) and u16(r[7], 9) == full, str(r[7][:14]))
    check('calibration: saved levels are the bottoms, the light key kept its own', o.keys[sQ]['ceil'] == full and o.keys[sE]['ceil'] == full and o.keys[sA]['ceil'] == full and o.keys[sG]['ceil'] == ceil[sG], str([o.keys[x]['ceil'] for x in (sQ, sE, sA, sG)]))
    # leaving, and saving with nothing pressed
    before = {i: o.keys[i]['ceil'] for i in range(L['sensors'])}
    o = boot(binary, ee, [S(SET, 0, 4), *hold(sG, bG), cstate(), S(SET, 0, 26, 0), cstate(), 'D', S(SET, 0, 26, 0), S(SET, 0, 5), 'D'])
    check('calibration: left without saving, nothing kept', done_keys(o.replies[1]) == [G] and o.replies[3][4] == 0 and o.cal == 0 and all(o.keys[i]['ceil'] == before[i] for i in before), str(o.keys[sG]))
    o = boot(binary, ee, ['D'])
    check('calibration: and nothing stored by leaving', all(o.keys[i]['ceil'] == before[i] for i in before))
    o = boot(binary, ee, [S(SET, 0, 4), 'N 6', 'T 200', 'N 6', S(SET, 0, 5), 'D'])
    check('calibration: started and saved with no key pressed changes nothing', all(o.keys[i]['ceil'] == before[i] for i in before), str(o.keys[0]))

    # ---- 10. the configurator's emulated board answers every packet as the firmware does
    for seed in (range(int(os.environ.get('SEEDS', '3'))) if emu else []):
        rng = random.Random(20261125 + L['sensors'] + 1000 * seed)
        spots = list(sensor.keys())
        script = []
        def byte(): return rng.choice([0, 1, 2, 5, 9, 10, 11, 12, 20, 22, 26, 30, 44, 50, 51, 60, 89, 90, 91, 120, 200, 254, 255])
        lo, hi = (L['floor'] - 40, L['floor'] + 220) if not falling else (0, L['floor'] + 50)
        real = [i for i in range(L['sensors']) if not (enc and i == 68)]
        # the keys most often moved: four that type, five of the piano (white and black), and the two octave keys
        busy = real[:4] + [sensor[q] for q in ((2, 1), (2, 2), (2, 5), (1, 2), (1, 5), (3, 1), (3, 2))]
        # the base-layer keycodes, the same on both sides (the instrument's notes no longer follow them: they go by place)
        for (r_, c_) in spots: script.append(('KC', [r_, c_, 4 + ((r_ * L['cols'] + c_) % 26)]))
        for _ in range(1500):
            x = rng.random()
            pos = rng.choice(spots) if rng.random() < 0.9 else empty
            if rng.random() < 0.07:   # the instrument: the mode, its settings, its read-back
                y = rng.random()
                script.append(('S', [SET, 0, 27, 0, rng.choice([0, 1, 1])] if y < 0.4 else [GET, 0, 27, 0] if y < 0.8 else [SET, 0, 27, 1, byte(), byte(), byte(), byte(), rng.choice([0, 1, 2, 3, 4, 9, 255]), byte()]))
                continue
            if rng.random() < 0.09:   # the DAW bridge: the clock, events to go out, keys as controls, the read-backs
                y = rng.random()
                if y < 0.30: script.append(('C', rng.choice([[0xF8], [0xF8], [0xF8], [0xF8], [0xFA], [0xFB], [0xFC], [0xF2, rng.randint(0, 127), rng.randint(0, 3)]])))
                elif y < 0.45: script.append(('S', [GET, 0, 28, 0]))
                elif y < 0.55: script.append(('S', [GET, 0, 28, 1, rng.choice([0, 1, 2, 3, 11, 12, 200])]))
                elif y < 0.75:
                    cpos = rng.choice([(o.keys[i]['row'], o.keys[i]['col']) for i in busy]) if rng.random() < 0.8 else rng.choice([pos, (0xFF, 0xFF)])
                    script.append(('S', [SET, 0, 28, 2, rng.choice([0, 1, 2, 3, 11, 12]), cpos[0], cpos[1], rng.choice([0, 1, 1, 2, 3, 4, 5]), byte(), byte()]))
                elif y < 0.80: script.append(('S', [SET, 0, 28, 3]))
                elif y < 0.88: script.append(('S', [SET, 0, 28, 4, rng.choice([0, 1])]))
                else:
                    n = rng.randint(0, 6)
                    script.append(('S', [SET, 0, 28, 1, n] + [x for _ in range(min(n, 5)) for x in (rng.choice([0, 16, 200, 255]), rng.choice([0, 39, 40, 45, 60, 200]), rng.choice([0x90, 0x80, 0xB0, 0xE1, 0x7F, 0xF8, 0xD2]), byte(), byte())]))
                continue
            if x < 0.08: script.append(('S', [SET, 0, 1, byte()]))
            elif x < 0.16: script.append(('S', [SET, 0, 2, byte()]))
            elif x < 0.18: script.append(('S', [SET, 0, 3]))
            elif x < 0.22: script.append(('S', [SET, 0, 6, rng.choice([0, 1, 2])]))
            elif x < 0.28: script.append(('S', [SET, 0, rng.choice([7, 8, 9]), byte()]))
            elif x < 0.40: script.append(('S', [SET, 0, 23, pos[0], pos[1], byte(), byte(), byte(), byte(), byte()]))
            elif x < 0.50:
                p2 = rng.choice(spots[:12] if rng.random() < 0.5 else spots)   # a small pool, so keys collide across pairs
                p1 = rng.choice(spots[:12]) if rng.random() < 0.5 else pos
                script.append(('S', [SET, 0, 24, rng.choice([0, 0, 1, 1, 2, 3, 7, 8]), *(p1 if rng.random() < 0.85 else (0xFF, 0xFF)), *(p2 if rng.random() < 0.85 else (0xFF, 0xFF))]))
            elif x < 0.58: script.append(('S', [GET, 0, 23, pos[0], pos[1]]))
            elif x < 0.64: script.append(('S', [GET, 0, rng.choice([1, 2, 6, 7, 8, 9])]))
            elif x < 0.69: script.append(('S', [GET, 0, 24, rng.choice([0, 0, 1, 2, 3, 7, 8])]))
            elif x < 0.71: script.append(('S', [GET, 0, rng.choice([22, 25])]))
            elif x < 0.72: script.append(('S', [0x09, 0]))
            elif x < 0.74: script.append(('S', [SET, 0, 4]))
            elif x < 0.755: script.append(('S', [SET, 0, 5]))
            elif x < 0.765: script.append(('S', [SET, 0, 26, 0]))
            elif x < 0.78: script.append(('S', [SET, 0, 26, 1, pos[0], pos[1]]))
            elif x < 0.81: script.append(('S', [GET, 0, 26, 0]))
            elif x < 0.84: script.append(('S', [GET, 0, 26, 1, pos[0], pos[1]]))
            elif x < 0.89: script.append(('RAW', [rng.choice(busy if rng.random() < 0.6 else real), rng.randint(lo, hi)]))
            elif x < 0.92: script.append(('N', rng.randint(1, 6)))
            elif x < 0.95: script.append(('V', rng.choice(busy if rng.random() < 0.6 else real)))
            else: script.append(('T', rng.choice([100, 400, 900, 1500, 1990, 2010, 2500, 4000])))
        script.append(('E', 0)); script.append(('O', 0))
        fresh(ee)
        line = {'KC': lambda a: 'KC %d %d %x' % tuple(a), 'E': lambda a: 'E', 'S': lambda a: 'S ' + ' '.join('%02x' % b for b in a), 'T': lambda a: 'T %d' % a, 'RAW': lambda a: 'RAW %d %d' % tuple(a), 'N': lambda a: 'N %d' % a, 'V': lambda a: 'V %d' % a, 'C': lambda a: 'C ' + ' '.join('%02x' % b for b in a), 'O': lambda a: 'O'}
        fwo = boot(binary, ee, [line[op](a) for op, a in script]); fw = fwo.replies
        opts = dict(name=title, vendorId=0x5363, productId=0x0010, rows=L['rows'], cols=L['cols'], sensors=[[o.keys[i]['row'], o.keys[i]['col']] for i in range(L['sensors'])],
                    defaultPair=[sA, sD], rest=L['floor'], bottom=L['ceiling'], calStart=L['calstart'], alias=({'68': 18} if enc else {}), noTravel=([68] if enc else []), synth=True, hasMidi=False, clockAtBoot=10000, perKey=True, uid=[0x44, 0x33, 0x22, 0x11, 0x88, 0x77, 0x66, 0x55, 0xCC, 0xBB, 0xAA, 0x99], version=[1, 0, 5, 0])
        job = os.path.join(tmp, board + '.job.json'); json.dump(dict(options=opts, script=script), open(job, 'w'))
        r = subprocess.run(['node', emu, job], capture_output=True, text=True, timeout=120)
        em = [[int(x, 16) for x in ln.split()[1:]] for ln in r.stdout.splitlines() if ln.startswith('R')]
        emv = [tuple(int(x) for x in ln.split()[1:]) for ln in r.stdout.splitlines() if ln.startswith('V')]
        sends = [a for op, a in script if op == 'S']
        eme = [(p_[1], int(p_[2]), int(p_[3]), int(p_[4])) for p_ in (ln.split() for ln in r.stdout.splitlines() if ln.startswith('E ') and ln != 'E .')]
        emo = [[int(x, 16) for x in ln.split()[2:]] for ln in r.stdout.splitlines() if ln.startswith('O ') and ln != 'O .']
        fwout = [m for t, m in fwo.outs]
        check('emulated board sends the DAW what the firmware sends through the bridge (%d messages)' % len(fwout), emo == fwout, 'first difference: %s' % str(next(((i, a, b) for i, (a, b) in enumerate(zip(fwout, emo)) if a != b), (len(fwout), len(emo)))))
        check('emulated board plays the notes the firmware plays (%d events)' % len(fwo.events), eme == fwo.events, 'first difference: %s' % str(next(((i, a, b) for i, (a, b) in enumerate(zip(fwo.events, eme)) if a != b), (len(fwo.events), len(eme)))))
        check('emulated board answered every packet', len(em) == len(fw) == len(sends), '%d %d %d %s' % (len(em), len(fw), len(sends), r.stderr[-300:]))
        diffs = [(i, sends[i], fw[i][:12], em[i][:12]) for i in range(min(len(em), len(fw))) if fw[i] != em[i]]
        check('emulated board shows the travel the firmware streams (%d readings)' % len(emv), emv == fwo.travels, str([(a, b) for a, b in zip(emv, fwo.travels) if a != b][:4]))
        check('emulated board = firmware on %d packets' % len(sends), not diffs, '\n    first differences (packet, firmware, emulated): ' + '\n    '.join(str(d) for d in diffs[:4]))

def run_synth(board, title):
    """The instrument mode: the scan rate, the piano laid over the board, a strike timed between two depths, pressure, the note's end, the octave keys, and typing kept apart."""
    print('==', title, '(instrument)')
    binary = os.path.join(tmp, board); ee = os.path.join(tmp, board + '.synth.ee')
    fresh(ee)
    o = boot(binary, ee, ['D'])
    at = {(k['row'], k['col']): s for s, k in o.keys.items()}
    A, S_, W, Q, Z, X_, QUOTE, RB = at[(2, 1)], at[(2, 2)], at[(1, 2)], at[(1, 1)], at[(3, 1)], at[(3, 2)], at[(2, 11)], at[(1, 12)]
    KEYS = ['KC 2 1 1d', 'KC 1 1 04']                                    # the keymap says Z where A sits and A where Q sits: the notes go by place, not by this
    WARM = ['X 600 600']                                                 # a thousand scans a second
    ON, OFF = S(SET, 0, 27, 0, 1) + '\nX 5 5', S(SET, 0, 27, 0, 0)   # the mode on, then a few scans with the keys at rest
    GETS = S(GET, 0, 27, 0)
    def strike(s, scans, to=1000, start=0):                              # travel from start to `to` in that many scans, a millisecond each
        return ['P %d %d' % (s, start)] + [l for i in range(1, scans + 1) for l in ('P %d %d' % (s, start + (to - start) * i // scans), 'X 1 1')]
    def up(s): return ['P %d 0' % s, 'X 5 5']
    def bit(m, pos): return (m[pos[0]] >> pos[1]) & 1

    o = boot(binary, ee, KEYS + WARM + [GETS, S(GET, 0, 22)])
    r, info = o.replies[0], o.replies[1]
    check('off at power-up; 1000 scans a second measured', r[4] == 0 and 990 <= r[5] * 256 + r[6] <= 1010, str(r[:10]))
    check('depths 12 / 60 / 30, channel 1, middle octave, pressure on', r[7:13] == [12, 60, 30, 0, 2, 1], str(r[7:13]))
    check('a host build says it has no MIDI interface, and that its notes are the piano by place', r[19] == 0 and r[20] == 1, str(r[19:21]))
    check('info carries the scan rate too', 990 <= info[9] * 256 + info[10] <= 1010, str(info[:12]))

    # the mode off: note keys type, nothing is played
    o = boot(binary, ee, KEYS + WARM + strike(A, 8) + ['X 20 20', 'M', 'E'])
    check('mode off: A types and plays nothing', bit(o.matrices[0], (2, 1)) == 1 and not o.events, str(o.events))

    # a strike: 0 to the bottom in 8 ms; then pressure; then the end
    o = boot(binary, ee, KEYS + WARM + [ON] + strike(A, 8) + ['M', 'E', GETS, 'P %d 800' % A, 'X 15 15', 'E', 'P %d 0' % A, 'X 3 3', 'E', 'M'])
    ev = o.events
    check('a strike on A is one note on: middle C (60), whatever the keymap says the key types', len(ev) >= 1 and ev[0][0] == 'on' and ev[0][2] == 60, str(ev[:3]))
    v8 = ev[0][3] if ev else 0
    check('8 ms from top to bottom is a hard strike (velocity %d)' % v8, 98 <= v8 <= 112, str(v8))
    check('the note key does not type while the mode is on', bit(o.matrices[0], (2, 1)) == 0 and bit(o.matrices[1], (2, 1)) == 0)
    g = o.replies[1]
    check('read-back: mode on, one note, middle C, its velocity, about 3.8 ms between the depths', g[4] == 1 and g[13] * 256 + g[14] == 1 and g[15] == 60 and g[16] == v8 and 34 <= g[17] * 256 + g[18] <= 42, str(g[4:19]))
    pr = [e for e in ev if e[0] == 'pr']
    check('past the second depth, travel is pressure: 80 % is 63', pr and pr[-1][2] == 60 and 61 <= pr[-1][3] <= 65, str(pr[-3:]))
    check('pressure is sent a hundred times a second at most', len(pr) <= 3, str(len(pr)))
    off = [e for e in ev if e[0] == 'off']
    check('letting go ends the note, fast', len(off) == 1 and off[0][2] == 60 and off[0][3] > 90, str(off))

    # velocity follows the speed, and does not depend on where between two scans the key started
    vel = {}
    for ms in (1, 4, 8, 16, 40, 120):
        o = boot(binary, ee, KEYS + WARM + [ON] + strike(A, ms) + ['E'])
        vel[ms] = o.events[0][3] if o.events and o.events[0][0] == 'on' else -1
    check('slower is softer; under 2 ms between the depths is the hardest there is: %s' % vel, all(vel[a] > vel[b] for a, b in zip((4, 8, 16, 40), (8, 16, 40, 120))) and vel[1] == vel[4] == 127 and 1 <= vel[120] <= 20, str(vel))
    shifted = []
    for start in (0, 30, 60, 90):
        o = boot(binary, ee, KEYS + WARM + [ON] + strike(A, 8, 1000 + start, start) + ['E'])
        shifted.append(o.events[0][3] if o.events else -1)
    check('the same strike started a little earlier or later gives the same velocity: %s' % shifted, max(shifted) - min(shifted) <= 3, str(shifted))

    # a press that turns back before the second depth is no note; a key where a piano has no black key types
    o = boot(binary, ee, KEYS + WARM + [ON] + strike(A, 6, 400) + strike(A, 6, 0, 400) + ['E'] + strike(Q, 8) + ['X 20 20', 'M', 'E'])
    check('half a press is no note', not o.events, str(o.events))
    check('Q, where a piano has no black key, types as usual in the mode', bit(o.matrices[0], (1, 1)) == 1)

    # a key held as a typing key when the mode comes on: let go of as a typing key, and no strike
    o = boot(binary, ee, KEYS + WARM + strike(A, 8) + ['X 20 20', 'M', ON, 'M', 'E'])
    check('held when the mode comes on: no longer typed, not struck', bit(o.matrices[0], (2, 1)) == 1 and bit(o.matrices[1], (2, 1)) == 0 and not o.events, str(o.events))

    # the mode off while a note sounds: the note ends; then the key types again
    o = boot(binary, ee, KEYS + WARM + [ON] + strike(A, 8) + ['E', OFF, 'E', 'P %d 0' % A, 'X 30 30'] + strike(A, 8) + ['X 20 20', 'M', 'E'])
    check('leaving the mode ends what sounds', [e[0] for e in o.events] == ['on', 'off'], str(o.events))
    check('and the key types again', bit(o.matrices[0], (2, 1)) == 1)

    # the piano: white keys along the home row, black keys over their gaps, the two ends
    o = boot(binary, ee, KEYS + WARM + [ON] + strike(S_, 8) + strike(W, 8) + strike(QUOTE, 8) + strike(RB, 8) + ['E'])
    on = [e[2] for e in o.events if e[0] == 'on']
    check("S is D (62), W above and between A and S is C sharp (61), ' is the top white key F (77), ] its F sharp (78)", on == [62, 61, 77, 78], str(on))
    whites, blacks = [], []
    for c in range(1, 12):
        o = boot(binary, ee, WARM + [ON] + strike(at[(2, c)], 8) + ['E']); whites.append(o.events[0][2] if o.events else None)
    for c in range(1, 13):
        o = boot(binary, ee, WARM + [ON] + strike(at[(1, c)], 8) + ['X 20 20', 'E']); blacks.append(o.events[0][2] if o.events else None)
    check('the home row A to \' is C D E F G A B C D E F', whites == [60, 62, 64, 65, 67, 69, 71, 72, 74, 76, 77], str(whites))
    check('the row above is . C# D# . F# G# A# . C# D# . F#', blacks == [None, 61, 63, None, 66, 68, 70, None, 73, 75, None, 78], str(blacks))

    # the octave by VIA, and the limits
    o = boot(binary, ee, KEYS + WARM + [ON] + strike(S_, 8) + ['E', S(SET, 0, 27, 1, 0xFF, 0xFF, 0xFF, 0xFF, 3, 0xFF), 'P %d 0' % A, 'X 5 5'] + strike(A, 8) + ['E',
              S(SET, 0, 27, 1, 2, 5, 99, 0x13, 9, 0), GETS])
    on = [e for e in o.events if e[0] == 'on']
    check('an octave up by the configurator, A is the C above (72)', [e[2] for e in on] == [62, 72], str(on))
    check('changing the octave from the configurator ends the notes that sound', len([e for e in o.events if e[0] == 'off']) >= 1, str(o.events))
    g = o.replies[-1]
    check('the depths are kept apart and in range: 5 / 15 / 10, channel 4, octave top, pressure off', g[7:13] == [5, 15, 10, 3, 4, 0], str(g[7:13]))
    o = boot(binary, ee, KEYS + WARM + [ON] + strike(A, 8) + [S(SET, 0, 27, 1, 0xFF, 0xFF, 0xFF, 5, 0xFF, 0xFF), 'E'])
    check('changing the channel ends the note on the channel it was started on', [e[:3] for e in o.events] == [('on', 0, 60), ('off', 0, 60)], str(o.events))

    # the octave keys: X up, Z down, one step a press, the note that sounds keeps its pitch
    o = boot(binary, ee, WARM + [ON] + strike(A, 8) + strike(X_, 8) + ['X 40 40', 'M', GETS] + strike(S_, 8) + up(A) + up(S_) + up(X_) + ['E', GETS])
    ev = o.events
    check('X is an octave up: it does not type, it does not repeat while held, and the read-back says octave +1', bit(o.matrices[0], (3, 2)) == 0 and o.replies[1][11] == 3 and o.replies[-1][11] == 3, str([o.replies[1][11], o.replies[-1][11]]))
    check('the note held across the step keeps its pitch and ends there; the next strike is an octave up', [e[:3] for e in ev if e[0] != 'pr'] == [('on', 0, 60), ('on', 0, 74), ('off', 0, 60), ('off', 0, 74)], str([e for e in ev if e[0] != 'pr']))
    seq = [ON]
    for _ in range(4): seq += strike(X_, 8) + up(X_)
    seq += [GETS] + strike(A, 8) + up(A) + ['E']
    for _ in range(6): seq += strike(Z, 8) + up(Z)
    seq += [GETS] + strike(A, 8) + up(A) + ['E', 'M']
    o = boot(binary, ee, WARM + seq)
    on = [e[2] for e in o.events if e[0] == 'on']
    check('four steps up stop at +2 (A is 84); six steps down stop at -2 (A is 36); Z does not type', o.replies[1][11] == 4 and o.replies[2][11] == 0 and on == [84, 36] and bit(o.matrices[0], (3, 1)) == 0, str([o.replies[1][11], o.replies[2][11], on]))
    o = boot(binary, ee, WARM + strike(X_, 8) + ['X 20 20', ON, 'X 40 40', GETS] + up(X_) + [OFF] + strike(X_, 8) + ['X 20 20', 'M'])
    check('X held when the mode comes on is no step; with the mode off X types again', o.replies[1][11] == 2 and bit(o.matrices[0], (3, 2)) == 1, str(o.replies[1][11]))

    # the lights: only the instrument's keys are lit while the mode is on, a key that is down is bright, and the old lighting comes back
    WHITE = [at[(2, c)] for c in range(1, 12)]; BLACK = [at[(1, c)] for c in (2, 3, 5, 6, 7, 9, 10, 12)]
    want = ''.join('w' if i in WHITE else 'b' if i in BLACK else 'c' if i in (Z, X_) else '.' for i in range(len(at)))
    SPACE = at[(4, 3)]
    gets = lambda o: [r for r in o.replies if r[0] == GET and r[2] == 27]
    o = boot(binary, ee, WARM + ['G', ON, 'G', GETS, 'P %d 400' % A, 'Y 40', 'G', 'P %d 0' % A, 'Y 40', 'G',
                                 S(SET, 0, 28, 2, 0, 4, 3, 1, 64, 0), 'Y 40', 'G', GETS, 'Y 3000', 'G', OFF, 'G', GETS])
    L = o.lights
    check('lights: nothing is painted while the mode is off', L[0][0] == 0 and L[0][1] == 0, str(L[0]))
    check('lights: with the mode on the white keys, the black keys and the two octave keys are lit and every other key is dark', L[1][0] == 1 and L[1][3] == want, L[1][3] + ' wanted ' + want)
    check('lights: the board says so (has them, wanted, painted; 21 keys lit)', gets(o)[0][21] == 7 and gets(o)[0][22] == 21, str(gets(o)[0][21:23]))
    down = want[:A] + 'W' + want[A + 1:]
    check('lights: A held is bright, and back as it was when it comes up', L[2][3] == down and L[3][3] == want, L[2][3])
    ctl = want[:SPACE] + 'c' + want[SPACE + 1:]
    check('lights: a key the configurator makes a control lights up as one (22 lit)', L[4][3] == ctl and gets(o)[1][22] == 22, L[4][3])
    check('lights: three seconds with nothing moving write the strip about once a second, not every scan (%d writes)' % (L[5][1] - L[4][1]), 2 <= L[5][1] - L[4][1] <= 4, str([L[4][1], L[5][1]]))
    check('lights: the mode off puts the old lighting back, once', L[6][0] == 0 and L[6][2] == 1 and gets(o)[2][21] == 3 and gets(o)[2][22] == 0, str([L[6], gets(o)[2][21:23]]))
    seq = [ON]
    for _ in range(40): seq += ['P %d 400' % A, 'Y 5', 'P %d 0' % A, 'Y 5']
    o = boot(binary, ee, WARM + seq + ['G'])
    check('lights: a key hammered eighty times in 400 ms writes the strip at most forty times a second (%d writes)' % o.lights[0][1], o.lights[0][1] <= 18, str(o.lights[0][1]))
    o = boot(binary, ee, WARM + [ON, S(SET, 0, 27, 2, 0), 'G', GETS, 'Y 1200', 'G', S(SET, 0, 27, 2, 1), 'G', GETS, OFF])
    check('lights: turned off from the configurator the old lighting is back while the mode stays on, and nothing repaints; turned on they are painted again',
          o.lights[0][0] == 0 and o.lights[0][2] == 1 and gets(o)[0][21] == 1 and o.lights[1][1] == o.lights[0][1] and o.lights[2][0] == 1 and gets(o)[1][21] == 7, str([o.lights, gets(o)[0][21], gets(o)[1][21]]))
    o = boot(binary, ee, WARM + [S(SET, 0, 4), 'T 50', ON, 'G', 'Y 1500', 'G'])   # calibration paints the keys itself
    check('lights: while the board is calibrating the instrument does not repaint over it', o.lights[1][1] == o.lights[0][1], str(o.lights))

def run_stream(board, title):
    """The stream of the keys that move: a whole frame to stand on, then one report a millisecond holding only what changed."""
    print('==', title, '(moving stream)')
    binary = os.path.join(tmp, board); ee = os.path.join(tmp, board + '.stream.ee')
    fresh(ee)
    o = boot(binary, ee, ['D', 'L'])
    at = {(k['row'], k['col']): s for s, k in o.keys.items()}; cols = o.layout['cols']; total = o.layout['rows'] * cols
    A, S_ = at[(2, 1)], at[(2, 2)]; iA, iS = 2 * cols + 1, 2 * cols + 2
    START, KEEP = S(SET, 0, 21, 3), S(SET, 0, 21, 3)
    def moving(reps): return [(t, r) for t, r in reps if r[0] == 0xF1]
    def whole(reps): return [(t, r) for t, r in reps if r[0] == 0xF0]
    # at rest: a whole frame at once, then only the beat
    o = boot(binary, ee, ['X 600 600', START, 'Y 100', 'H'])
    w, m = whole(o.reports), moving(o.reports)
    check('started: a whole frame first, in its three reports', [r[2] for _, r in o.reports[:3]] == [0, 1, 2] and all(r[0] == 0xF0 for _, r in o.reports[:3]), str([r[:3] for _, r in o.reports[:4]]))
    check('at rest in 100 ms: that one whole frame and a beat every 16 ms, no keys in them (%d reports)' % len(m), len(w) == 3 and 5 <= len(m) <= 7 and all(r[4] == 0 for _, r in m), str(len(m)))
    gaps = [b[0] - a[0] for a, b in zip(m, m[1:])]
    check('the beats are 16 or 17 ms apart', all(16 <= g <= 17 for g in gaps), str(gaps))
    # a press: 0 to the bottom in 20 ms, a millisecond a step
    press = [l for i in range(1, 21) for l in ('P %d %d' % (A, i * 50), 'Y 1')]
    o = boot(binary, ee, ['X 600 600', START, 'Y 40', 'H'] + press + ['Y 30', 'H', 'V %d' % A])
    reps = o.reports
    m = moving(reps); keyed = [(t, r) for t, r in m if r[4] > 0]
    vals = [r[6] for _, r in keyed]
    check('a press arrives as its own key only, one report a millisecond: %d reports for a 20 ms press' % len(keyed), 15 <= len(keyed) <= 20 and all(r[4] == 1 and r[5] == iA for _, r in keyed), str([(t, r[4:7]) for t, r in keyed][:6]))
    check('the travel in them only rises and ends where the board has the key (%s)' % vals[-3:], vals == sorted(vals) and vals[-1] == o.travel[A] and vals[-1] >= 98, str(vals))
    ts = [t for t, _ in keyed]
    check('never two in one millisecond, and none in the millisecond left free (every eighth)', len(set(ts)) == len(ts) and all((t & 7) != 7 for t, _ in m), str(ts))
    check('each carries the board clock and the next sequence number', all(r[2] + r[3] * 256 == (t & 0xFFFF) for t, r in m) and all((b[1][1] - a[1][1]) & 255 == 1 for a, b in zip(m, m[1:])), str([(t, r[1:4]) for t, r in m][:5]))
    # two keys at once share a report; a flicker of one step is not sent, but the next whole frame has it
    both = [l for i in range(1, 11) for l in ('P %d %d' % (A, i * 100), 'P %d %d' % (S_, i * 60), 'Y 1')]
    o = boot(binary, ee, ['X 600 600', START, 'Y 40', 'H'] + both + ['Y 10', 'H'])
    two = [r for _, r in moving(o.reports) if r[4] == 2]
    check('two keys moving together travel in one report, lower matrix index first', len(two) >= 6 and all(r[5] == iA and r[7] == iS for r in two), str([r[4:9] for r in two][:3]))
    o = boot(binary, ee, ['X 600 600', START, 'Y 40', 'H', 'P %d 500' % A, 'Y 40', 'H', 'V %d' % A, 'P %d 510' % A, 'Y 60', 'H', 'V %d' % A, KEEP, 'Y 300', 'H'])
    v0, v1 = o.travels[0][1], o.travels[1][1]
    sent_small = [r for _, r in moving(o.reports) if r[4] and r[6] == v1]
    wh = [r for _, r in whole(o.reports) if r[2] == iA // 29]
    check('a change of one step (%d to %d) is not sent as it happens; the next whole frame carries it' % (v0, v1), v1 - v0 == 1 and not sent_small and wh and wh[-1][3 + iA % 29] == v1, str([v0, v1, len(sent_small)]))
    # whole frames keep coming; the stream stops by itself; the old start values are as they were
    o = boot(binary, ee, ['X 600 600', START, 'Y 700', KEEP, 'Y 400', 'H'])
    w = [t for t, r in whole(o.reports) if r[2] == 0]
    check('a whole frame four times a second (%d in 1.1 s)' % len(w), 4 <= len(w) <= 6 and all(245 <= b - a <= 256 for a, b in zip(w, w[1:])), str(w))
    o = boot(binary, ee, ['X 600 600', START, 'Y 1400', 'H', 'Y 400', 'H'])
    first, last = min(t for t, _ in o.reports), max(t for t, _ in o.reports)
    check('with no keepalive it stops by itself after a second and a half (%d ms)' % (last - first), 1450 <= last - first <= 1520, str(last - first))
    o = boot(binary, ee, ['X 600 600', S(SET, 0, 21, 2), 'Y 100', 'H', S(SET, 0, 21, 0), 'Y 50', 'H'])
    span = max(t for t, _ in o.reports) - min(t for t, _ in o.reports)
    check('start value 2 is still whole frames at 250 a second, and 0 stops it', not moving(o.reports) and 70 <= len(whole(o.reports)) <= 78 and span <= 100, str([len(whole(o.reports)), span]))

def run_bridge(board, title):
    """The bridge to a DAW: the clock it sends, what the page plays going out at its millisecond, keys as controls."""
    print('==', title, '(DAW bridge)')
    binary = os.path.join(tmp, board); ee = os.path.join(tmp, board + '.bridge.ee')
    fresh(ee)
    o = boot(binary, ee, ['D', S(GET, 0, 22)])
    at = {(k['row'], k['col']): s for s, k in o.keys.items()}
    A, SPACE, UPK, DOWNK, Q = at[(2, 1)], at[(4, 3)], at[(1, 13)], at[(2, 12)], at[(1, 1)]
    WARM, ON, OFF = ['X 600 600'], S(SET, 0, 27, 0, 1) + '\nX 5 5', S(SET, 0, 27, 0, 0)
    STREAM, GETM = S(SET, 0, 21, 3), S(GET, 0, 28, 0)
    def strike(s, scans, to=1000, start=0): return ['P %d %d' % (s, start)] + [l for i in range(1, scans + 1) for l in ('P %d %d' % (s, start + (to - start) * i // scans), 'Y 1')]
    def ticks(n, ms): return [l for _ in range(n) for l in ('C f8', 'Y %d' % ms)]
    def clock(reps): return [(t, r) for t, r in reps if r[0] == 0xF2]
    def bit(m, pos): return (m[pos[0]] >> pos[1]) & 1
    check('info says the build has the bridge, in a second byte of features', o.replies[0][11] == 1, str(o.replies[0][:13]))

    # ---- the clock: start, 48 ticks 20 ms apart (125 beats a minute), then what the board says
    o = boot(binary, ee, WARM + [ON, STREAM, 'Y 30', 'H', 'Z', 'C fa'] + ticks(48, 20) + [GETM, 'H', 'Z'])
    g, cl = o.replies[-1], clock(o.reports); t0 = o.nows[0]
    check('running, a clock seen, 125.0 beats a minute, the last tick is number 47', g[4] == 3 and g[5] * 256 + g[6] == 1250 and (g[7] << 24 | g[8] << 16 | g[9] << 8 | g[10]) == 47, str(g[4:11]))
    six = [(t, r) for t, r in cl if r[1] & 1 and (r[4] | r[5] << 8) % 6 == 0 and (r[4] | r[5] << 8) > 0]
    check('the page is told on every sixth tick (a sixteenth note): 6, 12 ... 42', [r[4] | r[5] << 8 for _, r in six] == [6, 12, 18, 24, 30, 36, 42], str([r[4] | r[5] << 8 for _, r in six]))
    check('each report carries the board clock at the tick it describes, to the millisecond', all((r[2] | r[3] << 8) == ((t0 + 20 * (r[4] | r[5] << 8)) & 0xFFFF) for _, r in six), str([(r[2] | r[3] << 8, (t0 + 20 * (r[4] | r[5] << 8)) & 0xFFFF) for _, r in six][:3]))
    check('and the tempo, once a quarter note has passed', six[-1][1][8] | six[-1][1][9] << 8 == 1250, str(six[-1][1][8:10]))
    # stop, continue, a new position; and no clock at all
    o = boot(binary, ee, WARM + [ON, STREAM, 'Y 30', 'C fa'] + ticks(12, 20) + ['C fc', GETM] + ticks(6, 20) + [GETM, 'C fb'] + ticks(3, 20) + [GETM, 'C f2 08 00', 'C f8', 'Y 20', 'C f8', 'Y 20', GETM, 'Y 1300', GETM, 'H'])
    num = lambda g: g[7] << 24 | g[8] << 16 | g[9] << 8 | g[10]
    r1, r2, r3, r4, r5 = o.replies[-5:]
    check('stop: not running, the tick number stands still though the clock goes on (11, 11)', r1[4] == 2 and num(r1) == 11 and r2[4] == 2 and num(r2) == 11, str([r1[4], num(r1), r2[4], num(r2)]))
    check('continue: running again from where it stood (14)', r3[4] == 3 and num(r3) == 14, str([r3[4], num(r3)]))
    check('a song position of 8 sixteenths: the next tick is number 48, then 49', num(r4) == 49, str(num(r4)))
    check('a second with no clock: the board says none is seen', r5[4] & 2 == 0, str(r5[4]))
    idle = [t for t, r in clock(o.reports)][-5:]
    check('with nothing to say the report still comes four times a second', all(245 <= b - a <= 256 for a, b in zip(idle, idle[1:])), str(idle))
    o = boot(binary, ee, WARM + [STREAM, 'Y 30', 'C fa'] + ticks(12, 20) + ['H'])
    check('with the instrument mode off the bridge says nothing', not clock(o.reports), str(len(clock(o.reports))))

    # ---- what the page plays: out at its millisecond
    o = boot(binary, ee, WARM + [ON, GETM])
    def ev(due, status, d1, d2): return [due & 0xFF, (due >> 8) & 0xFF, status, d1, d2]
    def run_out(extra):
        o1 = boot(binary, ee, WARM + [ON, 'Z'] + extra)
        return o1, o1.nows[0]
    o, t0 = run_out([])
    now16 = t0 & 0xFFFF
    o = boot(binary, ee, WARM + [ON, 'Z', S(SET, 0, 28, 1, 3, *ev(now16 + 50, 0x90, 60, 100), *ev(now16 + 120, 0x80, 60, 0), *ev(now16, 0x99, 36, 127)), 'Y 200', 'O', GETM])
    outs = [(t - o.nows[0], m) for t, m in o.outs]
    check('an event due now goes at once; the others in their own millisecond (0, 50, 120)', [(t, m[0], m[1]) for t, m in outs] == [(0, 0x99, 36), (50, 0x90, 60), (120, 0x80, 60)] or [(t, m[0], m[1]) for t, m in outs] == [(0, 0x99, 36), (51, 0x90, 60), (121, 0x80, 60)], str(outs))
    g = o.replies[-1]
    check('read-back: nothing waiting, three sent, none dropped, twelve control slots', g[13] == 0 and g[14] * 256 + g[15] == 3 and g[16] == 0 and g[17] == 12, str(g[13:18]))
    o = boot(binary, ee, WARM + [ON, 'Z', S(SET, 0, 28, 1, 2, *ev(now16 + 400, 0x90, 60, 100), *ev(now16 + 500, 0x91, 62, 100)), 'Y 420', 'O', OFF, 'Y 300', 'O'])
    a, b = [m for t, m in o.outs[:1]], [m for t, m in o.outs[1:]]
    check('leaving the mode: what waited is dropped, and the channel a note went out on is told all notes off', a == [[0x90, 60, 100]] and b == [[0xB0, 123, 0]], str(o.outs))
    many = [S(SET, 0, 28, 1, 5, *[x for i in range(5) for x in ev(now16 + 5000 + j * 5 + i, 0x90, 40 + i, 1)]) for j in range(12)]
    o = boot(binary, ee, WARM + [ON] + many + [GETM])
    g = o.replies[-1]
    check('sixty events far ahead: forty-eight wait, twelve are counted as dropped', g[13] == 48 and g[16] == 12, str(g[13:17]))
    o = boot(binary, ee, WARM + [S(SET, 0, 28, 1, 1, *ev(now16, 0x90, 60, 100)), 'Y 20', 'O'])
    check('with the mode off nothing goes out', not o.outs, str(o.outs))

    # ---- keys as controls
    CTL = lambda slot, pos, kind, num=0, ch=0: S(SET, 0, 28, 2, slot, pos[0], pos[1], kind, num, ch)
    SETUP = [CTL(0, (4, 3), 1, 64), CTL(1, (1, 13), 2), CTL(2, (2, 12), 3), CTL(3, (2, 1), 1, 7)]
    o = boot(binary, ee, WARM + [ON] + SETUP + ['P %d 500' % SPACE, 'Y 30', 'O', 'M', 'P %d 1000' % SPACE, 'Y 30', 'P %d 0' % SPACE, 'Y 30', 'O', S(GET, 0, 28, 1, 0), S(GET, 0, 28, 1, 5)])
    first, rest = o.outs[:1], [m for t, m in o.outs[1:]]
    check('SPACE as controller 64: half way down is 57, and it does not type', [m for t, m in first] == [[0xB0, 64, 57]] and bit(o.matrices[0], (4, 3)) == 0, str(o.outs[:2]))
    check('the bottom is 127 and rest is 0', rest == [[0xB0, 64, 127], [0xB0, 64, 0]], str(rest))
    check('read-back: slot 0 is that key, a controller, number 64; slot 5 is empty', o.replies[-2][5:10] == [4, 3, 1, 64, 0] and o.replies[-1][5:8] == [0xFF, 0xFF, 0], str([o.replies[-2][5:10], o.replies[-1][5:8]]))
    o = boot(binary, ee, WARM + [ON] + SETUP + ['P %d 1000' % UPK, 'Y 30', 'O', 'P %d 1000' % DOWNK, 'Y 30', 'O', 'P %d 0' % UPK, 'Y 30', 'O'])
    bends = [m for t, m in o.outs]
    check('the two bend keys make one bend: up is the top (7f 7f), both is the middle (00 40), down alone is the bottom (01 00)', bends == [[0xE0, 0x7F, 0x7F], [0xE0, 0x00, 0x40], [0xE0, 0x01, 0x00]], str(bends))
    ramp = [l for i in range(1, 101) for l in ('P %d %d' % (SPACE, i * 10), 'Y 1')]
    o = boot(binary, ee, WARM + [ON] + SETUP + ramp + ['Y 20', 'O'])
    vals = [m[2] for t, m in o.outs]
    check('a press over a tenth of a second: at most a hundred messages a second, rising, ending at 127 (%d messages)' % len(vals), 6 <= len(vals) <= 12 and vals == sorted(vals) and vals[-1] == 127, str(vals))
    o = boot(binary, ee, WARM + [ON] + SETUP + strike(A, 8) + ['Y 20', 'E', 'O'])
    check('a note key cannot be a control: A still plays middle C and sends no controller', [e[:3] for e in o.events if e[0] == 'on'] == [('on', 0, 60)] and not o.outs, str([o.events[:2], o.outs]))
    o = boot(binary, ee, WARM + [ON] + SETUP + ['P %d 1000' % SPACE, 'P %d 1000' % UPK, 'Y 30', 'O', OFF, 'O', 'P %d 0' % SPACE, 'Y 60', 'P %d 1000' % SPACE, 'Y 60', 'M', 'O'])
    left = [m for t, m in o.outs[2:]]
    check('leaving the mode with controls held: each is sent back to rest, and the key types again', sorted(left) == sorted([[0xB0, 64, 0], [0xE0, 0x00, 0x40]]) and bit(o.matrices[0], (4, 3)) == 1, str(left))
    o = boot(binary, ee, WARM + [ON] + SETUP + [S(SET, 0, 28, 3), 'P %d 1000' % SPACE, 'Y 60', 'M', 'O', S(GET, 0, 28, 1, 0)])
    check('every slot emptied: SPACE types again and sends nothing', bit(o.matrices[0], (4, 3)) == 1 and not o.outs and o.replies[-1][5] == 0xFF, str(o.outs))

    # ---- the board's own notes kept in while the page plays them
    o = boot(binary, ee, WARM + [ON, S(SET, 0, 28, 4, 1)] + strike(A, 8) + ['Y 20', 'E', S(GET, 0, 27, 0), GETM])
    check('kept in: the strike is counted and timed but not sent', not o.events and o.replies[-2][13] * 256 + o.replies[-2][14] == 1 and o.replies[-1][18] == 1, str([o.events, o.replies[-2][13:15]]))
    o = boot(binary, ee, WARM + [ON] + strike(A, 8) + ['Y 20', 'E', S(SET, 0, 28, 4, 1), 'E', 'P %d 0' % A, 'Y 20', 'E'])
    check('kept in while a note sounds: the note is ended first, so nothing is left on in the DAW', [e[0] for e in o.events if e[0] != 'pr'] == ['on', 'off'], str(o.events))

run_board('icebreaker_hall_effect', 'Icebreaker HE')
run_synth('icebreaker_hall_effect', 'Icebreaker HE')
run_stream('icebreaker_hall_effect', 'Icebreaker HE')
run_bridge('icebreaker_hall_effect', 'Icebreaker HE')
run_board('cleaver', 'Cleaver HE')
run_synth('cleaver', 'Cleaver HE')
run_stream('cleaver', 'Cleaver HE')
run_bridge('cleaver', 'Cleaver HE')
print('%d passed, %d failed' % (passed, failed))
sys.exit(1 if failed else 0)
