// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Host test for src/cds_values.hpp: CDS text parsing and the display rows.
#include <stdio.h>
#include <string.h>
#include "cds_values.hpp"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)
#define CHECK_STR(a, b) do { if (strcmp((a), (b)) != 0) { printf("FAIL %s:%d: '%s' != '%s'\n", __FILE__, __LINE__, (a), (b)); ++g_fail; } } while (0)

#define DEG "\xC2\xB0"

static void rows(const eng::Metrics& m, char r[eng::kRowCount][eng::kRowLen]) { eng::formatRows(m, r); }

int main() {
    // ---- numField ----
    double v = -1;
    CHECK(eng::numField("{\"RPMSpeed\":2453}", "RPMSpeed", &v) && v == 2453);
    CHECK(eng::numField("{\"temperatureExterior\":14.0}", "temperatureExterior", &v) && v == 14.0);
    CHECK(eng::numField("{\"temperatureExterior\": -3.5}", "temperatureExterior", &v) && v == -3.5);
    CHECK(!eng::numField("{\"error\":400}", "RPMSpeed", &v));
    CHECK(!eng::numField("{\"RPMSpeed\":null}", "RPMSpeed", &v));
    CHECK(!eng::numField("{\"RPMSpeed\":\"fast\"}", "RPMSpeed", &v));
    CHECK(!eng::numField("{\"RPMSpeed\":-inf}", "RPMSpeed", &v));
    CHECK(!eng::numField("", "RPMSpeed", &v));
    CHECK(!eng::numField(NULL, "RPMSpeed", &v));
    CHECK(eng::numField("{\"temperature\":{\"engine\":92,\"oil\":104}}", "oil", &v) && v == 104);
    CHECK(eng::numField("{\"temperature\":{\"engine\":92,\"oil\":104}}", "engine", &v) && v == 92);

    // ---- narrow / utf8to16 ----
    const unsigned short u16[] = { '{', '"', 'a', '"', ':', '1', 0x142, '}', 0 };
    char nb[32];
    CHECK(eng::narrow(u16, nb, sizeof(nb)) == 8);
    CHECK_STR(nb, "{\"a\":1?}");
    char small[4];
    eng::narrow(u16, small, sizeof(small));
    CHECK_STR(small, "{\"a");
    unsigned short wide[16];
    size_t n = eng::utf8to16("A" DEG "C", wide, 16);
    CHECK(n == 3 && wide[0] == 'A' && wide[1] == 0xB0 && wide[2] == 'C' && wide[3] == 0);
    n = eng::utf8to16("Pr\xC4\x99", wide, 16);                       // Prę
    CHECK(n == 3 && wide[2] == 0x119);
    n = eng::utf8to16("\xC5\x9B", wide, 2);                           // fits exactly (1 unit + terminator)
    CHECK(n == 1 && wide[0] == 0x15B);
    n = eng::utf8to16("abc", wide, 3);                                // truncated to capacity - 1
    CHECK(n == 2 && wide[2] == 0);
    n = eng::utf8to16("\xFF", wide, 4);
    CHECK(n == 1 && wide[0] == '?');

    // ---- apply: engine.temperature ----
    eng::Metrics m;
    CHECK(eng::apply(m, eng::kEngineTemp, "{\"temperature\":{\"engine\":92,\"oil\":104}}"));
    CHECK(m.hasCoolant && m.coolant == 92 && m.hasOil && m.oil == 104);
    CHECK(!eng::apply(m, eng::kEngineTemp, "{\"temperature\":{\"engine\":92,\"oil\":104}}"));   // unchanged
    CHECK(eng::apply(m, eng::kEngineTemp, "{\"temperature\":{\"engine\":93,\"oil\":104}}"));    // coolant moved
    CHECK(m.coolant == 93);
    CHECK(eng::apply(m, eng::kEngineTemp, "{\"error\":400}"));                                  // both become invalid
    CHECK(!m.hasCoolant && !m.hasOil);
    CHECK(!eng::apply(m, eng::kEngineTemp, "{\"error\":400}"));                                 // still invalid: no change
    CHECK(eng::apply(m, eng::kEngineTemp, "{\"temperature\":{\"engine\":900,\"oil\":18}}"));   // implausible coolant
    CHECK(!m.hasCoolant && m.hasOil && m.oil == 18);

    // ---- apply: the rest ----
    CHECK(eng::apply(m, eng::kRpm, "{\"RPMSpeed\":2453}") && m.hasRpm && m.rpm == 2453);
    CHECK(eng::apply(m, eng::kRpm, "{\"RPMSpeed\":99999}") && !m.hasRpm);
    CHECK(eng::apply(m, eng::kSpeed, "{\"speedActual\":87.4}") && m.hasSpeed);
    CHECK(eng::apply(m, eng::kGear, "{\"gear\":6}") && m.hasGear && m.gear == 6);
    CHECK(!eng::apply(m, eng::kGear, "{\"gear\":6}"));
    CHECK(eng::apply(m, eng::kGear, "{\"gear\":-1}") && !m.hasGear);
    CHECK(eng::apply(m, eng::kFuel, "{\"fuel\":{\"range\":522,\"reserve\":0,\"tanklevel\":39}}"));
    CHECK(m.hasFuel && m.fuel == 39 && m.hasRange && m.range == 522);
    CHECK(eng::apply(m, eng::kOutside, "{\"temperatureExterior\":14.0}") && m.hasOutside && m.outside == 14.0);
    CHECK(eng::propValid(m, eng::kFuel) && eng::propValid(m, eng::kOutside) && !eng::propValid(m, eng::kRpm));

    // ---- rows: nothing known yet ----
    char r[eng::kRowCount][eng::kRowLen];
    eng::Metrics empty;
    rows(empty, r);
    CHECK_STR(r[0], "Woda: --");
    CHECK_STR(r[1], "Olej: --");
    CHECK_STR(r[2], "Obroty: --");
    CHECK_STR(r[3], "Pr\xC4\x99" "dko" "\xC5\x9B" "\xC4\x87" ": --");
    CHECK_STR(r[4], "Bieg: --");
    CHECK_STR(r[5], "Paliwo: --, zasi\xC4\x99" "g: --");
    CHECK_STR(r[6], "Temp. zewn.: --");

    // ---- rows: full data ----
    eng::Metrics f;
    eng::apply(f, eng::kEngineTemp, "{\"temperature\":{\"engine\":92.4,\"oil\":104.6}}");
    eng::apply(f, eng::kRpm, "{\"RPMSpeed\":2453}");
    eng::apply(f, eng::kSpeed, "{\"speedActual\":87.4}");
    eng::apply(f, eng::kGear, "{\"gear\":6}");
    eng::apply(f, eng::kFuel, "{\"fuel\":{\"range\":522,\"reserve\":0,\"tanklevel\":38.6}}");
    eng::apply(f, eng::kOutside, "{\"temperatureExterior\":-3.46}");
    rows(f, r);
    CHECK_STR(r[0], "Woda: 92 " DEG "C");
    CHECK_STR(r[1], "Olej: 105 " DEG "C");
    CHECK_STR(r[2], "Obroty: 2450 obr/min");
    CHECK_STR(r[3], "Pr\xC4\x99" "dko" "\xC5\x9B" "\xC4\x87" ": 87 km/h");
    CHECK_STR(r[4], "Bieg: D3");
    CHECK_STR(r[5], "Paliwo: 39 l, zasi\xC4\x99" "g: 522 km");
    CHECK_STR(r[6], "Temp. zewn.: -3,5 " DEG "C");

    // ---- gear names ----
    char g[8];
    eng::gearText(1, g, sizeof(g)); CHECK_STR(g, "N");
    eng::gearText(2, g, sizeof(g)); CHECK_STR(g, "R");
    eng::gearText(3, g, sizeof(g)); CHECK_STR(g, "P");
    eng::gearText(4, g, sizeof(g)); CHECK_STR(g, "D1");
    eng::gearText(11, g, sizeof(g)); CHECK_STR(g, "D8");
    eng::gearText(0, g, sizeof(g)); CHECK_STR(g, "--");

    // ---- no row exceeds the label width (about 34 characters, counting UTF-8 characters not bytes) ----
    for (int i = 0; i < eng::kRowCount; ++i) {
        unsigned short w[eng::kRowLen];
        CHECK(eng::utf8to16(r[i], w, eng::kRowLen) <= 34);
    }

    if (g_fail) { printf("%d check(s) failed\n", g_fail); return 1; }
    printf("test_cds_values OK\n");
    return 0;
}
