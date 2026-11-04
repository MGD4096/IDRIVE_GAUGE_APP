# Initial project created with Claude, not yet tested
[Polish version of readme](README_PL.md)

# NBT Evo – engine data on the iDrive display

A native iDrive app (ID5/ID6, QNX) that shows live:
temperature of coolant, oil temperature, engine RPM, speed, gear,
fuel level and range, and outside temperature. It appears as a
row titled “Engine Data” in the Media/Radio menu
(immediately after the Bluetooth audio row, in the same place where
the Supra-iDrive6-Android-Auto repository places “Android Auto” – this
place has been verified in the car).

```
Water: 92 °C
Oil: 104 °C
RPM: 2450 rpm
Speed: 87 km/h
Gear: D3
Fuel: 39 l, range: 522 km
Outside temp.: 14.0 °C
```

It does not require an Android Auto daemon or anything beyond the app itself.
The identifiers were chosen so it can be installed alongside NbtAad
(not verified on the car).

## How it works

HmiMain3D loads `Plugins/<Name>/<Name>.manifest` at startup
(framework `PluginApp`, the same one used by BMW SecurityMenu /
ActiveSeating and by NbtAad). `EngineData.xml` describes the menu row and
one screen with seven labels (`label`), each linked to a text model
`raDataModel`. `libEngineData.so`:

1. about 1.5 s after startup, subscribes to **CDS** (Car Data Service)
   properties – using the same `cdsAddPropertyChangedEventHandler`
   call that NbtAad uses to read `driving.gear` and `sensors.PDCStatus`;
2. keeps the latest values; every 500 ms it formats the rows and writes
   via `PluginApp::updateModel(model, text)` only the rows that changed;
3. if a value is unavailable, invalid, or unrealistic (for example:
   `{"error":400}`), the row displays `--`.

| CDS property | Example value | Row |
|---|---|---|
| `engine.temperature` | `{"temperature":{"engine":19,"oil":18}}` | Water, Oil |
| `engine.RPMSpeed` | `{"RPMSpeed":2453}` | RPM (rounded to 10) |
| `driving.speedActual` | `{"speedActual":87}` | Speed |
| `driving.gear` | `{"gear":6}` | Gear (1 N, 2 R, 3 P, 4+ D1…) |
| `sensors.fuel` | `{"fuel":{"range":522,"reserve":0,"tanklevel":39}}` | Fuel, range |
| `sensors.temperatureExterior` | `{"temperatureExterior":14.0}` | Outside temp. |

Names and formats: https://bimmergestalt.github.io/BMWConnectedAnalysis/cds/

## What is verified and what is not

**Verified (on host):**
- Parsing and formatting logic (`tests/test_cds_values.cpp`) and the full
  lifecycle of the app: loading through `pluginAppInfo`, `onInit`,
  subscription to six properties, refreshing only changed rows,
  invalid data, and `onFinalize` (`tests/app_host_test.cpp`, using a
  framework mock). `make check`.
- All 10 symbols `bmw::…` used by the library are exported by
  `libAppFramework.A.so` / `libCoreFramework.A.so` from the car
  (verified with `nm -D`), including
  `updateModel(unsigned, BasicString<unsigned short> const&)`.
- XML is syntactically valid, and model and label IDs are consistent with the code.

**Not yet verified – to be confirmed in the car (first boot is the test):**
- **`qcc` compilation.** This environment does not have QNX SDP 6.5;
  the code is C++98 and builds with `g++ -std=gnu++98`, but the actual
  target build (ARMv7, GCC 4.4) was not run.
- **`label` + `raDataModel` in the PluginApp XML.** NbtAad only uses
  XML for buttons, checkboxes, images, Richtext lists, and `textIdModel`;
  `label`/`raDataModel` come from RHMI documentation (BimmerGestalt),
  and the `updateModel` overload with text exists in the framework.
  This is the biggest risk: if HmiMain3D rejects the XML, the row simply
  will not appear.
- **Whether the car publishes `engine.*` / `sensors.*` to plugin apps.**
  In NbtAad, `driving.gear`, `sensors.PDCStatus`, `driving.mode`,
  `vehicle.dayNightChangeOver`, and `communication.callStatusN` are confirmed.
- **Meaning of `engine.temperature.engine`.** The CDS documentation does not
  explicitly say it is coolant; compare it with the gauge in the dashboard.
- **Gear mapping** (1 N, 2 R, 3 P, 4+ D) comes from the documentation;
  in the manual the values may differ – raw values are logged.
- Two values I chose myself: `entryButtonFocusID="32751"` (NbtAad has 32752)
  and `<id>211</id>` in the manifest (NbtAad has 210), so the apps do not
  conflict. If the menu does not appear, change them.
- The `pl` language code in the text database (other languages follow
  NbtAad). With a different iDrive language, the row title may be empty.

