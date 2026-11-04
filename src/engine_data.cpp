// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// libEngineData.so - live engine data (coolant / oil temperature, RPM, speed, gear, fuel, outside temperature) as a
// native iDrive "PluginApp" for BMW NBT Evo head units (ID5 / ID6).
//
// How it works: HmiMain3D loads Plugins/EngineData/EngineData.manifest at boot. EngineData.xml adds a menu row and one
// screen (hmiState 10) with seven labels, each bound to a raDataModel (text). This library subscribes to the car's
// CDS properties (the same call NBT-AAD uses for driving.gear / sensors.PDCStatus), keeps the latest values and, twice
// a second, writes changed rows into the label models with PluginApp::updateModel(model, text).
//
// The framework glue (PluginApp constructor, own vtable, Runnable stand-in, pluginAppInfo) is the pattern car-verified
// by NBT-AAD's rhmiapp/nbt_aad_app.cpp (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto); only the declarations
// that are needed here are repeated, there are no BMW headers. Not built or run against a real head unit yet - see
// README.md.
//
// Logs: slog (code 0x4e47) always; plus <usb stick>/nbt-engine-data.log if that file already exists on a stick
// (create an empty one on a PC to switch it on).
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <new>
#include <dirent.h>
#include <sys/stat.h>
#include <dlfcn.h>
#ifndef RTLD_DEFAULT
#define RTLD_DEFAULT ((void*)-2)   // QNX value
#endif
#include <pthread.h>
#ifdef __QNX__
#include <sys/slog.h>
#include <sys/slogcodes.h>
#endif
#include "cds_values.hpp"

#define ENGINE_APP_VERSION "engine-data-0.1.0 (" __DATE__ " " __TIME__ ")"

// ---- minimal declarations of BMW's RHMI app framework (libAppFramework.A.so / libCoreFramework.A.so) ----------------
// Names and parameter types only (they produce the exported, mangled symbols); nothing here is defined. All of these
// symbols are exported by the libraries (checked with nm -D on the copies from the car).
namespace bmw {
class IDispatcher;
class Bundle;
class Runnable;
enum RHMIInterfaceVersion { RHMI_INTERFACE_VERSION_1 = 1 };

template <typename T> class BasicString {
public:
    BasicString(const T* s);                 // _ZN3bmw11BasicStringIcEC1EPKc / ...ItEC1EPKt (UTF-16)
    BasicString(const BasicString& other);   // char only is exported; never used for UTF-16
    ~BasicString();
    const T* raw() const { return m_data; }  // the data pointer is the only member
private:
    T* m_data;
};

template <typename R, typename A1, typename A2, typename A3, typename A4> struct Delegate {
    void* object;
    R (*stub)(void* object, A1 a1);
};
typedef Delegate<bool, const BasicString<unsigned short>&, void, void, void> CdsDelegate;

const size_t kPluginAppStorage = 0x200;      // SecurityMenu's own members start at 0x160

class PluginApp {
public:
    PluginApp(IDispatcher& dispatcher, BasicString<char> name, unsigned major, unsigned minor,
              RHMIInterfaceVersion rhmi, const Bundle& params, const Bundle& config);
    void post(Runnable* runnable, unsigned delayMs);                       // runs runnable->run() on the app thread
    void updateModel(unsigned modelId, const BasicString<unsigned short>& text);   // raDataModel text (UTF-16)
    void cdsAddPropertyChangedEventHandler(BasicString<char>& property, unsigned id, const CdsDelegate& handler);
    bool onInit();                           // base implementations, called from our overrides
    void onFinalize();
private:
    unsigned char m_storage[kPluginAppStorage];
};
} // namespace bmw

