# NBT Evo – dane silnika na ekranie iDrive

Natywna aplikacja iDrive (ID5/ID6, QNX) pokazująca na żywo: **temperaturę wody, temperaturę oleju, obroty, prędkość,
bieg, poziom paliwa i zasięg oraz temperaturę zewnętrzną**. Pojawia się jako wiersz **„Dane silnika”** w menu
Media/Radio (zaraz po wierszu Bluetooth audio, w tym samym miejscu, w którym repozytorium Supra-iDrive6-Android-Auto
umieszcza „Android Auto” – to miejsce jest tam sprawdzone na aucie).

```
Woda: 92 °C
Olej: 104 °C
Obroty: 2450 obr/min
Prędkość: 87 km/h
Bieg: D3
Paliwo: 39 l, zasięg: 522 km
Temp. zewn.: 14,0 °C
```

Nie wymaga demona Android Auto ani niczego poza samą aplikacją. Identyfikatory są dobrane tak, żeby dało się ją
zainstalować obok NbtAad (niesprawdzone na aucie).

## Jak to działa

HmiMain3D przy starcie ładuje `Plugins/<Nazwa>/<Nazwa>.manifest` (framework `PluginApp`, ten sam co w BMW
SecurityMenu / ActiveSeating i w NbtAad). `EngineData.xml` opisuje wiersz menu i jeden ekran z siedmioma etykietami
(`label`), każda związana z modelem tekstowym `raDataModel`. `libEngineData.so`:

1. po ok. 1,5 s od startu subskrybuje właściwości **CDS** (Car Data Service) – tym samym wywołaniem
   `cdsAddPropertyChangedEventHandler`, którym NbtAad czyta `driving.gear` i `sensors.PDCStatus`;
2. trzyma ostatnie wartości; co 500 ms formatuje wiersze i przez `PluginApp::updateModel(model, tekst)` zapisuje
   **tylko te, które się zmieniły**;
3. gdy wartość jest niedostępna, błędna lub nierealna (np. `{"error":400}`), wiersz pokazuje `--`.

| Właściwość CDS | Przykładowa wartość | Wiersz |
|---|---|---|
| `engine.temperature` | `{"temperature":{"engine":19,"oil":18}}` | Woda, Olej |
| `engine.RPMSpeed` | `{"RPMSpeed":2453}` | Obroty (zaokrąglone do 10) |
| `driving.speedActual` | `{"speedActual":87}` | Prędkość |
| `driving.gear` | `{"gear":6}` | Bieg (1 N, 2 R, 3 P, 4+ D1…) |
| `sensors.fuel` | `{"fuel":{"range":522,"reserve":0,"tanklevel":39}}` | Paliwo, zasięg |
| `sensors.temperatureExterior` | `{"temperatureExterior":14.0}` | Temp. zewn. |

Nazwy i formaty: <https://bimmergestalt.github.io/BMWConnectedAnalysis/cds/>.

## Co jest sprawdzone, a co nie

**Sprawdzone (na hoście):**
- Logika parsowania i formatowania (`tests/test_cds_values.cpp`) oraz cały cykl życia aplikacji: ładowanie przez
  `pluginAppInfo`, `onInit`, subskrypcja sześciu właściwości, odświeżanie tylko zmienionych wierszy, błędne dane,
  `onFinalize` (`tests/app_host_test.cpp`, na atrapie frameworka). `make check`.
- Wszystkie 10 symboli `bmw::…`, których biblioteka używa, jest eksportowane przez `libAppFramework.A.so` /
  `libCoreFramework.A.so` z auta (sprawdzone `nm -D`), w tym `updateModel(unsigned, BasicString<unsigned short> const&)`.
- XML-e są poprawne składniowo, a identyfikatory modeli i etykiet są spójne z kodem.

