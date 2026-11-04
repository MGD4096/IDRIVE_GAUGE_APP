// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Host test: loads a host build of libGaugeApp.so the way ManagedPluginApp does (dlsym "pluginAppInfo"), drives
// create / onInit / the posted Runnables / CDS callbacks / onFinalize / destroy against the fake framework and checks
// what ends up in the label models.
#include <dlfcn.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include "fake_appframework.hpp"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)
#define CHECK_STR(a, b) do { std::string _a = (a); if (_a != (b)) { printf("FAIL %s:%d: '%s' != '%s'\n", __FILE__, __LINE__, _a.c_str(), (b)); ++g_fail; } } while (0)

#define DEG "\xC2\xB0"

struct Info {
    uint16_t a, b;
    const char* name;
    const char* vendor;
    uint32_t rhmi;
    bmw::PluginApp* (*create)(bmw::IDispatcher&, const char*, const bmw::Bundle&, const bmw::Bundle&);
    void (*destroy)(void*);
};

// Runs every Runnable that is queued right now once (what the framework does when the delay is over). Returns how
// many ran; Runnables that post themselves again land in the queue for the next call.
static int runPosted() {
    std::vector<std::pair<bmw::Runnable*, unsigned> > now;
    now.swap(g_fake.posted);
    for (size_t i = 0; i < now.size(); ++i) {
        void** vptr = *(void***)now[i].first;
        ((void (*)(void*))vptr[2])(now[i].first);      // slot 2 of the vptr array = run(), as in NBT-AAD's test
    }
    return (int)now.size();
}

static void feed(const char* prop, const char* ascii) {
    std::map<std::string, bmw::CdsDelegate>::iterator it = g_fake.cds.find(prop);
    if (it == g_fake.cds.end()) { printf("FAIL: no CDS handler for %s\n", prop); ++g_fail; return; }
    unsigned short buf[256];
    size_t n = 0;
    for (; ascii[n] && n < 255; ++n) buf[n] = (unsigned char)ascii[n];
    buf[n] = 0;
    bmw::BasicString<unsigned short> s(buf);
    it->second.stub(it->second.object, s);
}

