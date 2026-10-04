// fanguard - drives the fan(s) of an Apple Silicon Mac from the hottest of a chosen group of SMC temperature sensors.
//
// Reading needs no privileges; writing (set, auto, test, run) needs root.
// Below `start` degrees Apple's automatic fan control stays in charge. From `start` to `full` the fan speed rises
// linearly from minimum to maximum, above `full` it runs at maximum. It never drops below the speed Apple's
// controller was running when fanguard took over, and it hands control back to Apple on every exit.
//
// Written by Argus (Claude Opus 5.5) for Hans-Dieter Zucht, October 2026. Tested on a Mac mini M6 (Mac18,5).
// MIT License.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <IOKit/IOKitLib.h>

#define MODE_FILE "/Users/Shared/mac-fan-guard/mode"

// ---- SMC access (AppleSMC user client, selector 2) ----
typedef struct { char major, minor, build, reserved[1]; UInt16 release; } Vers;
typedef struct { UInt16 version, length; UInt32 cpu, gpu, mem; } PLimit;
typedef struct { UInt32 dataSize, dataType; char dataAttributes; } KeyInfo;
typedef struct { UInt32 key; Vers vers; PLimit pl; KeyInfo ki; char result, status, data8; UInt32 data32; unsigned char bytes[32]; } SMCData;
enum { SMC_READ = 5, SMC_WRITE = 6, SMC_KEY_AT_INDEX = 8, SMC_KEY_INFO = 9 };

static io_connect_t con;
static int smc_call(SMCData *in, SMCData *out) { size_t o = sizeof(SMCData); return IOConnectCallStructMethod(con, 2, in, sizeof(SMCData), out, &o); }
static UInt32 k2u(const char *s) { return ((UInt32)s[0]<<24)|((UInt32)s[1]<<16)|((UInt32)s[2]<<8)|(UInt32)s[3]; }
static void u2k(UInt32 u, char *s) { s[0]=u>>24; s[1]=u>>16; s[2]=u>>8; s[3]=u; s[4]=0; }

static int smc_info(const char *key, KeyInfo *ki) {
    SMCData in = {0}, out = {0}; in.key = k2u(key); in.data8 = SMC_KEY_INFO;
    if (smc_call(&in, &out) || out.result) return -1;
    *ki = out.ki; return 0;
}
static int smc_read(const char *key, KeyInfo *ki, unsigned char *b) {
    if (smc_info(key, ki)) return -1;
    SMCData in = {0}, out = {0}; in.key = k2u(key); in.ki.dataSize = ki->dataSize; in.data8 = SMC_READ;
    if (smc_call(&in, &out) || out.result) return -1;
    memcpy(b, out.bytes, 32); return 0;
}
static int smc_write(const char *key, const unsigned char *b, UInt32 size) {
    SMCData in = {0}, out = {0}; in.key = k2u(key); in.ki.dataSize = size; in.data8 = SMC_WRITE;
    memcpy(in.bytes, b, size);
    int r = smc_call(&in, &out);
    return r ? r : out.result;
}
static double smc_val(const char *key) {
    KeyInfo ki; unsigned char b[32]; char t[5];
    if (smc_read(key, &ki, b)) return -1;
    u2k(ki.dataType, t);
    if (!strcmp(t, "flt ")) { float f; memcpy(&f, b, 4); return f; }
    if (!strcmp(t, "ui8 ")) return b[0];
    if (!strcmp(t, "ui16")) return (b[0]<<8)|b[1];
    return -1;
}
static int write_flt(const char *key, float f) { unsigned char b[4]; memcpy(b, &f, 4); return smc_write(key, b, 4); }
static int write_u8(const char *key, unsigned char v) { return smc_write(key, &v, 1); }