**Niesprawdzone – do potwierdzenia na aucie (pierwsze uruchomienie to test):**
- **Kompilacja `qcc`.** W tym środowisku nie ma QNX SDP 6.5; kod jest C++98 i buduje się `g++ -std=gnu++98`, ale
  kompilacja docelowa (ARMv7, GCC 4.4) nie była uruchomiona.
- **`label` + `raDataModel` w XML PluginApp.** NbtAad używa w XML tylko przycisków, checkboksów, obrazów, listy
  Richtext i `textIdModel`; `label`/`raDataModel` pochodzą z dokumentacji RHMI (BimmerGestalt), a przeciążenie
  `updateModel` z tekstem istnieje we frameworku. To największe ryzyko: jeśli HmiMain3D odrzuci XML, wiersza po prostu
  nie będzie.
- **Czy auto publikuje `engine.*` / `sensors.*` aplikacjom pluginowym.** W NbtAad potwierdzone są `driving.gear`,
  `sensors.PDCStatus`, `driving.mode`, `vehicle.dayNightChangeOver`, `communication.callStatusN`.
- **Znaczenie `engine.temperature.engine`.** Dokumentacja CDS nie mówi wprost, że to płyn chłodzący; porównaj z
  zegarem w kokpicie.
- **Mapowanie biegów** (1 N, 2 R, 3 P, 4+ D) pochodzi z dokumentacji; w manualu wartości mogą być inne – surowa
  wartość trafia do logu.
- Dwie wartości, które wybrałem sam: `entryButtonFocusID="32751"` (NbtAad ma 32752) i `<id>211</id>` w manifeście
  (NbtAad ma 210), żeby aplikacje się nie zderzały. Jeśli menu się nie pojawia, zmień je.
- Kod języka `pl` w bazie tekstów (reszta języków jak w NbtAad). Przy innym języku iDrive tytuł wiersza może być pusty.

> Ingerencja w head unit odbywa się na Twoją odpowiedzialność (jak w NbtAad: po błędzie reflash robi tylko
> BMW/Toyota). Aplikacja niczego nie patchuje ani nie zapisuje na partycjach systemowych poza własnym folderem
> w `Plugins`.

## Wymagania

Takie same jak NbtAad: dostęp root/SSH do NBT Evo oraz jedno z kodowań **A4A, A4A_REMOTE_APPS, REMOTE_HMI,
TOUCH_COMMAND** (bez tego HmiMain3D nie ładuje PluginApps). Do budowy: QNX SDP 6.5 (`qcc`) oraz kopie
`libAppFramework.A.so` i `libCoreFramework.A.so` z Twojego head unit (`/opt/hmi/ID5/lib`).

## Budowanie

```
cp /sciezka/do/libAppFramework.A.so /sciezka/do/libCoreFramework.A.so lib/     # z auta
make            # libEngineData.so + EngineData_ALL_Texts.zip + EngineData_ALL_Images.zip
make check      # testy hostowe (zwykły g++, bez QNX)
```

(`make db` buduje same bazy – `python3` + Pillow. Można też użyć `build.sh` z repozytorium NbtAad w kontenerze QNX.)

## Instalacja

Na head unit, pliki z pendrive'a (`/mnt/umass…` – sprawdź `ls /mnt`):

```
mkdir -p /opt/hmi/ID5/data/ro/Plugins/EngineData
cp /mnt/umassXXXX/libEngineData.so /opt/hmi/ID5/data/ro/Plugins/EngineData/
cp /mnt/umassXXXX/EngineData.manifest /opt/hmi/ID5/data/ro/Plugins/EngineData/
cp /mnt/umassXXXX/EngineData.xml /opt/hmi/ID5/data/ro/Plugins/EngineData/
cp /mnt/umassXXXX/EngineData_ALL_Texts.zip /opt/hmi/ID5/data/ro/Plugins/EngineData/
cp /mnt/umassXXXX/EngineData_ALL_Images.zip /opt/hmi/ID5/data/ro/Plugins/EngineData/
```