static const std::string& row(unsigned i) { return g_fake.modelText[21 + i]; }

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: %s libGaugeApp.so\n", argv[0]); return 2; }
    void* h = dlopen(argv[1], RTLD_NOW | RTLD_GLOBAL);
    if (!h) { printf("dlopen: %s\n", dlerror()); return 1; }
    Info* info = (Info*)dlsym(h, "pluginAppInfo");
    if (!info) { printf("no pluginAppInfo\n"); return 1; }
    printf("pluginAppInfo: %u.%u name=%s vendor=%s rhmi=%u\n", info->a, info->b, info->name, info->vendor, info->rhmi);
    CHECK(info->a == 1 && info->b == 0 && info->rhmi == 1);

    char dummy[16];
    bmw::PluginApp* app = info->create(*(bmw::IDispatcher*)dummy, "GaugeApp", *(bmw::Bundle*)dummy, *(bmw::Bundle*)dummy);
    if (!app) { printf("create returned NULL\n"); return 1; }
    CHECK(strcmp(app->name, "GaugeApp") == 0);
    CHECK(info->create(*(bmw::IDispatcher*)dummy, "GaugeApp", *(bmw::Bundle*)dummy, *(bmw::Bundle*)dummy) == NULL);   // single instance

    // ---- onInit (virtual call: reaches the library's override through its own vtable) ----
    bool ok = app->onInit();
    CHECK(ok);
    CHECK(g_fake.baseOnInit == 1);                       // the base onInit ran
    CHECK(g_fake.modelWrites.size() == 7);               // every row written once with its placeholder
    CHECK_STR(row(0), "Woda: --");
    CHECK_STR(row(4), "Bieg: --");
    CHECK(g_fake.posted.size() == 1 && g_fake.posted[0].second == 1500);   // the CDS subscription
    CHECK(g_fake.cds.empty());                           // not yet subscribed

    // ---- the subscription runs: six handlers, then the render loop starts ----
    runPosted();
    CHECK(g_fake.cds.size() == 6);
    CHECK(g_fake.cds.count("engine.temperature") && g_fake.cds.count("engine.RPMSpeed") &&
          g_fake.cds.count("driving.speedActual") && g_fake.cds.count("driving.gear") &&
          g_fake.cds.count("sensors.fuel") && g_fake.cds.count("sensors.temperatureExterior"));
    CHECK(g_fake.posted.size() == 1 && g_fake.posted[0].second == 500);

    // ---- values arrive, the render tick writes them ----
    feed("engine.temperature", "{\"temperature\":{\"engine\":92,\"oil\":104}}");
    feed("engine.RPMSpeed", "{\"RPMSpeed\":2453}");
    feed("driving.speedActual", "{\"speedActual\":87}");
    feed("driving.gear", "{\"gear\":6}");
    feed("sensors.fuel", "{\"fuel\":{\"range\":522,\"reserve\":0,\"tanklevel\":39}}");
    feed("sensors.temperatureExterior", "{\"temperatureExterior\":14.0}");
    CHECK_STR(row(0), "Woda: --");                       // nothing is written before the tick
    runPosted();
    CHECK_STR(row(0), "Woda: 92 " DEG "C");
    CHECK_STR(row(1), "Olej: 104 " DEG "C");
    CHECK_STR(row(2), "Obroty: 2450 obr/min");
    CHECK_STR(row(3), "Pr\xC4\x99" "dko" "\xC5\x9B" "\xC4\x87" ": 87 km/h");
    CHECK_STR(row(4), "Bieg: D3");
    CHECK_STR(row(5), "Paliwo: 39 l, zasi\xC4\x99" "g: 522 km");
    CHECK_STR(row(6), "Temp. zewn.: 14,0 " DEG "C");
    CHECK(g_fake.posted.size() == 1);                    // render loop re-posted itself

    // ---- only changed rows are written ----
    std::map<unsigned, int> before = g_fake.modelWrites;
    feed("engine.RPMSpeed", "{\"RPMSpeed\":3100}");
    feed("engine.temperature", "{\"temperature\":{\"engine\":92,\"oil\":104}}");   // identical
    runPosted();
    CHECK_STR(row(2), "Obroty: 3100 obr/min");
    CHECK(g_fake.modelWrites[23] == before[23] + 1);
    CHECK(g_fake.modelWrites[21] == before[21] && g_fake.modelWrites[22] == before[22]);
    runPosted();                                         // nothing dirty: no writes at all
    CHECK(g_fake.modelWrites[23] == before[23] + 1);

    // ---- rows that stay within one displayed value are not rewritten (RPM 3100 -> 3104 shows 3100 both times) ----
    int w = g_fake.modelWrites[23];
    feed("engine.RPMSpeed", "{\"RPMSpeed\":3104}");
    runPosted();
    CHECK(g_fake.modelWrites[23] == w);

    // ---- error / bad values go back to "--" ----
    feed("engine.temperature", "{\"error\":400}");
    feed("sensors.temperatureExterior", "{\"temperatureExterior\":null}");
    runPosted();
    CHECK_STR(row(0), "Woda: --");
    CHECK_STR(row(1), "Olej: --");
    CHECK_STR(row(6), "Temp. zewn.: --");
    CHECK_STR(row(2), "Obroty: 3100 obr/min");           // untouched

    // ---- negative outside temperature, neutral, empty text ----
    feed("sensors.temperatureExterior", "{\"temperatureExterior\":-3.5}");
    feed("driving.gear", "{\"gear\":1}");
    feed("driving.speedActual", "");
    runPosted();
    CHECK_STR(row(6), "Temp. zewn.: -3,5 " DEG "C");
    CHECK_STR(row(4), "Bieg: N");
    CHECK_STR(row(3), "Pr\xC4\x99" "dko" "\xC5\x9B" "\xC4\x87" ": --");

    // ---- onError is harmless, then finalize: the render loop stops posting ----
    bmw::BasicString<char> msg("integration error");
    app->onError(7, msg);
    app->onFinalize();
    CHECK(g_fake.baseOnFinalize == 1);
    runPosted();                                         // the queued render tick sees the app is gone ...
    CHECK(g_fake.posted.empty());                        // ... and does not post again
    info->destroy(app);

    if (g_fail) { printf("%d check(s) failed\n", g_fail); return 1; }
    printf("app_host_test OK\n");
    return 0;
}
