// Runs the Icebreaker Hotswap's own custom-channel handler on a host: the block of its VIA keymap between the
// two marks is compiled as it stands (run.py cuts it out), with stand-ins for what the QMK core gives it.
// stdin: "S <hex bytes>" a VIA packet (32 bytes, zero filled) -> prints "R <32 hex bytes>" as the core would
// send it back after calling via_custom_value_command_kb.
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
enum { id_custom_set_value = 0x07, id_custom_get_value = 0x08, id_custom_save = 0x09, id_unhandled = 0xFF };
enum { id_custom_channel = 0 };
#define MATRIX_ROWS 5
#define MATRIX_COLS 15
#include "hs_block.inc"
#ifdef HS_HAS_HARDWARE_ID
hardware_id_t get_hardware_id(void) { hardware_id_t id = {{0x44332211, 0x88776655, 0}}; return id; }   // an RP2040 gives eight bytes: the flash chip's ID
#endif
int main(void) {
    char line[512];
    while (fgets(line, sizeof line, stdin)) {
        if (line[0] != 'S') continue;
        uint8_t d[32] = {0}; int n = 0; char *p = line + 1;
        while (n < 32) { char *e; long v = strtol(p, &e, 16); if (e == p) break; d[n++] = (uint8_t)v; p = e; }
        via_custom_value_command_kb(d, 32);
        printf("R"); for (int i = 0; i < 32; i++) printf(" %02x", d[i]); printf("\n");
    }
    return 0;
}