// ---- Settings (fanguard.conf next to the binary, lines key=value) ----
static char sensor_prefixes[256] = "TVD,Tg,Tp";   // prefixes of the SMC keys that decide
static double t_start = 60, t_full = 85, hysteresis = 5, interval = 2;
static void load_conf(const char *path) {
    FILE *f = fopen(path, "r"); if (!f) return;
    char line[300];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        char *v = strchr(line, '='); if (!v) continue; *v++ = 0; v[strcspn(v, "\r\n #")] = 0;
        if (!strcmp(line, "sensors")) snprintf(sensor_prefixes, sizeof sensor_prefixes, "%s", v);
        else if (!strcmp(line, "start")) t_start = atof(v);
        else if (!strcmp(line, "full")) t_full = atof(v);
        else if (!strcmp(line, "hysteresis")) hysteresis = atof(v);
        else if (!strcmp(line, "interval")) interval = atof(v);
    }
    fclose(f);
}

// ---- Sensors: enumerate all T* keys once, remember which ones decide ----
#define MAX_SENSORS 400
static char skey[MAX_SENSORS][5]; static int nsens = 0, decides[MAX_SENSORS];
static int matches_prefix(const char *k) {
    char copy[256]; snprintf(copy, sizeof copy, "%s", sensor_prefixes);
    for (char *p = strtok(copy, ","); p; p = strtok(NULL, ","))
        if (!strncmp(k, p, strlen(p))) return 1;
    return 0;
}
static void find_sensors(void) {
    KeyInfo ki; unsigned char b[32];
    smc_read("#KEY", &ki, b);
    UInt32 n = ((UInt32)b[0]<<24)|(b[1]<<16)|(b[2]<<8)|b[3];
    for (UInt32 i = 0; i < n && nsens < MAX_SENSORS; i++) {
        SMCData in = {0}, out = {0}; in.data8 = SMC_KEY_AT_INDEX; in.data32 = i;
        if (smc_call(&in, &out)) continue;
        char k[5]; u2k(out.key, k);
        if (k[0] != 'T') continue;
        double v = smc_val(k);
        if (v < 5 || v > 130) continue;                    // plausible temperatures only
        strcpy(skey[nsens], k); decides[nsens] = matches_prefix(k); nsens++;
    }
    int any = 0; for (int i = 0; i < nsens; i++) any |= decides[i];
    if (!any) {                                            // none of the prefixes exist on this chip: use all
        for (int i = 0; i < nsens; i++) decides[i] = 1;
        snprintf(sensor_prefixes, sizeof sensor_prefixes, "all T* (configured prefixes not found on this chip)");
    }
}
static double hottest(char *which) {
    double m = -1;
    for (int i = 0; i < nsens; i++) if (decides[i]) { double v = smc_val(skey[i]); if (v > m && v < 130) { m = v; strcpy(which, skey[i]); } }
    return m;
}

// ---- Fans (FNum: Mac mini 1, MacBook Pro 2) ----
static int nfans = 1; static double fmn[8], fmx[8], fmin0, fmax0;
static char *fkey(int i, const char *s) { static char b[8][5]; static int r = 0; r = (r + 1) % 8; snprintf(b[r], 5, "F%d%s", i, s); return b[r]; }
static void read_fans(void) {
    nfans = (int)smc_val("FNum"); if (nfans < 1) nfans = 1; if (nfans > 8) nfans = 8;
    for (int i = 0; i < nfans; i++) { fmn[i] = smc_val(fkey(i, "Mn")); fmx[i] = smc_val(fkey(i, "Mx")); }
    fmin0 = fmn[0]; fmax0 = fmx[0];
}
static double current_rpm(void) { double m = 0; for (int i = 0; i < nfans; i++) { double v = smc_val(fkey(i, "Ac")); if (v > m) m = v; } return m; }
static int is_manual(void) { for (int i = 0; i < nfans; i++) if (smc_val(fkey(i, "md")) > 0) return 1; return 0; }
// rpm refers to fan 0; every other fan gets the same fraction of its own range.
static int set_manual(float rpm) {
    double x = fmax0 > fmin0 ? (rpm - fmin0) / (fmax0 - fmin0) : 1; if (x < 0) x = 0; if (x > 1) x = 1;
    int err = 0;
    for (int i = 0; i < nfans; i++) {
        if (write_u8(fkey(i, "md"), 1)) { write_u8("Ftst", 1); usleep(500000); if (write_u8(fkey(i, "md"), 1)) { err = -1; continue; } }
        if (write_flt(fkey(i, "Tg"), fmn[i] + (fmx[i] - fmn[i]) * x)) err = -1;
    }
    return err;
}
static int set_auto(void) { int r = 0; for (int i = 0; i < nfans; i++) r |= write_u8(fkey(i, "md"), 0); write_u8("Ftst", 0); return r; }

