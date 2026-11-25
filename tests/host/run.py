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
        self.mode = self.pair = self.layout = self.cal = None; self.pairs = {}; self.events = []
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
        check('info: features per-key, pairs, stored, id, calibration, synth, fast stream; 8 pairs', info[7] == 0x7F and info[8] == 8, '%s %s' % (hex(info[7]), info[8]))
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
        # the base-layer keycodes, the same on both sides: the instrument's notes follow them
        for (r_, c_) in spots: script.append(('KC', [r_, c_, 4 + ((r_ * L['cols'] + c_) % 26)]))
        for _ in range(1500):
            x = rng.random()
            pos = rng.choice(spots) if rng.random() < 0.9 else empty
            if rng.random() < 0.07:   # the instrument: the mode, its settings, its read-back
                y = rng.random()
                script.append(('S', [SET, 0, 27, 0, rng.choice([0, 1, 1])] if y < 0.4 else [GET, 0, 27, 0] if y < 0.8 else [SET, 0, 27, 1, byte(), byte(), byte(), byte(), rng.choice([0, 1, 2, 3, 4, 9, 255]), byte()]))
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
            elif x < 0.89: script.append(('RAW', [rng.choice(real[:10] if rng.random() < 0.6 else real), rng.randint(lo, hi)]))
            elif x < 0.92: script.append(('N', rng.randint(1, 6)))
            elif x < 0.95: script.append(('V', rng.choice(real[:10] if rng.random() < 0.6 else real)))
            else: script.append(('T', rng.choice([100, 400, 900, 1500, 1990, 2010, 2500, 4000])))
        script.append(('E', 0))
        fresh(ee)
        line = {'KC': lambda a: 'KC %d %d %x' % tuple(a), 'E': lambda a: 'E', 'S': lambda a: 'S ' + ' '.join('%02x' % b for b in a), 'T': lambda a: 'T %d' % a, 'RAW': lambda a: 'RAW %d %d' % tuple(a), 'N': lambda a: 'N %d' % a, 'V': lambda a: 'V %d' % a}
        fwo = boot(binary, ee, [line[op](a) for op, a in script]); fw = fwo.replies
        opts = dict(name=title, vendorId=0x5363, productId=0x0010, rows=L['rows'], cols=L['cols'], sensors=[[o.keys[i]['row'], o.keys[i]['col']] for i in range(L['sensors'])],
                    defaultPair=[sA, sD], rest=L['floor'], bottom=L['ceiling'], calStart=L['calstart'], alias=({'68': 18} if enc else {}), noTravel=([68] if enc else []), synth=True, hasMidi=False, clockAtBoot=10000, perKey=True, uid=[0x44, 0x33, 0x22, 0x11, 0x88, 0x77, 0x66, 0x55, 0xCC, 0xBB, 0xAA, 0x99], version=[1, 0, 5, 0])
        job = os.path.join(tmp, board + '.job.json'); json.dump(dict(options=opts, script=script), open(job, 'w'))
        r = subprocess.run(['node', emu, job], capture_output=True, text=True, timeout=120)
        em = [[int(x, 16) for x in ln.split()[1:]] for ln in r.stdout.splitlines() if ln.startswith('R')]
        emv = [tuple(int(x) for x in ln.split()[1:]) for ln in r.stdout.splitlines() if ln.startswith('V')]
        sends = [a for op, a in script if op == 'S']
        eme = [(p_[1], int(p_[2]), int(p_[3]), int(p_[4])) for p_ in (ln.split() for ln in r.stdout.splitlines() if ln.startswith('E ') and ln != 'E .')]
        check('emulated board plays the notes the firmware plays (%d events)' % len(fwo.events), eme == fwo.events, 'first difference: %s' % str(next(((i, a, b) for i, (a, b) in enumerate(zip(fwo.events, eme)) if a != b), (len(fwo.events), len(eme)))))
        check('emulated board answered every packet', len(em) == len(fw) == len(sends), '%d %d %d %s' % (len(em), len(fw), len(sends), r.stderr[-300:]))
        diffs = [(i, sends[i], fw[i][:12], em[i][:12]) for i in range(min(len(em), len(fw))) if fw[i] != em[i]]
        check('emulated board shows the travel the firmware streams (%d readings)' % len(emv), emv == fwo.travels, str([(a, b) for a, b in zip(emv, fwo.travels) if a != b][:4]))
        check('emulated board = firmware on %d packets' % len(sends), not diffs, '\n    first differences (packet, firmware, emulated): ' + '\n    '.join(str(d) for d in diffs[:4]))

