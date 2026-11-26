// Runs the HE board's real source on a host: he_switch_matrix.c, he_perkey.c, he_stream.c and the
// VIA keymap's via_apc.c are compiled unchanged; this file stands in for the MCU (EEPROM, timer,
// pins, ADC) and for the QMK core's two datablock calls. One process is one power cycle: the
// EEPROM is read from and written back to the file given as argv[1].
//
// Commands on stdin, one per line:
//   S <hex bytes>   a VIA packet to via_custom_value_command_kb(); prints "R <32 hex bytes>"
//   T <ms>          let time pass (the scan loop's he_perkey_task and the keymap's save run)
//   A <sensor> <pct>  hold that key at pct of its travel (-1 = every key)
//   RAW <sensor> <adc>  set the raw ADC value of a key
//   N <scans>       run he_matrix_scan() that many times
//   M               print the matrix rows ("M r0 r1 ...", hex)
//   D               dump what the scan uses: per key "act rel dead down up floor ceiling", then mode, pairs, calibrating
//   V <sensor>      print the travel the last scan saw for that key (0-100)
//   L               print the EEPROM layout numbers
//   KC <row> <col> <hex>  the base-layer keycode at that position (the instrument's notes follow it)
//   E               print and forget the instrument's events ("E on|off|pr channel note value" each, then "E .")
//   P <sensor> <permille>  hold that key at that many tenths of a percent of its travel
//   X <scans> <ms>         that many scans, with that much time passing evenly over them
#include "host.h"
#undef printf
#include "he_switch_matrix.h"
#include "he_perkey.h"
#include "he_stream.h"
#include "he_midi.h"
#include "he_synth.h"

uint8_t  EE[4096];
uint32_t host_now = 10000;
uint8_t  host_pin[HOST_PINS];
matrix_row_t matrix[MATRIX_ROWS];
uint8_t last_moved_slider, current_slider_type, latest_slider_value;
bool    slider_active;

extern const sensor_to_matrix_map_t sensor_to_matrix_map[];
void via_custom_value_command_kb(uint8_t *data, uint8_t length);
void host_housekeeping_save(void);
uint16_t he_readkey_raw(uint8_t sensorIndex);

// ---- EEPROM
uint8_t  eeprom_read_byte(const uint8_t *addr) { return EE[(size_t)addr]; }
uint32_t eeprom_read_dword(const uint32_t *addr) { uint32_t v; memcpy(&v, &EE[(size_t)addr], 4); return v; }
void     eeprom_read_block(void *buf, const void *addr, size_t len) { memcpy(buf, &EE[(size_t)addr], len); }
void     eeprom_update_dword(uint32_t *addr, uint32_t v) { memcpy(&EE[(size_t)addr], &v, 4); }
void     eeprom_update_block(const void *buf, void *addr, size_t len) { memcpy(&EE[(size_t)addr], buf, len); }
// quantum/eeconfig.c, the four datablock calls, as in QMK 0.25
void eeconfig_read_kb_datablock(void *data) {
    if (eeprom_read_dword(EECONFIG_KEYBOARD) == (EECONFIG_KB_DATA_VERSION)) eeprom_read_block(data, EECONFIG_KB_DATABLOCK, (EECONFIG_KB_DATA_SIZE));
    else memset(data, 0, (EECONFIG_KB_DATA_SIZE));
}
void eeconfig_update_kb_datablock(const void *data) {
    eeprom_update_dword(EECONFIG_KEYBOARD, (EECONFIG_KB_DATA_VERSION));
    eeprom_update_block(data, EECONFIG_KB_DATABLOCK, (EECONFIG_KB_DATA_SIZE));
}
void eeconfig_read_user_datablock(void *data) {
    if (eeprom_read_dword(EECONFIG_USER) == (EECONFIG_USER_DATA_VERSION)) eeprom_read_block(data, EECONFIG_USER_DATABLOCK, (EECONFIG_USER_DATA_SIZE));
    else memset(data, 0, (EECONFIG_USER_DATA_SIZE));
}
void eeconfig_update_user_datablock(const void *data) {
    eeprom_update_dword(EECONFIG_USER, (EECONFIG_USER_DATA_VERSION));
    eeprom_update_block(data, EECONFIG_USER_DATABLOCK, (EECONFIG_USER_DATA_SIZE));
}