namespace {

// ---- constants --------------------------------------------------------------------------------------------------
const unsigned    kFirstRowModel = 21;       // raDataModel ids 21..27 in EngineData.xml, one per row
const unsigned    kSubscribeDelayMs = 1500;  // CDS handlers are registered ~1.5 s after onInit, like NBT-AAD / SecurityMenu
const unsigned    kRenderMs = 500;           // label refresh period (only changed rows are written)
const long        kNoDataWarnS = 25;         // log once when no CDS value arrived this long after subscribing
const char* const kDisableA = "/opt/engine/app.disable";
const char* const kDisableB = "/var/opt/engine/app.disable";
const char* const kUsbLogName = "nbt-engine-data.log";
const long        kUsbLogMaxBytes = 4L * 1024L * 1024L;
enum { kSlotOnError = 21, kSlotOnInit = 22, kSlotOnFinalize = 23, kVtblSlots = 34 };   // PluginApp vtable layout

// ---- logging ---------------------------------------------------------------------------------------------------
pthread_mutex_t g_logMutex = PTHREAD_MUTEX_INITIALIZER;
char   g_logPath[320] = "";
time_t g_logChecked = 0;

bool fileExists(const char* p) {
    struct stat st;
    return stat(p, &st) == 0;
}

// <stick>/nbt-engine-data.log of the first /mnt/umass* stick that already holds that file (an empty file made on a
// PC switches the log on). Looked up at most every 5 s.
bool usbLogPath(char* out, size_t n) {
    pthread_mutex_lock(&g_logMutex);
    time_t now = time(NULL);
    if (!g_logChecked || now - g_logChecked >= 5 || now < g_logChecked) {
        g_logChecked = now;
        g_logPath[0] = 0;
        DIR* d = opendir("/mnt");
        if (d) {
            struct dirent* e;
            while ((e = readdir(d)) != NULL) {
                if (strncmp(e->d_name, "umass", 5) != 0) continue;
                char p[320];
                snprintf(p, sizeof(p), "/mnt/%.200s/%s", e->d_name, kUsbLogName);
                struct stat st;
                if (stat(p, &st) == 0 && (long)st.st_size < kUsbLogMaxBytes && access(p, W_OK) == 0) {
                    snprintf(g_logPath, sizeof(g_logPath), "%s", p);
                    break;
                }
            }
            closedir(d);
        }
    }
    bool ok = g_logPath[0] != 0;
    if (ok) snprintf(out, n, "%s", g_logPath);
    pthread_mutex_unlock(&g_logMutex);
    return ok;
}

void elog(const char* fmt, ...) {
    char msg[320];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    char line[400];
    int n = snprintf(line, sizeof(line), "[%5ld.%03ld] [EngineData] %s\n", (long)ts.tv_sec,
                     (long)(ts.tv_nsec / 1000000), msg);
    if (n < 0) return;
    if (n >= (int)sizeof(line)) n = (int)sizeof(line) - 1;
    char path[320];
    if (usbLogPath(path, sizeof(path))) {
        int fd = open(path, O_WRONLY | O_APPEND);
        if (fd >= 0) {
            ssize_t w = write(fd, line, (size_t)n);
            (void)w;
            close(fd);
        }
    }
#ifdef __QNX__
    slogf(_SLOG_SETCODE(0x4e47, 1), _SLOG_INFO, "[EngineData] %s", msg);
#else
    fputs(line, stderr);                                  // host tests
#endif
}

// ---- state ----------------------------------------------------------------------------------------------------
bmw::PluginApp* g_app = NULL;                // set in create, cleared in finalize / destroy
pthread_mutex_t g_dataMutex = PTHREAD_MUTEX_INITIALIZER;
eng::Metrics    g_metrics;
bool            g_dirty = true;
char            g_shown[eng::kRowCount][eng::kRowLen];   // what each label currently holds (to skip unchanged rows)
bool            g_shownValid = false;
int             g_seen[eng::kPropCount];     // how many values arrived per property
bool            g_lastOk[eng::kPropCount];   // whether the last value of a property was usable
long            g_subscribedAt = -1;         // monotonic s of the subscription, -1 before
bool            g_noDataLogged = false;
int             g_totalSeen = 0;

long monoSec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec;
}

// ---- bmw::Runnable stand-in (vtable: offset-to-top, typeinfo, ~D1, ~D0, run), copied from libCoreFramework's own
// vtable so the type info is right. Static objects, so the destructor slots are no-ops. Same technique as NBT-AAD.
struct StandInRunnable { void** vptr; };
void runnableNoop(void*) {}

void initRunnable(StandInRunnable& r, void** vtbl /* 5 slots */, void (*run)(void*)) {
    void** base = reinterpret_cast<void**>(dlsym(RTLD_DEFAULT, "_ZTVN3bmw8RunnableE"));
    if (base) memcpy(vtbl, base, 5 * sizeof(void*));
    else { vtbl[0] = NULL; vtbl[1] = NULL; }
    vtbl[2] = reinterpret_cast<void*>(&runnableNoop);
    vtbl[3] = reinterpret_cast<void*>(&runnableNoop);
    vtbl[4] = reinterpret_cast<void*>(run);
    r.vptr = vtbl + 2;
}