def run_synth(board, title):
    """The instrument mode: the scan rate, a strike timed between two depths, pressure, the note's end, and typing kept apart."""
    print('==', title, '(instrument)')
    binary = os.path.join(tmp, board); ee = os.path.join(tmp, board + '.synth.ee')
    fresh(ee)
    o = boot(binary, ee, ['D'])
    at = {(k['row'], k['col']): s for s, k in o.keys.items()}
    Z, X_, Q, A = at[(3, 1)], at[(3, 2)], at[(1, 1)], at[(2, 1)]
    KEYS = ['KC 3 1 1d', 'KC 3 2 1b', 'KC 1 1 14', 'KC 2 1 04']          # Z, X, Q are notes; A is not
    WARM = ['X 600 600']                                                 # a thousand scans a second
    ON, OFF = S(SET, 0, 27, 0, 1) + '\nX 5 5', S(SET, 0, 27, 0, 0)   # the mode on, then a few scans with the keys at rest
    GETS = S(GET, 0, 27, 0)
    def strike(s, scans, to=1000, start=0):                              # travel from start to `to` in that many scans, a millisecond each
        return ['P %d %d' % (s, start)] + [l for i in range(1, scans + 1) for l in ('P %d %d' % (s, start + (to - start) * i // scans), 'X 1 1')]
    def bit(m, pos): return (m[pos[0]] >> pos[1]) & 1

    o = boot(binary, ee, KEYS + WARM + [GETS, S(GET, 0, 22)])
    r, info = o.replies[0], o.replies[1]
    check('off at power-up; 1000 scans a second measured', r[4] == 0 and 990 <= r[5] * 256 + r[6] <= 1010, str(r[:10]))
    check('depths 12 / 60 / 30, channel 1, middle octave, pressure on', r[7:13] == [12, 60, 30, 0, 2, 1], str(r[7:13]))
    check('a host build says it has no MIDI interface', r[19] == 0)
    check('info carries the scan rate too', 990 <= info[9] * 256 + info[10] <= 1010, str(info[:12]))

    # the mode off: note keys type, nothing is played
    o = boot(binary, ee, KEYS + WARM + strike(Z, 8) + ['X 20 20', 'M', 'E'])
    check('mode off: Z types and plays nothing', bit(o.matrices[0], (3, 1)) == 1 and not o.events, str(o.events))

    # a strike: 0 to the bottom in 8 ms; then pressure; then the end
    o = boot(binary, ee, KEYS + WARM + [ON] + strike(Z, 8) + ['M', 'E', GETS, 'P %d 800' % Z, 'X 15 15', 'E', 'P %d 0' % Z, 'X 3 3', 'E', 'M'])
    ev = o.events
    check('a strike is one note on: C3', len(ev) >= 1 and ev[0][0] == 'on' and ev[0][2] == 48, str(ev[:3]))
    v8 = ev[0][3] if ev else 0
    check('8 ms from top to bottom is a hard strike (velocity %d)' % v8, 98 <= v8 <= 112, str(v8))
    check('the note key does not type while the mode is on', bit(o.matrices[0], (3, 1)) == 0 and bit(o.matrices[1], (3, 1)) == 0)
    g = o.replies[1]
    check('read-back: mode on, one note, C3, its velocity, about 3.8 ms between the depths', g[4] == 1 and g[13] * 256 + g[14] == 1 and g[15] == 48 and g[16] == v8 and 34 <= g[17] * 256 + g[18] <= 42, str(g[4:19]))
    pr = [e for e in ev if e[0] == 'pr']
    check('past the second depth, travel is pressure: 80 % is 63', pr and pr[-1][2] == 48 and 61 <= pr[-1][3] <= 65, str(pr[-3:]))
    check('pressure is sent a hundred times a second at most', len(pr) <= 3, str(len(pr)))
    off = [e for e in ev if e[0] == 'off']
    check('letting go ends the note, fast', len(off) == 1 and off[0][2] == 48 and off[0][3] > 90, str(off))

    # velocity follows the speed, and does not depend on where between two scans the key started
    vel = {}
    for ms in (1, 4, 8, 16, 40, 120):
        o = boot(binary, ee, KEYS + WARM + [ON] + strike(Z, ms) + ['E'])
        vel[ms] = o.events[0][3] if o.events and o.events[0][0] == 'on' else -1
    check('slower is softer; under 2 ms between the depths is the hardest there is: %s' % vel, all(vel[a] > vel[b] for a, b in zip((4, 8, 16, 40), (8, 16, 40, 120))) and vel[1] == vel[4] == 127 and 1 <= vel[120] <= 20, str(vel))
    shifted = []
    for start in (0, 30, 60, 90):
        o = boot(binary, ee, KEYS + WARM + [ON] + strike(Z, 8, 1000 + start, start) + ['E'])
        shifted.append(o.events[0][3] if o.events else -1)
    check('the same strike started a little earlier or later gives the same velocity: %s' % shifted, max(shifted) - min(shifted) <= 3, str(shifted))

    # a press that turns back before the second depth is no note; a key that is not a note key types
    o = boot(binary, ee, KEYS + WARM + [ON] + strike(Z, 6, 400) + strike(Z, 6, 0, 400) + ['E'] + strike(A, 8) + ['X 20 20', 'M', 'E'])
    check('half a press is no note', not o.events, str(o.events))
    check('a key that is not a note key types as usual in the mode', bit(o.matrices[0], (2, 1)) == 1)

    # a key held as a typing key when the mode comes on: let go of as a typing key, and no strike
    o = boot(binary, ee, KEYS + WARM + strike(Z, 8) + ['X 20 20', 'M', ON, 'M', 'E'])
    check('held when the mode comes on: no longer typed, not struck', bit(o.matrices[0], (3, 1)) == 1 and bit(o.matrices[1], (3, 1)) == 0 and not o.events, str(o.events))

    # the mode off while a note sounds: the note ends; then the key types again
    o = boot(binary, ee, KEYS + WARM + [ON] + strike(Z, 8) + ['E', OFF, 'E', 'P %d 0' % Z, 'X 30 30'] + strike(Z, 8) + ['X 20 20', 'M', 'E'])
    check('leaving the mode ends what sounds', [e[0] for e in o.events] == ['on', 'off'], str(o.events))
    check('and the key types again', bit(o.matrices[0], (3, 1)) == 1)

    # the other notes, the octave, the limits
    o = boot(binary, ee, KEYS + WARM + [ON] + strike(Q, 8) + strike(X_, 8) + ['E', S(SET, 0, 27, 1, 0xFF, 0xFF, 0xFF, 0xFF, 3, 0xFF), 'P %d 0' % Z, 'X 5 5'] + strike(Z, 8) + ['E',
              S(SET, 0, 27, 1, 2, 5, 99, 0x13, 9, 0), GETS])
    on = [e for e in o.events if e[0] == 'on']
    check('Q is C4, X is D3; an octave up Z is C4', [e[2] for e in on] == [60, 50, 60], str(on))
    check('changing the octave ends the notes that sound', len([e for e in o.events if e[0] == 'off']) >= 2, str(o.events))
    g = o.replies[-1]
    check('the depths are kept apart and in range: 5 / 15 / 10, channel 4, octave top, pressure off', g[7:13] == [5, 15, 10, 3, 4, 0], str(g[7:13]))

run_board('icebreaker_hall_effect', 'Icebreaker HE')
run_synth('icebreaker_hall_effect', 'Icebreaker HE')
run_board('cleaver', 'Cleaver HE')
run_synth('cleaver', 'Cleaver HE')
print('%d passed, %d failed' % (passed, failed))
sys.exit(1 if failed else 0)