Jeśli kopiowanie kończy się „read-only file system”, przemontuj cel do zapisu tak samo, jak przy NbtAad
(`install.md`). Potem przytrzymaj przycisk zasilania ok. 30 s (restart). W **Media/Radio** powinien być wiersz
**Dane silnika**.

**Aktualizacja:** nie nadpisuj działającej biblioteki – `mv libEngineData.so libEngineData.so.old`, skopiuj nową,
zrestartuj. **Odinstalowanie:** usuń folder `Plugins/EngineData` i zrestartuj. **Wyłącznik awaryjny bez usuwania:**
`mkdir -p /var/opt/engine && touch /var/opt/engine/app.disable` (`/opt/engine/app.disable` też działa, ale `/opt`
bywa tylko do odczytu).

## Diagnostyka

Aplikacja loguje przez `slog` (kod 0x4e47; `sloginfo | grep EngineData`). Łatwiej: **utwórz na komputerze pusty plik
`nbt-engine-data.log` na pendrivie** i włóż go do auta – log będzie dopisywany tam (limit 4 MB).

Każda właściwość CDS jest zapisana w logu w surowej postaci przy pierwszych 5 odczytach, np.
`CDS engine.temperature = '{"temperature":{"engine":92,"oil":104}}' -> ok`. To dokładnie to, czego potrzeba, żeby
sprawdzić, co Twoje auto naprawdę wysyła.

**Brak danych (wszędzie `--`):** wiersz menu jest, ale wartości nie przychodzą – 25 s po starcie w logu pojawi się
komunikat `no CDS value arrived…`. Znaczy to, że auto nie publikuje tych właściwości aplikacjom pluginowym albo
subskrypcja się nie powiodła. Prześlij fragment logu – można wtedy spróbować innych nazw właściwości lub
odczytu z innego źródła.

**Brak wiersza w menu:** sprawdź kodowanie (A4A itd.), obecność wszystkich 5 plików, ewentualny konflikt
`entryButtonFocusID` / `<id>` w manifeście oraz log HMI. Jeśli XML jest odrzucany, zacznij od zdjęcia `label`
z `EngineData.xml` i sprawdzenia samego wiersza.

## Dostosowanie

- **Teksty i język wierszy:** makra `ENG_TXT_*` na górze sekcji „display rows” w `src/cds_values.hpp`
  (UTF-8; escape'y zapisane jako osobne literały).
- **Nowa wartość:** dodaj pozycję w `enum Prop` i `propName`, regułę w `apply()`, wiersz w `formatRows`, kolejny
  `raDataModel` + `label` w `EngineData.xml` (ID 28 / 38…), zwiększ `kRowCount`, `kPropCount` i tablicę `kStubs`.
- **Częstotliwość odświeżania:** `kRenderMs` w `src/engine_data.cpp`.

## Układ projektu

```
src/cds_values.hpp     parsowanie CDS i formatowanie wierszy (czysta logika, testowana)
src/engine_data.cpp    aplikacja: PluginApp, subskrypcja CDS, odświeżanie etykiet
EngineData.xml         wiersz menu + ekran z 7 etykietami
EngineData.manifest    manifest dla CAppManager
make_db.py             bazy tekstów i obrazów (+ ikona 63x54)
tests/                 testy hostowe i atrapa frameworka
```

## Licencja i pochodzenie

Wzorce ABI frameworka (konstruktor `PluginApp`, własna vtable, `Runnable`, `pluginAppInfo`) i sposób czytania CDS
pochodzą z NbtAad (<https://github.com/Sq00ky/Supra-iDrive6-Android-Auto>, PolyForm Noncommercial 1.0.0), więc ten
projekt jest udostępniony na tej samej licencji: tylko do użytku niekomercyjnego. Biblioteki BMW
(`libAppFramework`, `libCoreFramework`) nie są częścią projektu.