> Any modification to the head unit is done at your own responsibility
> (like NbtAad: a failed reflash only affects BMW/Toyota). The app does not
> patch or write to system partitions outside its own folder in `Plugins`.

## Requirements

Same as NbtAad: root/SSH access to NBT Evo and one of the coding options
**A4A, A4A_REMOTE_APPS, REMOTE_HMI, TOUCH_COMMAND** (without this,
HmiMain3D does not load PluginApps). For building: QNX SDP 6.5 (`qcc`) and
copies of `libAppFramework.A.so` and `libCoreFramework.A.so` from your head
unit (`/opt/hmi/ID5/lib`).

## Building

```
cp /path/to/libAppFramework.A.so /path/to/libCoreFramework.A.so lib/     # from the car
make            # libEngineData.so + EngineData_ALL_Texts.zip + EngineData_ALL_Images.zip
make check      # host tests (regular g++, no QNX)
```

(`make db` builds the databases only – `python3` + Pillow. You can also use
`build.sh` from the NbtAad repository in a QNX container.)

## Installation

On the head unit, copy the files from a USB stick (`/mnt/umass…` –
check `ls /mnt`):

```
mkdir -p /opt/hmi/ID5/data/ro/Plugins/EngineData
cp /mnt/umassXXXX/libEngineData.so /opt/hmi/ID5/data/ro/Plugins/EngineData/
cp /mnt/umassXXXX/EngineData.manifest /opt/hmi/ID5/data/ro/Plugins/EngineData/
cp /mnt/umassXXXX/EngineData.xml /opt/hmi/ID5/data/ro/Plugins/EngineData/
cp /mnt/umassXXXX/EngineData_ALL_Texts.zip /opt/hmi/ID5/data/ro/Plugins/EngineData/
cp /mnt/umassXXXX/EngineData_ALL_Images.zip /opt/hmi/ID5/data/ro/Plugins/EngineData/
```

If copying ends with “read-only file system”, remount the destination
as writable in the same way as with NbtAad (`install.md`). Then hold the power
button for about 30 s (restart). In **Media/Radio**, the row
**Engine Data** should appear.

**Update:** do not overwrite a working library – move it:
`mv libEngineData.so libEngineData.so.old`, copy the new one,
then restart. **Uninstall:** delete the `Plugins/EngineData` folder and
restart. **Emergency disable without deleting:**  
`mkdir -p /var/opt/engine && touch /var/opt/engine/app.disable`
(`/opt/engine/app.disable` also works, but `/opt` is often read-only).

## Diagnostics

The app logs via `slog` (code 0x4e47; `sloginfo | grep EngineData`).
Easier: **create an empty file named `nbt-engine-data.log` on your computer
and place it on the USB stick**; logs will be appended there (limit 4 MB).

Each CDS property is logged in raw form during the first 5 reads, for example:
`CDS engine.temperature = '{"temperature":{"engine":92,"oil":104}}' -> ok`.
This is exactly what you need to check what your car is really sending.

**No data (all rows show `--`):** the menu row is present, but values are
not arriving – 25 s after startup, a log message
`no CDS value arrived…` will appear. This means the car is not publishing
these properties to plugin apps, or the subscription failed. Send the
relevant log snippet – then we can try different property names or read
from another source.

**No menu row:** check the encoding (A4A etc.), presence of all 5 files,
possible conflict in `entryButtonFocusID` / `<id>` in the manifest, and the
HMI log. If the XML is rejected, start by removing `label` from
`EngineData.xml` and verify the menu row itself.

## Customization

- **Text and language for row labels:** macros `ENG_TXT_*` at the top of
  the “display rows” section in `src/cds_values.hpp`
  (UTF-8; escapes are stored as separate literals).
- **New value:** add an entry in `enum Prop` and `propName`, a rule in
  `apply()`, a row in `formatRows`, another `raDataModel` + `label` in
  `EngineData.xml` (ID 28 / 38…), and increase `kRowCount`,
  `kPropCount`, and the `kStubs` array.
- **Refresh frequency:** `kRenderMs` in `src/engine_data.cpp`.

## Project layout

```
src/cds_values.hpp     CDS parsing and row formatting logic (pure logic, tested)
src/engine_data.cpp    app: PluginApp, CDS subscription, label refresh
EngineData.xml         menu row + screen with 7 labels
EngineData.manifest    manifest for CAppManager
make_db.py             text and image databases (+ 63x54 icon)
tests/                 host tests and framework mock
```

## License and origin

The ABI framework templates (constructor `PluginApp`, custom vtable,
`Runnable`, `pluginAppInfo`) and the CDS access method originate from
NbtAad (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto, PolyForm
Noncommercial 1.0.0), so this project is released under the same license:
for non-commercial use only. BMW libraries (`libAppFramework`,
`libCoreFramework`) are not part of this project.

If you want, I can also turn this into a polished README.md file ready to paste directly into the repository.
