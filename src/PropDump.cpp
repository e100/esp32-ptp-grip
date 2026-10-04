#include "PropDump.h"

// Prints one line per property the camera answers for. Used to find which
// properties change between photo and video mode: run it in each mode and
// compare.

static uint8_t intSize(uint16_t type) {
    switch (type) {
        case 0x0001: case 0x0002: return 1;
        case 0x0003: case 0x0004: return 2;
        case 0x0005: case 0x0006: return 4;
        case 0x0007: case 0x0008: return 8;
        case 0x0009: case 0x000A: return 16;
        default:                  return 0;
    }
}

static const char* typeName(uint16_t type) {
    switch (type) {
        case 0x0001: return "i8";
        case 0x0002: return "u8";
        case 0x0003: return "i16";
        case 0x0004: return "u16";
        case 0x0005: return "i32";
        case 0x0006: return "u32";
        case 0x0007: return "i64";
        case 0x0008: return "u64";
        case 0xFFFF: return "str";
        default:     return (type & 0x4000) ? "arr" : "?";
    }
}

static int64_t readInt(const uint8_t* p, uint8_t size, bool isSigned) {
    uint64_t v = 0;
    uint8_t n = size > 8 ? 8 : size;
    for (uint8_t i = 0; i < n; i++) v |= (uint64_t)p[i] << (8 * i);
    if (isSigned && n < 8 && (v & (1ull << (8 * n - 1)))) v |= ~0ull << (8 * n);
    return (int64_t)v;
}

// Bytes taken by one value of `type` at p, or 0 if it doesn't fit in `avail`.
static size_t valueSize(uint16_t type, const uint8_t* p, size_t avail) {
    if (type == 0xFFFF) {                           // string: u8 chars, UTF-16LE
        if (avail < 1) return 0;
        size_t n = 1 + 2 * (size_t)p[0];
        return n <= avail ? n : 0;
    }
    if (type & 0x4000) {                            // array: u32 count, elements
        uint8_t es = intSize(type & ~0x4000);
        if (avail < 4 || es == 0) return 0;
        uint32_t count = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        size_t n = 4 + (size_t)count * es;
        return n <= avail ? n : 0;
    }
    uint8_t s = intSize(type);
    return (s && s <= avail) ? s : 0;
}

static void printValue(Print& out, uint16_t type, const uint8_t* p) {
    if (type == 0xFFFF) {
        out.print('"');
        for (uint8_t i = 0; i < p[0]; i++) {
            uint16_t c = (uint16_t)(p[1 + 2 * i] | (p[2 + 2 * i] << 8));
            if (c == 0) break;
            out.print(c < 0x80 ? (char)c : '?');
        }
        out.print('"');
    } else if (type & 0x4000) {
        uint32_t count = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        out.printf("[%u items]", (unsigned)count);
    } else {
        out.printf("%lld", (long long)readInt(p, intSize(type), type & 1));
    }
}

static CameraResult dumpOne(NikonCamera& camera, uint16_t code, Print& out) {
    const uint8_t* d;
    size_t len;
    CameraResult r = camera.readPropDesc(code, d, len);
    if (r != CAM_OK) return r;

    uint16_t type = (uint16_t)(d[2] | (d[3] << 8));
    out.printf("%04X %-3s %s ", code, typeName(type), d[4] ? "rw" : "ro");

    size_t pos = 5;
    size_t n = valueSize(type, d + pos, len - pos);          // factory default
    if (!n) { out.println("(can't parse)"); return CAM_OK; }
    pos += n;
    n = valueSize(type, d + pos, len - pos);                 // current
    if (!n) { out.println("(can't parse)"); return CAM_OK; }
    out.print("now=");
    printValue(out, type, d + pos);
    pos += n;

    if (pos < len) {
        uint8_t form = d[pos++];
        uint8_t es = intSize(type);
        if (form == 0x01 && es && pos + 3 * es <= len) {
            bool sg = type & 1;
            out.printf("  range %lld..%lld step %lld", (long long)readInt(d + pos, es, sg),
                       (long long)readInt(d + pos + es, es, sg), (long long)readInt(d + pos + 2 * es, es, sg));
        } else if (form == 0x02 && pos + 2 <= len) {
            uint16_t count = (uint16_t)(d[pos] | (d[pos + 1] << 8));
            pos += 2;
            out.printf("  list(%u):", count);
            for (uint16_t i = 0; i < count; i++) {
                size_t vs = valueSize(type, d + pos, len - pos);
                if (!vs) break;
                if (i == 24) { out.print(" ..."); break; }
                out.print(' ');
                printValue(out, type, d + pos);
                pos += vs;
            }
        }
    }
    out.println();
    return CAM_OK;
}

// Property codes tried by a full scan: standard properties and the Nikon
// vendor range. DeviceInfo may not list everything (the ZR lists 21).
struct CodeRange { uint16_t first, last; };
static const CodeRange SCAN_RANGES[] = { { 0x5001, 0x5030 }, { 0xD000, 0xD7FF } };

using VisitFn = CameraResult (*)(NikonCamera& camera, uint16_t code, Print& out);

// Scan state shared by the loops below
struct Scan {
    NikonCamera& camera;
    Print&       out;
    VisitFn      fn;
    unsigned     found = 0;
    uint8_t      failures = 0;   // consecutive transport failures
    uint32_t     lastPoll = 0;

    Scan(NikonCamera& c, Print& o, VisitFn f) : camera(c), out(o), fn(f), lastPoll(millis()) {}