// ---- timer
uint16_t timer_read(void) { return (uint16_t)host_now; }
uint32_t timer_read32(void) { return host_now; }
uint16_t timer_elapsed(uint16_t last) { return (uint16_t)((uint16_t)host_now - last); }
uint32_t timer_elapsed32(uint32_t last) { return host_now - last; }

// ---- ADC: the firmware selects a sensor by its mux pins; the pin pattern says which key is read
static uint16_t host_adc[SENSOR_COUNT];
static uint32_t sig_of[SENSOR_COUNT];
static int      learning = -1;
static uint32_t sig_mask = 0xFFFFFFFF;   // after learning: only the pins the mux selection moves
static uint32_t pins_sig(void) { uint32_t s = 0; for (int i = 0; i < HOST_PINS; i++) s |= (uint32_t)(host_pin[i] & 1) << i; return s & sig_mask; }
adc_mux pinToMux(pin_t pin) { return pin; }
int16_t adc_read(adc_mux mux) {
    (void)mux;
    uint32_t s = pins_sig();
    if (learning >= 0) { sig_of[learning] = s; return 0; }
    for (int i = 0; i < SENSOR_COUNT; i++) if (sig_of[i] == s) return (int16_t)host_adc[i];
    return 0;
}

// ---- outputs with no meaning here
static uint8_t  last_reports[4096][32]; static uint32_t report_at[4096]; static int n_reports = 0;
void raw_hid_send(uint8_t *data, uint8_t length) { if (n_reports < 4096) { report_at[n_reports] = host_now; memcpy(last_reports[n_reports++], data, length < 32 ? length : 32); } }
// what the bridge sends to the DAW, each with the millisecond it left in
static uint8_t  outs[2048][3]; static uint32_t out_at[2048]; static int n_out = 0;
void he_midi_out(uint8_t status, uint8_t d1, uint8_t d2) { if (n_out < 2048) { out_at[n_out] = host_now; outs[n_out][0] = status; outs[n_out][1] = d1; outs[n_out][2] = d2; n_out++; } }
void bootloader_jump(void) {}
// the instrument's lights: what the mode last painted, a letter a sensor (. off, w white key, b black key, c control; a capital while the key is down)
static char lights_now[SENSOR_COUNT + 1]; static int lights_painted = 0, lights_writes = 0, lights_ended = 0;
bool he_lights_paint(const uint8_t *kinds) { for (int i = 0; i < SENSOR_COUNT; i++) { char c = ".wbc"[kinds[i] & 3]; lights_now[i] = (kinds[i] & HE_LIGHT_DOWN) && c != '.' ? (char)(c - 32) : c; } lights_painted = 1; lights_writes++; return true; }
void he_lights_end(void) { lights_painted = 0; lights_ended++; }
void start_slider_visualization(uint8_t value) { (void)value; }
void start_calibration_rgb(void) {}
void end_calibration_visual(void) {}
void calibration_warning(void) {}
void update_calibration_rgb(uint8_t sensor, uint16_t value) { (void)sensor; (void)value; }
void apply_calibration_changes_rgb(void) {}
void suspend_power_down_user(void) {}
void suspend_wakeup_init_user(void) {}
#ifdef HOST_HAS_HARDWARE_ID
#include "hardware_id.h"
hardware_id_t get_hardware_id(void) { hardware_id_t id = {{0x11223344, 0x55667788, 0x99AABBCC}}; return id; }
#endif

static uint16_t raw_for_pct(int s, int pct) {
    int fl = he_key_configs[s].noise_floor, ce = he_key_configs[s].noise_ceiling;
    // a little past the exact point so the integer division in rescale() lands on pct
    double v = fl + (ce - fl) * (pct / 100.0);
    return (uint16_t)(ce > fl ? v + 0.999 : v - 0.999);
}

// The instrument: the keymap's keycodes and where its events go.
static uint16_t host_kc[MATRIX_ROWS][MATRIX_COLS];
uint16_t dynamic_keymap_get_keycode(uint8_t layer, uint8_t row, uint8_t col) { return layer == 0 && row < MATRIX_ROWS && col < MATRIX_COLS ? host_kc[row][col] : 0; }
static uint8_t ev[512][4]; static int n_ev = 0;
void he_synth_out(uint8_t kind, uint8_t channel, uint8_t note, uint8_t value) { if (n_ev < 512) { ev[n_ev][0] = kind; ev[n_ev][1] = channel; ev[n_ev][2] = note; ev[n_ev][3] = value; n_ev++; } }