StandInRunnable g_subRunnable;
StandInRunnable g_renderRunnable;
void* g_subVtbl[5];
void* g_renderVtbl[5];

// ---- display -----------------------------------------------------------------------------------------------------
void writeRow(bmw::PluginApp* app, int row, const char* utf8) {
    unsigned short wide[eng::kRowLen];
    eng::utf8to16(utf8, wide, eng::kRowLen);
    bmw::BasicString<unsigned short> text(wide);
    app->updateModel(kFirstRowModel + (unsigned)row, text);
}

// Formats the current values and writes every row whose text changed. force = write all (first call).
void renderRows(bmw::PluginApp* app, bool force) {
    eng::Metrics snap;
    pthread_mutex_lock(&g_dataMutex);
    snap = g_metrics;
    g_dirty = false;
    pthread_mutex_unlock(&g_dataMutex);
    char rows[eng::kRowCount][eng::kRowLen];
    eng::formatRows(snap, rows);
    for (int i = 0; i < eng::kRowCount; ++i) {
        if (!force && g_shownValid && strcmp(rows[i], g_shown[i]) == 0) continue;
        writeRow(app, i, rows[i]);
        memcpy(g_shown[i], rows[i], eng::kRowLen);
    }
    g_shownValid = true;
}

void renderRun(void* /*self*/) {
    bmw::PluginApp* app = g_app;
    if (!app) return;                                    // finalized: do not post again
    bool dirty;
    pthread_mutex_lock(&g_dataMutex);
    dirty = g_dirty;
    pthread_mutex_unlock(&g_dataMutex);
    if (dirty) renderRows(app, false);
    if (!g_noDataLogged && g_subscribedAt >= 0 && g_totalSeen == 0 && monoSec() - g_subscribedAt >= kNoDataWarnS) {
        g_noDataLogged = true;
        elog("no CDS value arrived %ld s after subscribing: the car may not publish engine.* / sensors.* / driving.* to "
             "plugin apps, or the subscription failed (see README, 'No data')", (long)kNoDataWarnS);
    }
    app->post(reinterpret_cast<bmw::Runnable*>(&g_renderRunnable), kRenderMs);
}

// ---- CDS ---------------------------------------------------------------------------------------------------------
void handleCds(int prop, const bmw::BasicString<unsigned short>& value) {
    char text[200];
    eng::narrow(value.raw(), text, sizeof(text));
    bool changed;
    bool ok;
    int seen;
    bool wasOk;
    pthread_mutex_lock(&g_dataMutex);
    changed = eng::apply(g_metrics, prop, text);
    if (changed) g_dirty = true;
    ok = eng::propValid(g_metrics, prop);
    seen = ++g_seen[prop];
    wasOk = g_lastOk[prop];
    g_lastOk[prop] = ok;
    ++g_totalSeen;
    pthread_mutex_unlock(&g_dataMutex);
    // The first values of every property are logged raw (that is what is needed to check a car's layout); after that
    // only a change between usable / not usable, so a revving engine does not flood the log.
    if (seen <= 5 || ok != wasOk) elog("CDS %s = '%s' -> %s", eng::propName(prop), text, ok ? "ok" : "not usable");
}

typedef bool (*CdsStub)(void*, const bmw::BasicString<unsigned short>&);

template <int P> bool onCds(void* /*app*/, const bmw::BasicString<unsigned short>& v) {
    handleCds(P, v);
    return true;
}

const CdsStub kStubs[eng::kPropCount] = { &onCds<0>, &onCds<1>, &onCds<2>, &onCds<3>, &onCds<4>, &onCds<5> };

void subscribeRun(void* /*self*/) {
    bmw::PluginApp* app = g_app;
    if (!app) return;
    for (int p = 0; p < eng::kPropCount; ++p) {
        bmw::BasicString<char> name(eng::propName(p));
        bmw::CdsDelegate d;
        d.object = app;
        d.stub = kStubs[p];
        app->cdsAddPropertyChangedEventHandler(name, 0, d);
        elog("CDS handler for %s registered", eng::propName(p));
    }
    g_subscribedAt = monoSec();
    initRunnable(g_renderRunnable, g_renderVtbl, &renderRun);
    app->post(reinterpret_cast<bmw::Runnable*>(&g_renderRunnable), kRenderMs);
}

