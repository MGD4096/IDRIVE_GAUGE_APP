// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Engine data for BMW NBT Evo (ID5/ID6) - pure logic, no iDrive framework: parsing of CDS property texts and
// formatting of the display rows. Host-tested in tests/test_cds_values.cpp. C++98 (the QNX SDP 6.5 compiler is GCC 4.4).
//
// The parsing style (key lookup in the JSON-like text the car sends as UTF-16) follows NBT-AAD's rhmiapp/cds_parse.hpp
// (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto). CDS property names and value layouts come from
// https://bimmergestalt.github.io/BMWConnectedAnalysis/cds/ :
//   engine.temperature          {"temperature":{"engine":19,"oil":18}}                  deg C
//   engine.RPMSpeed             {"RPMSpeed":0}
//   driving.speedActual         {"speedActual":0}                                       km/h
//   driving.gear                {"gear":0}                                              1 N, 2 R, 3 P, 4+ D1+
//   sensors.fuel                {"fuel":{"range":522,"reserve":0,"tanklevel":39}}       km / litres
//   sensors.temperatureExterior {"temperatureExterior":14.0}                            deg C
// A value that is missing, not a number or outside a plausible range is shown as "--" (e.g. {"error":400}).
#ifndef NBT_ENGINE_CDS_VALUES_HPP
#define NBT_ENGINE_CDS_VALUES_HPP

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace eng {

// CDS properties the app subscribes to (index = Prop).
enum Prop { kEngineTemp = 0, kRpm, kSpeed, kGear, kFuel, kOutside, kPropCount };

inline const char* propName(int p) {
    switch (p) {
    case kEngineTemp: return "engine.temperature";
    case kRpm:        return "engine.RPMSpeed";
    case kSpeed:      return "driving.speedActual";
    case kGear:       return "driving.gear";
    case kFuel:       return "sensors.fuel";
    case kOutside:    return "sensors.temperatureExterior";
    }
    return "";
}

// UTF-16 (as the car delivers CDS values) -> ASCII; anything above 0x7f becomes '?'. Returns the length.
inline size_t narrow(const unsigned short* in, char* out, size_t n) {
    size_t i = 0;
    if (!out || n == 0) return 0;
    while (in && in[i] && i < n - 1) {
        out[i] = in[i] < 0x80 ? (char)in[i] : '?';
        ++i;
    }
    out[i] = 0;
    return i;
}

// UTF-8 -> UTF-16 (BMP only; an invalid sequence becomes '?'). n = capacity of out in units, terminator included.
inline size_t utf8to16(const char* in, unsigned short* out, size_t n) {
    size_t o = 0;
    if (!out || n == 0) return 0;
    const unsigned char* p = (const unsigned char*)in;
    while (p && *p && o + 1 < n) {
        unsigned c = *p;
        if (c < 0x80) { out[o++] = (unsigned short)c; ++p; }
        else if ((c & 0xe0) == 0xc0 && (p[1] & 0xc0) == 0x80) {
            out[o++] = (unsigned short)(((c & 0x1f) << 6) | (p[1] & 0x3f));
            p += 2;
        } else if ((c & 0xf0) == 0xe0 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80) {
            out[o++] = (unsigned short)(((c & 0x0f) << 12) | ((p[1] & 0x3f) << 6) | (p[2] & 0x3f));
            p += 3;
        } else { out[o++] = '?'; ++p; }
    }
    out[o] = 0;
    return o;
}

// Number after "<key>": in text. false when the key is missing, the value is not a number (null, string, ...) or
// not finite. Accepts integers and decimals ("tanklevel":39, "temperatureExterior":14.0).
inline bool numField(const char* text, const char* key, double* out) {
    if (!text || !key || !out) return false;
    char pat[48];
    size_t kl = strlen(key);
    if (kl + 3 > sizeof(pat)) return false;
    pat[0] = '"';
    memcpy(pat + 1, key, kl);
    pat[kl + 1] = '"';
    pat[kl + 2] = 0;
    const char* p = strstr(text, pat);
    if (!p) return false;
    p += kl + 2;
    while (*p == ' ' || *p == ':') ++p;
    if (!(*p == '-' || *p == '.' || (*p >= '0' && *p <= '9'))) return false;
    char* end = NULL;
    double v = strtod(p, &end);
    if (end == p) return false;
    if (!(v > -1e9 && v < 1e9)) return false;           // also rejects NaN and infinity
    *out = v;
    return true;
}

// Latest known values. A has* flag is false until a valid value arrived and again after an invalid one.
struct Metrics {
    bool   hasCoolant, hasOil, hasRpm, hasSpeed, hasGear, hasFuel, hasRange, hasOutside;
    double coolant, oil, rpm, speed, fuel, range, outside;
    int    gear;
    Metrics()
        : hasCoolant(false), hasOil(false), hasRpm(false), hasSpeed(false), hasGear(false), hasFuel(false),
          hasRange(false), hasOutside(false), coolant(0), oil(0), rpm(0), speed(0), fuel(0), range(0), outside(0),
          gear(0) {}
};

inline bool inRange(double v, double lo, double hi) { return v >= lo && v <= hi; }

// Stores one value; returns true when what is shown for it would change.
inline bool setValue(bool& has, double& cur, bool ok, double v) {
    if (!ok) { bool ch = has; has = false; return ch; }
    bool ch = !has || cur != v;
    has = true;
    cur = v;
    return ch;
}

// Applies the text of one CDS property to m. Returns true when a displayed value changed.
inline bool apply(Metrics& m, int prop, const char* text) {
    double v = 0;
    bool ch = false;
    switch (prop) {
    case kEngineTemp: {
        bool a = numField(text, "engine", &v) && inRange(v, -40, 200);
        ch = setValue(m.hasCoolant, m.coolant, a, v) || ch;
        v = 0;
        bool b = numField(text, "oil", &v) && inRange(v, -40, 250);
        ch = setValue(m.hasOil, m.oil, b, v) || ch;
        break;
    }
    // Note: ok is computed in its own statement; passing numField(.., &v) and v in one call would read v before it is set
    // (the order in which function arguments are evaluated is unspecified).
    case kRpm: {
        bool ok = numField(text, "RPMSpeed", &v) && inRange(v, 0, 12000);
        ch = setValue(m.hasRpm, m.rpm, ok, v);
        break;
    }
    case kSpeed: {
        bool ok = numField(text, "speedActual", &v) && inRange(v, 0, 500);
        ch = setValue(m.hasSpeed, m.speed, ok, v);
        break;
    }
    case kGear: {
        bool ok = numField(text, "gear", &v) && inRange(v, 0, 30);
        int g = ok ? (int)v : 0;
        ch = (m.hasGear != ok) || (ok && m.gear != g);
        m.hasGear = ok;
        if (ok) m.gear = g;
        break;
    }
    case kFuel: {
        bool a = numField(text, "tanklevel", &v) && inRange(v, 0, 500);
        ch = setValue(m.hasFuel, m.fuel, a, v) || ch;
        v = 0;
        bool b = numField(text, "range", &v) && inRange(v, 0, 5000);
        ch = setValue(m.hasRange, m.range, b, v) || ch;
        break;
    }
    case kOutside: {
        bool ok = numField(text, "temperatureExterior", &v) && inRange(v, -60, 80);
        ch = setValue(m.hasOutside, m.outside, ok, v);
        break;
    }
    }
    return ch;
}

// Is anything shown for this property (used for diagnostics only).
inline bool propValid(const Metrics& m, int prop) {
    switch (prop) {
    case kEngineTemp: return m.hasCoolant || m.hasOil;
    case kRpm:        return m.hasRpm;
    case kSpeed:      return m.hasSpeed;
    case kGear:       return m.hasGear;
    case kFuel:       return m.hasFuel || m.hasRange;
    case kOutside:    return m.hasOutside;
    }
    return false;
}

// ---- display rows ---------------------------------------------------------------------------------------------------
// One label per row; the iDrive label is a single line cut at about 34 characters. Texts are UTF-8 here and are
// converted to UTF-16 for the HMI. Edit the words here to change the language (the escapes are written as separate
// literals so a following hex digit is not swallowed by an escape).
enum { kRowCount = 7, kRowLen = 72 };

#define ENG_DEG "\xC2\xB0"
#define ENG_TXT_COOLANT  "Woda"
#define ENG_TXT_OIL      "Olej"
#define ENG_TXT_RPM      "Obroty"
#define ENG_TXT_SPEED    "Pr\xC4\x99" "dko" "\xC5\x9B" "\xC4\x87"
#define ENG_TXT_GEAR     "Bieg"
#define ENG_TXT_FUEL     "Paliwo"
#define ENG_TXT_RANGE    "zasi\xC4\x99" "g"
#define ENG_TXT_OUTSIDE  "Temp. zewn."
#define ENG_UNIT_RPM     "obr/min"

inline int roundInt(double v) { return (int)(v >= 0 ? v + 0.5 : v - 0.5); }

// "N", "R", "P", "D1".. for the CDS driving.gear values 1, 2, 3, 4+ (see header comment); unknown 0 -> "--".
inline void gearText(int g, char* out, size_t n) {
    if (g == 1) snprintf(out, n, "N");
    else if (g == 2) snprintf(out, n, "R");
    else if (g == 3) snprintf(out, n, "P");
    else if (g >= 4) snprintf(out, n, "D%d", g - 3);
    else snprintf(out, n, "--");
}

// Fills the rows (UTF-8). Row order = label order in EngineData.xml.
inline void formatRows(const Metrics& m, char rows[kRowCount][kRowLen]) {
    char a[24], b[24];
    if (m.hasCoolant) snprintf(rows[0], kRowLen, ENG_TXT_COOLANT ": %d " ENG_DEG "C", roundInt(m.coolant));
    else snprintf(rows[0], kRowLen, ENG_TXT_COOLANT ": --");
    if (m.hasOil) snprintf(rows[1], kRowLen, ENG_TXT_OIL ": %d " ENG_DEG "C", roundInt(m.oil));
    else snprintf(rows[1], kRowLen, ENG_TXT_OIL ": --");
    if (m.hasRpm) snprintf(rows[2], kRowLen, ENG_TXT_RPM ": %d " ENG_UNIT_RPM, roundInt(m.rpm / 10.0) * 10);
    else snprintf(rows[2], kRowLen, ENG_TXT_RPM ": --");
    if (m.hasSpeed) snprintf(rows[3], kRowLen, ENG_TXT_SPEED ": %d km/h", roundInt(m.speed));
    else snprintf(rows[3], kRowLen, ENG_TXT_SPEED ": --");
    gearText(m.hasGear ? m.gear : 0, a, sizeof(a));
    snprintf(rows[4], kRowLen, ENG_TXT_GEAR ": %s", a);
    if (m.hasFuel) snprintf(a, sizeof(a), "%d l", roundInt(m.fuel)); else snprintf(a, sizeof(a), "--");
    if (m.hasRange) snprintf(b, sizeof(b), "%d km", roundInt(m.range)); else snprintf(b, sizeof(b), "--");
    snprintf(rows[5], kRowLen, ENG_TXT_FUEL ": %s, " ENG_TXT_RANGE ": %s", a, b);
    if (m.hasOutside) {
        snprintf(a, sizeof(a), "%.1f", m.outside);
        for (char* p = a; *p; ++p) if (*p == '.') *p = ',';   // Polish decimal comma
        snprintf(rows[6], kRowLen, ENG_TXT_OUTSIDE ": %s " ENG_DEG "C", a);
    } else {
        snprintf(rows[6], kRowLen, ENG_TXT_OUTSIDE ": --");
    }
}

} // namespace eng

#endif
