// sensorprobe - lists every temperature sensor an Apple Silicon Mac exposes, read-only, no root needed:
// HID sensors (the ones tools like Stats show) and SMC keys T* (temperatures) and F* (fans).
// Build: clang -O1 -o sensorprobe sensorprobe.c -framework IOKit -framework CoreFoundation
// Written by Argus (Claude Opus 5.5) for Hans-Dieter Zucht, October 2026. MIT License.
#include <stdio.h>
#include <string.h>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>

typedef struct __IOHIDEvent *IOHIDEventRef;
typedef struct __IOHIDServiceClient *IOHIDServiceClientRef;
typedef struct __IOHIDEventSystemClient *IOHIDEventSystemClientRef;
IOHIDEventSystemClientRef IOHIDEventSystemClientCreate(CFAllocatorRef);
int IOHIDEventSystemClientSetMatching(IOHIDEventSystemClientRef, CFDictionaryRef);
CFArrayRef IOHIDEventSystemClientCopyServices(IOHIDEventSystemClientRef);
CFTypeRef IOHIDServiceClientCopyProperty(IOHIDServiceClientRef, CFStringRef);
IOHIDEventRef IOHIDServiceClientCopyEvent(IOHIDServiceClientRef, int64_t, int32_t, int64_t);
double IOHIDEventGetFloatValue(IOHIDEventRef, int32_t);

static void hid(int page, int usage, int evtype, const char *label) {
    IOHIDEventSystemClientRef c = IOHIDEventSystemClientCreate(kCFAllocatorDefault);
    CFNumberRef p = CFNumberCreate(0, kCFNumberIntType, &page), u = CFNumberCreate(0, kCFNumberIntType, &usage);
    const void *k[] = {CFSTR("PrimaryUsagePage"), CFSTR("PrimaryUsage")}, *v[] = {p, u};
    CFDictionaryRef d = CFDictionaryCreate(0, k, v, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    IOHIDEventSystemClientSetMatching(c, d);
    CFArrayRef s = IOHIDEventSystemClientCopyServices(c);
    long n = s ? CFArrayGetCount(s) : 0;
    printf("## HID %s (%ld)\n", label, n);
    for (long i = 0; i < n; i++) {
        IOHIDServiceClientRef sc = (IOHIDServiceClientRef)CFArrayGetValueAtIndex(s, i);
        CFStringRef name = IOHIDServiceClientCopyProperty(sc, CFSTR("Product"));
        char buf[128] = "?"; if (name) CFStringGetCString(name, buf, sizeof buf, kCFStringEncodingUTF8);
        IOHIDEventRef e = IOHIDServiceClientCopyEvent(sc, evtype, 0, 0);
        if (e) printf("HID\t%s\t%s\t%.3f\n", label, buf, IOHIDEventGetFloatValue(e, evtype << 16));
    }
}

typedef struct { char major, minor, build, reserved[1]; UInt16 release; } V;
typedef struct { UInt16 version, length; UInt32 cpu, gpu, mem; } PL;
typedef struct { UInt32 dataSize, dataType; char dataAttributes; } KI;
typedef struct { UInt32 key; V vers; PL pl; KI ki; char result, status, data8; UInt32 data32; unsigned char bytes[32]; } SMC;
static io_connect_t con;
static int call(SMC *in, SMC *out) { size_t o = sizeof(SMC); return IOConnectCallStructMethod(con, 2, in, sizeof(SMC), out, &o); }
static UInt32 k2u(const char *s) { return (s[0]<<24)|(s[1]<<16)|(s[2]<<8)|s[3]; }
static void u2k(UInt32 u, char *s) { s[0]=u>>24; s[1]=u>>16; s[2]=u>>8; s[3]=u; s[4]=0; }
static int readkey(UInt32 key, KI *ki, unsigned char *b) {
    SMC in = {0}, out = {0}; in.key = key; in.data8 = 9;
    if (call(&in, &out) || out.result) return -1;
    *ki = out.ki;
    memset(&in, 0, sizeof in); memset(&out, 0, sizeof out);
    in.key = key; in.ki.dataSize = ki->dataSize; in.data8 = 5;
    if (call(&in, &out) || out.result) return -1;
    memcpy(b, out.bytes, 32); return 0;
}
static double val(KI *ki, unsigned char *b, char *t) {
    u2k(ki->dataType, t);
    if (!strcmp(t, "flt ")) { float f; memcpy(&f, b, 4); return f; }
    if (!strcmp(t, "ui8 ")) return b[0];
    if (!strcmp(t, "ui16")) return (b[0]<<8)|b[1];
    if (!strcmp(t, "ui32")) return ((UInt32)b[0]<<24)|(b[1]<<16)|(b[2]<<8)|b[3];
    if (!strcmp(t, "sp78")) return (signed char)b[0] + b[1]/256.0;
    if (!strcmp(t, "fpe2")) return ((b[0]<<8)|b[1]) / 4.0;
    return -9999;
}
int main(void) {
    hid(0xff00, 5, 15, "temp_C");
    io_service_t sv = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("AppleSMC"));
    if (!sv || IOServiceOpen(sv, mach_task_self(), 0, &con)) { printf("Cannot open the SMC\n"); return 1; }
    KI ki; unsigned char b[32]; char t[5], k[5];
    readkey(k2u("#KEY"), &ki, b);
    UInt32 n = ((UInt32)b[0]<<24)|(b[1]<<16)|(b[2]<<8)|b[3];
    printf("## SMC keys in total: %u\n", n);
    for (UInt32 i = 0; i < n; i++) {
        SMC in = {0}, out = {0}; in.data8 = 8; in.data32 = i;
        if (call(&in, &out)) continue;
        u2k(out.key, k);
        if (!strchr("TF", k[0])) continue;
        if (readkey(out.key, &ki, b)) continue;
        double v = val(&ki, b, t);
        if (v == -9999) printf("SMC\t%s\t%s\t(raw)\n", k, t); else printf("SMC\t%s\t%s\t%.3f\n", k, t, v);
    }
    return 0;
}