static void read_mode(char *mode, size_t n) {
    snprintf(mode, n, "curve");
    FILE *f = fopen(MODE_FILE, "r");
    if (f) { if (fgets(mode, (int)n, f)) mode[strcspn(mode, "\r\n ")] = 0; fclose(f); }
}
static void stamp(void) { time_t now = time(0); printf("%.19s ", ctime(&now)); }
static volatile sig_atomic_t stop = 0;
static void on_signal(int s) { (void)s; stop = 1; }
static void nap(void) { for (int i = 0; i < interval * 10 && !stop; i++) usleep(100000); }

static void status(void) {
    char which[5] = "-"; double t = hottest(which);
    for (int i = 0; i < nfans; i++)
        printf("Fan %d: %.0f rpm (target %.0f, min %.0f, max %.0f), %s\n", i,
               smc_val(fkey(i, "Ac")), smc_val(fkey(i, "Tg")), fmn[i], fmx[i], smc_val(fkey(i, "md")) > 0 ? "manual" : "automatic");
    printf("Deciding sensors (%s): hottest %s = %.1f °C · curve from %.0f, full at %.0f, hysteresis %.0f K\n",
           sensor_prefixes, which, t, t_start, t_full, hysteresis);
    char mode[32]; read_mode(mode, sizeof mode);
    printf("Service mode: %s  (change: echo curve|auto|0-100 > %s)\n", mode, MODE_FILE);
}

// The service: follows the mode file. "curve": Apple below start, curve above. A number: that percentage as a
// minimum, the curve can still raise it. "auto": Apple only.
static int run(void) {
    int active = 0; double floor_rpm = fmin0; float last = -1;
    char last_mode[32] = "curve";
    printf("fanguard running: sensors %s, curve %.0f-%.0f °C, hysteresis %.0f K, every %.0f s\n", sensor_prefixes, t_start, t_full, hysteresis, interval);
    fflush(stdout);
    while (!stop) {
        char mode[32]; read_mode(mode, sizeof mode);
        if (strcmp(mode, last_mode)) {
            stamp(); printf("mode %s -> %s\n", last_mode, mode); fflush(stdout);
            snprintf(last_mode, sizeof last_mode, "%s", mode); active = 0; last = -1; set_auto();
        }
        if (!strcmp(mode, "auto")) { nap(); continue; }
        char which[5] = "-"; double t = hottest(which);
        if (mode[0] >= '0' && mode[0] <= '9') {            // percentage = minimum; the curve may still go higher
            double p = atof(mode); if (p > 100) p = 100;
            double x = (t - t_start) / (t_full - t_start); if (x < 0) x = 0; if (x > 1) x = 1;
            float rpm = fmin0 + (fmax0 - fmin0) * (p / 100 > x ? p / 100 : x);
            if (last < 0 || rpm - last > 50 || last - rpm > 150) {
                int r = set_manual(rpm); last = rpm;
                stamp(); printf("%s %.1f °C, minimum %.0f %% -> %.0f rpm%s\n", which, t, p, rpm, r ? " (WRITE FAILED)" : ""); fflush(stdout);
            }
            nap(); continue;
        }
        if (!active && t >= t_start) {                      // take over; never go below what Apple was running
            active = 1; floor_rpm = smc_val("F0Ac"); if (floor_rpm < fmin0) floor_rpm = fmin0;
        }
        if (active && t < t_start - hysteresis) {
            active = 0; last = -1; set_auto();
            stamp(); printf("%s %.1f °C -> automatic\n", which, t); fflush(stdout);
        }
        if (active) {
            double x = (t - t_start) / (t_full - t_start); if (x < 0) x = 0; if (x > 1) x = 1;
            float rpm = fmin0 + (fmax0 - fmin0) * x;
            if (rpm < floor_rpm) rpm = floor_rpm;
            if (last < 0 || rpm - last > 50 || last - rpm > 150) {   // rise quickly, fall slowly
                int r = set_manual(rpm); last = rpm;
                stamp(); printf("%s %.1f °C -> %.0f rpm%s\n", which, t, rpm, r ? " (WRITE FAILED)" : ""); fflush(stdout);
            }
        }
        nap();
    }
    set_auto(); printf("Stopped, fans back on automatic.\n");
    return 0;
}