int main(int argc, char **argv) {
    const char *eefile = argc > 1 ? argv[1] : NULL;
    memset(EE, 0xFF, sizeof(EE));
    if (eefile) { FILE *f = fopen(eefile, "rb"); if (f) { if (fread(EE, 1, sizeof(EE), f) != sizeof(EE)) { /* short file: rest stays erased */ } fclose(f); } }

    for (int s = 0; s < SENSOR_COUNT; s++) { learning = s; he_readkey_raw(s); }
    learning = -1;
    { uint32_t moved = 0; for (int i = 1; i < SENSOR_COUNT; i++) moved |= sig_of[i] ^ sig_of[0]; sig_mask = moved; for (int i = 0; i < SENSOR_COUNT; i++) sig_of[i] &= moved; }
    for (int a = 0; a < SENSOR_COUNT; a++) for (int b = a + 1; b < SENSOR_COUNT; b++)
        if (sig_of[a] == sig_of[b] && !(a == 68 || b == 68)) fprintf(stdout, "WARN sensors %d and %d select the same mux channel\n", a, b);
    for (int s = 0; s < SENSOR_COUNT; s++) host_adc[s] = EXPECTED_NOISE_FLOOR;

    // matrix_init(), the parts that matter here
    memset(he_live_travel, 0xFF, sizeof(he_live_travel));
    he_init(he_key_configs, SENSOR_COUNT);
    noise_floor_calibration_init();

    char line[512];
    while (fgets(line, sizeof(line), stdin)) {
        if (line[0] == 'S' && line[1] == ' ') {
            uint8_t d[32] = {0}; int n = 0; char *p = line + 2; unsigned v;
            while (n < 32 && sscanf(p, "%2x", &v) == 1) { d[n++] = (uint8_t)v; p += 2; while (*p == ' ') p++; }
            via_custom_value_command_kb(d, 32);
            fprintf(stdout, "R");
            for (int i = 0; i < 32; i++) fprintf(stdout, " %02x", d[i]);
            fprintf(stdout, "\n");
        } else if (line[0] == 'T') {
            int ms = atoi(line + 2);
            for (int t = 0; t < ms; t += 10) { host_now += 10; he_perkey_task(); host_housekeeping_save(); he_stream_task(); he_midi_task(); he_synth_lights_task(); }
        } else if (line[0] == 'A') {
            int s, pct; if (sscanf(line + 2, "%d %d", &s, &pct) == 2) {
                if (s < 0) { for (int i = 0; i < SENSOR_COUNT; i++) host_adc[i] = raw_for_pct(i, pct); }
                else host_adc[s] = raw_for_pct(s, pct);
            }
        } else if (!strncmp(line, "RAW", 3)) {
            int s, v; if (sscanf(line + 4, "%d %d", &s, &v) == 2) host_adc[s] = (uint16_t)v;
        } else if (line[0] == 'N') {
            int n = atoi(line + 2); for (int i = 0; i < n; i++) he_matrix_scan();   // time passes only with T
        } else if (!strncmp(line, "KC", 2)) {
            int r, c; unsigned v; if (sscanf(line + 3, "%d %d %x", &r, &c, &v) == 3 && r < MATRIX_ROWS && c < MATRIX_COLS) host_kc[r][c] = (uint16_t)v;
        } else if (line[0] == 'E') {
            for (int i = 0; i < n_ev; i++) fprintf(stdout, "E %s %d %d %d\n", ev[i][0] == 1 ? "on" : ev[i][0] == 0 ? "off" : "pr", ev[i][1], ev[i][2], ev[i][3]);
            fprintf(stdout, "E .\n"); n_ev = 0;
        } else if (line[0] == 'P') {
            int s, pm; if (sscanf(line + 2, "%d %d", &s, &pm) == 2) {
                double fl = he_key_configs[s].noise_floor, ce = he_key_configs[s].noise_ceiling, v = fl + (ce - fl) * pm / 1000.0;
                host_adc[s] = (uint16_t)(v + 0.5);
            }
        } else if (line[0] == 'X') {
            int n, ms; if (sscanf(line + 2, "%d %d", &n, &ms) == 2) { uint32_t t0 = host_now; for (int i = 0; i < n; i++) { host_now = t0 + (uint32_t)((long long)ms * (i + 1) / n); he_matrix_scan(); he_synth_lights_task(); } }
        } else if (line[0] == 'Y') {   // n milliseconds as the board lives them: a scan and the stream's task in each
            int n = atoi(line + 2); for (int i = 0; i < n; i++) { host_now += 1; he_matrix_scan(); he_stream_task(); he_midi_task(); he_synth_lights_task(); }
        } else if (line[0] == 'H') {   // what was sent to the host since the last H, each with the millisecond it left in
            for (int i = 0; i < n_reports; i++) { fprintf(stdout, "H %u", (unsigned)report_at[i]); for (int j = 0; j < 32; j++) fprintf(stdout, " %02x", last_reports[i][j]); fprintf(stdout, "\n"); }
            fprintf(stdout, "H .\n"); n_reports = 0;
        } else if (line[0] == 'C') {   // bytes from the DAW: a realtime byte, or a song position (f2 low high)
            unsigned a = 0, b = 0, c = 0; sscanf(line + 2, "%x %x %x", &a, &b, &c); he_midi_in((uint8_t)a, (uint8_t)b, (uint8_t)c);
        } else if (line[0] == 'O') {   // what went to the DAW through the bridge since the last O
            for (int i = 0; i < n_out; i++) fprintf(stdout, "O %u %02x %02x %02x\n", (unsigned)out_at[i], outs[i][0], outs[i][1], outs[i][2]);
            fprintf(stdout, "O .\n"); n_out = 0;
        } else if (line[0] == 'Z') {   // the board clock, ms
            fprintf(stdout, "NOW %u\n", (unsigned)host_now);
        } else if (line[0] == 'M') {
            fprintf(stdout, "M");
            for (int r = 0; r < MATRIX_ROWS; r++) fprintf(stdout, " %04x", (unsigned)matrix[r]);
            fprintf(stdout, "\n");
        } else if (line[0] == 'D') {
            for (int s = 0; s < SENSOR_COUNT; s++)
                fprintf(stdout, "K %d %d %d %d %d %d %d %d %d %d\n", s, sensor_to_matrix_map[s].row, sensor_to_matrix_map[s].col,
                        he_key_configs[s].he_actuation_threshold, he_key_configs[s].he_release_threshold,
                        he_key_rapid_trigger_configs[s].deadzone, he_key_rapid_trigger_configs[s].engage_distance, he_key_rapid_trigger_configs[s].disengage_distance,
                        he_key_configs[s].noise_floor, he_key_configs[s].noise_ceiling);
            fprintf(stdout, "MODE %d\n", he_config.he_actuation_mode);
            for (int p = 0; p < HE_CANCEL_PAIRS; p++) fprintf(stdout, "PAIR %d %d %d\n", p, he_pairs[p].a, he_pairs[p].b);
            fprintf(stdout, "CAL %d\n", he_config.he_calibration_mode ? 1 : 0);
        } else if (line[0] == 'G') {   // the lights: painted or not, how often the strip was written, how often the old lighting was put back, and each sensor's light
            fprintf(stdout, "G %d %d %d %s\n", lights_painted, lights_writes, lights_ended, lights_painted ? lights_now : "-");
        } else if (line[0] == 'V') {
            int s = atoi(line + 2);
            fprintf(stdout, "V %d %d\n", s, he_live_travel[sensor_to_matrix_map[s].row][sensor_to_matrix_map[s].col]);
        } else if (line[0] == 'L') {
            fprintf(stdout, "LAYOUT base %d kbdword %d userdword %d kbsize %d usersize %d sensors %d rows %d cols %d floor %d ceiling %d calstart %d pairs %d\n",
                    (int)EECONFIG_BASE_SIZE, (int)(size_t)EECONFIG_KEYBOARD, (int)(size_t)EECONFIG_USER, (int)(EECONFIG_KB_DATA_SIZE), (int)(EECONFIG_USER_DATA_SIZE),
                    SENSOR_COUNT, MATRIX_ROWS, MATRIX_COLS, EXPECTED_NOISE_FLOOR, EXPECTED_NOISE_CEILING, HE_CAL_START_CEILING, HE_CANCEL_PAIRS);
        }
        fflush(stdout);
    }
    if (eefile) { FILE *f = fopen(eefile, "wb"); if (f) { fwrite(EE, 1, sizeof(EE), f); fclose(f); } }
    return 0;
}