    // Returns false to stop: the connection looks dead.
    bool visit(uint16_t code) {
        // Keep the session alive (answers the camera's keep-alive probes)
        if (millis() - lastPoll > 2000) {
            camera.poll();
            lastPoll = millis();
        }
        CameraResult r = fn(camera, code, out);
        if (r == CAM_OK) found++;
        failures = (r == CAM_E_PROTOCOL || r == CAM_E_TIMEOUT) ? failures + 1 : 0;
        if (failures >= 3 || !camera.isReady()) {
            out.printf("dump: stopped at %04X, connection lost (%s)\n", code, cameraResultStr(r));
            return false;
        }
        return true;
    }

    void scanRanges() {
        out.print("scanning");
        for (const CodeRange& r : SCAN_RANGES) out.printf(" %04X-%04X", r.first, r.last);
        out.println(" (may take a while)");
        bool ok = true;
        for (const CodeRange& r : SCAN_RANGES) {
            for (uint32_t code = r.first; ok && code <= r.last; code++) ok = visit((uint16_t)code);
        }
    }
};

static void printCodes(Print& out, const char* label, const uint16_t* codes, uint16_t n) {
    out.printf("%s (%u):", label, n);
    for (uint16_t i = 0; i < n; i++) {
        if (i % 16 == 0) out.print("\n ");
        out.printf(" %04X", codes[i]);
    }
    out.println();
}

void dumpDeviceInfo(NikonCamera& camera, Print& out) {
    const PtpDeviceInfo& info = camera.deviceInfo();
    out.printf("%s %s firmware %s, vendor extension 0x%08lX v%u, mode %u%s\n",
               info.manufacturer, info.model, info.deviceVersion,
               (unsigned long)info.vendorExtensionId, info.vendorExtensionVersion,
               info.functionalMode, info.truncated ? " (lists truncated)" : "");
    printCodes(out, "operations", info.operations, info.numOperations);
    printCodes(out, "events", info.events, info.numEvents);
    printCodes(out, "properties", info.properties, info.numProperties);
}

void dumpCameraProps(NikonCamera& camera, Print& out, bool fullScan) {
    if (!camera.isReady()) {
        out.println("dump: camera not connected");
        return;
    }
    const PtpDeviceInfo& info = camera.deviceInfo();
    uint32_t start = millis();
    Scan scan(camera, out, dumpOne);
    out.println("=== property dump begin ===");

    if (info.numProperties > 0 && !fullScan) {
        out.printf("DeviceInfo lists %u properties\n", info.numProperties);
        for (uint16_t i = 0; i < info.numProperties; i++) {
            if (!scan.visit(info.properties[i])) break;
        }
    } else {
        scan.scanRanges();
    }
    out.printf("=== property dump end: %u properties, %lu ms ===\n", scan.found, (unsigned long)(millis() - start));
}

// ----------------------------------------------------------------------------
// Watch: snapshot every property's description, and on the next run print the
// ones that changed. Codes are visited in ascending order, so the previous
// snapshot is walked alongside.
// ----------------------------------------------------------------------------

static constexpr size_t   WATCH_MAX = 768;
static uint16_t watchCodes[2][WATCH_MAX];
static uint32_t watchHashes[2][WATCH_MAX];
static size_t   watchCount[2];
static uint8_t  watchCur;          // snapshot being built; the other is the previous one
static size_t   watchPrevPos;
static bool     watchHavePrev;
static unsigned watchChanges;

static uint32_t fnv1a(const uint8_t* d, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ d[i]) * 16777619u;
    return h;
}

static CameraResult watchOne(NikonCamera& camera, uint16_t code, Print& out) {
    const uint8_t* d;
    size_t len;
    CameraResult r = camera.readPropDesc(code, d, len);
    if (r != CAM_OK) return r;
    uint32_t h = fnv1a(d, len);

    uint8_t prev = watchCur ^ 1;
    const char* tag = nullptr;
    if (watchHavePrev) {
        // Codes in the previous snapshot below this one have disappeared
        while (watchPrevPos < watchCount[prev] && watchCodes[prev][watchPrevPos] < code) {
            out.printf("gone    %04X\n", watchCodes[prev][watchPrevPos++]);
            watchChanges++;
        }
        if (watchPrevPos < watchCount[prev] && watchCodes[prev][watchPrevPos] == code) {
            if (watchHashes[prev][watchPrevPos] != h) tag = "changed ";
            watchPrevPos++;
        } else {
            tag = "new     ";
        }
    }
    size_t& n = watchCount[watchCur];
    if (n < WATCH_MAX) {
        watchCodes[watchCur][n] = code;
        watchHashes[watchCur][n] = h;
        n++;
    }
    if (tag) {
        out.print(tag);
        dumpOne(camera, code, out);     // reads it again to print it
        watchChanges++;
    }
    return CAM_OK;
}

void watchCameraProps(NikonCamera& camera, Print& out) {
    if (!camera.isReady()) {
        out.println("watch: camera not connected");
        return;
    }
    uint32_t start = millis();
    watchCount[watchCur] = 0;
    watchPrevPos = 0;
    watchChanges = 0;
    out.println(watchHavePrev ? "=== watch: changes since the last snapshot ===" : "=== watch: taking a first snapshot ===");

    Scan scan(camera, out, watchOne);
    scan.scanRanges();

    if (watchHavePrev) {
        uint8_t prev = watchCur ^ 1;
        while (watchPrevPos < watchCount[prev]) {
            out.printf("gone    %04X\n", watchCodes[prev][watchPrevPos++]);
            watchChanges++;
        }
        out.printf("=== watch end: %u changes among %u properties, %lu ms ===\n", watchChanges,
                   (unsigned)watchCount[watchCur], (unsigned long)(millis() - start));
    } else {
        out.printf("=== watch end: %u properties saved; change something, then send 'w' again ===\n",
                   (unsigned)watchCount[watchCur]);
    }
    watchHavePrev = true;
    watchCur ^= 1;
}