// ---- the app object ---------------------------------------------------------------------------------------------
void* g_vtbl[2 + kVtblSlots];   // [offset-to-top, typeinfo, slots...] copied from bmw::PluginApp's vtable

bool appOnInit(bmw::PluginApp* self) {
    bool ok = self->onInit();                            // base implementation
    for (int p = 0; p < eng::kPropCount; ++p) { g_seen[p] = 0; g_lastOk[p] = false; }
    g_totalSeen = 0;
    g_subscribedAt = -1;
    g_noDataLogged = false;
    g_shownValid = false;
    renderRows(self, true);                              // "--" in every row until the first CDS values arrive
    initRunnable(g_subRunnable, g_subVtbl, &subscribeRun);
    self->post(reinterpret_cast<bmw::Runnable*>(&g_subRunnable), kSubscribeDelayMs);
    elog("onInit: base onInit -> %d, %d label rows written, CDS subscription in %u ms", (int)ok, (int)eng::kRowCount,
         kSubscribeDelayMs);
    return ok;
}

void appOnError(bmw::PluginApp* /*self*/, unsigned code, const bmw::BasicString<char>& text) {
    const char* s = text.raw();
    elog("onError %u: %s", code, s ? s : "(null)");
}

void appOnFinalize(bmw::PluginApp* self) {
    elog("onFinalize");
    g_app = NULL;                                        // posted Runnables see this and stop
    self->onFinalize();
}

bmw::PluginApp* createApp(bmw::IDispatcher& dispatcher, const char* name, const bmw::Bundle& params,
                          const bmw::Bundle& config) {
    elog("create(name=%s) %s", name ? name : "(null)", ENGINE_APP_VERSION);
    if (fileExists(kDisableA) || fileExists(kDisableB)) { elog("disabled by app.disable"); return NULL; }
    if (g_app) { elog("create: already created"); return NULL; }

    void* mem = ::operator new(bmw::kPluginAppStorage);
    memset(mem, 0, bmw::kPluginAppStorage);
    bmw::PluginApp* app = new (mem) bmw::PluginApp(dispatcher, bmw::BasicString<char>(name ? name : "EngineData"), 1, 0,
                                                   bmw::RHMI_INTERFACE_VERSION_1, params, config);

    // Own vtable: identical to bmw::PluginApp's, with our onInit / onError / onFinalize (what a C++ subclass would do).
    void** base = *reinterpret_cast<void***>(app);
    memcpy(g_vtbl, base - 2, sizeof(g_vtbl));
    g_vtbl[2 + kSlotOnInit]     = reinterpret_cast<void*>(&appOnInit);
    g_vtbl[2 + kSlotOnError]    = reinterpret_cast<void*>(&appOnError);
    g_vtbl[2 + kSlotOnFinalize] = reinterpret_cast<void*>(&appOnFinalize);
    *reinterpret_cast<void***>(app) = g_vtbl + 2;

    g_app = app;
    elog("create: app %p (PluginApp vtable %p)", (void*)app, (void*)base);
    return app;
}

void destroyApp(void* app) {
    elog("destroy(%p)", app);
    if (!app) return;
    if (app == g_app) g_app = NULL;
    void** vt = *reinterpret_cast<void***>(app);
    reinterpret_cast<void (*)(void*)>(vt[1])(app);       // deleting destructor (PluginApp::~PluginApp + operator delete)
}

__attribute__((constructor)) void libraryLoaded() {
    elog("library loaded into pid %d, %s", (int)getpid(), ENGINE_APP_VERSION);
}

} // namespace

// ---- entry point looked up by ManagedPluginApp::init (dlsym "pluginAppInfo") ----------------------------------------
extern "C" {
struct EnginePluginAppInfo {
    uint16_t versionMajor;
    uint16_t versionMinor;
    const char* name;
    const char* vendor;
    uint32_t rhmiInterfaceVersion;
    bmw::PluginApp* (*create)(bmw::IDispatcher&, const char*, const bmw::Bundle&, const bmw::Bundle&);
    void (*destroy)(void*);
};
__attribute__((visibility("default"))) EnginePluginAppInfo pluginAppInfo = {
    1, 0, "Engine Data", "NBT-ENGINE", 1, &createApp, &destroyApp
};
}