int main(int argc, char **argv) {
    char conf[1024]; snprintf(conf, sizeof conf, "%s", argv[0]);
    char *slash = strrchr(conf, '/'); if (slash) strcpy(slash + 1, "fanguard.conf"); else strcpy(conf, "fanguard.conf");
    load_conf(getenv("FANGUARD_CONF") ? getenv("FANGUARD_CONF") : conf);
    io_service_t sv = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("AppleSMC"));
    if (!sv || IOServiceOpen(sv, mach_task_self(), 0, &con)) { fprintf(stderr, "Cannot open the SMC.\n"); return 1; }
    const char *cmd = argc > 1 ? argv[1] : "status";
    find_sensors(); read_fans();

    if (!strcmp(cmd, "line")) {        // one line for the menu bar: sensor °C rpm manual|auto mode
        char which[5] = "-"; double t;
        if (argc > 2) { snprintf(which, sizeof which, "%s", argv[2]); t = smc_val(which); } else t = hottest(which);
        char mode[32]; read_mode(mode, sizeof mode);
        printf("%s %.1f %.0f %s %s\n", which, t, current_rpm(), is_manual() ? "manual" : "auto", mode);
        return 0;
    }
    if (!strcmp(cmd, "status")) { status(); return 0; }
    if (!strcmp(cmd, "sensors")) {
        for (int i = 0; i < nsens; i++) printf("%s %s %.1f\n", decides[i] ? "*" : " ", skey[i], smc_val(skey[i]));
        printf("* = deciding (sensors=%s)\n", sensor_prefixes); return 0;
    }
    if (geteuid() != 0) { fprintf(stderr, "'%s' writes to the SMC and needs root: sudo %s %s %s\n", cmd, argv[0], cmd, argc > 2 ? argv[2] : ""); return 2; }
    signal(SIGINT, on_signal); signal(SIGTERM, on_signal);

    if (!strcmp(cmd, "auto")) { int r = set_auto(); printf("Automatic %s\n", r ? "FAILED" : "restored"); return r != 0; }
    if (!strcmp(cmd, "set") && argc > 2) {
        double p = atof(argv[2]); if (p < 0) p = 0; if (p > 100) p = 100;
        float rpm = fmin0 + (fmax0 - fmin0) * p / 100;
        int r = set_manual(rpm); printf("Fan %s %.0f %% = %.0f rpm\n", r ? "FAILED to set to" : "set to", p, rpm); return r != 0;
    }
    if (!strcmp(cmd, "test")) {          // 12 s at maximum, then back to automatic
        printf("Before: %.0f rpm, %s\n", current_rpm(), is_manual() ? "manual" : "automatic");
        int r = set_manual(fmax0);
        printf("Setting maximum %.0f rpm: %s\n", fmax0, r ? "FAILED" : "ok");
        for (int s = 0; s < 12 && !stop; s++) { sleep(1); printf("  %2ds: %.0f rpm\n", s + 1, current_rpm()); fflush(stdout); }
        printf("Back to automatic: %s\n", set_auto() ? "FAILED" : "ok");
        sleep(3); printf("After: %.0f rpm, %s\n", current_rpm(), is_manual() ? "manual" : "automatic");
        return r != 0;
    }
    if (!strcmp(cmd, "run")) return run();
    fprintf(stderr, "Commands: status | sensors | line [KEY] | set <0-100> | auto | test | run\n"); return 2;
}
