#!/usr/bin/env python3
"""Build the host harness for one HE board from the firmware hub's source.
usage: build.py <firmware repo> <board dir under boards/wired> <out binary> [--uid]"""
import sys, os, subprocess, re
repo, board, out = sys.argv[1:4]
uid = '--uid' in sys.argv
here = os.path.dirname(os.path.abspath(__file__))
B = os.path.join(repo, 'boards/wired', board, 'hall_effect')
km = os.path.join(B, 'keymaps/via/keymap.c')
src = open(km).read()
# the keymap's save block, lifted out of housekeeping_task_user() as it stands
start = src.index('if (eeprom_save_pending && timer_elapsed(eeprom_save_timer) > EEPROM_SAVE_DELAY) {')
i = src.index('{', start); depth = 0
while True:
    if src[i] == '{': depth += 1
    elif src[i] == '}':
        depth -= 1
        if depth == 0: break
    i += 1
block = src[start:i + 1]
decl = re.search(r'static uint16_t last_eeprom_write_timer = 0;', src).group(0)
gen = os.path.join(os.path.dirname(out), os.path.basename(out) + '_housekeeping.c')
open(gen, 'w').write('// lifted from %s by build.py\n#include "host.h"\n#include "he_switch_matrix.h"\n%s\nvoid host_housekeeping_save(void) {\n    %s\n}\n' % (km, decl, block))
flags = ['-fcommon', '-O1', '-Wall', '-Wno-unused-variable', '-Wno-unused-function', '-Wno-unused-but-set-variable', '-DVIA_ENABLE',
         '-include', os.path.join(B, 'config.h'), '-include', os.path.join(B, 'keymaps/via/config.h'), '-I' + os.path.join(here, 'stubs'), '-I' + B]
if uid:
    flags += ['-I' + os.path.join(here, 'stubs_uid'), '-DHOST_HAS_HARDWARE_ID']
files = [os.path.join(here, 'harness.c'), gen] + [os.path.join(B, f) for f in ('he_switch_matrix.c', 'he_perkey.c', 'he_stream.c', 'keymaps/via/via_apc.c')]
r = subprocess.run(['gcc'] + flags + files + ['-lm', '-o', out], capture_output=True, text=True)
sys.stderr.write(r.stderr)
sys.exit(r.returncode)
