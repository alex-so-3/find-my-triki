# find-my-triki

Alternatywny firmware dla kontrolera **Triki z Żabki** (nRF52810 + LSM6DSL + MX25R8035F). Zamienia
go w lokalizator widoczny jednocześnie w dwóch sieciach:

- **Apple Find My** – jako tag [OpenHaystack](https://github.com/seemoo-lab/openhaystack),
- **Google Find Hub** – jako tracker ze stałym identyfikatorem (EID), jak
  [Everytag](https://github.com/vasimv/Everytag).

Do tego: dzwonienie diodą (standard DULT), konfiguracja przez BLE, bezpieczne OTA i opcjonalny
tryb kontrolera do gier.

**Oszczędzanie baterii akcelerometrem.** Większość tagów nadaje cały czas w tym samym rytmie. Ten
używa wbudowanego w Triki akcelerometru: w ruchu każda sieć dostaje ramkę co 2 s, a po 10 minutach
bez ruchu tylko co 20 s. Pierwsze poruszenie od razu budzi tag (czujnik pracuje przy 1,6 Hz i
bierze ~4,5 µA). Połączenie z tagiem, np. dzwonienie z telefonu, też liczy się jak ruch, więc
schowany tag przy szukaniu zaczyna nadawać często. W uśpieniu całość pobiera ~7 µA.

Do pracy z tagiem:
- [OpenTagViewer-triki](https://github.com/alex-so-3/OpenTagViewer-triki) – aplikacja na Androida
  (lokalizacje z obu sieci, dzwonienie, ustawienia, aktualizacja firmware),
- [googlefind-service](https://github.com/alex-so-3/googlefind-service) – usługa (Docker) dla sieci
  Google; bez niej tag po kilku dniach znika z Find Hub,
- `tools/` – to samo z komputera.

## Bateria

Szacunek z [pomiarów](docs/measurements.md) (~6,8 µA w uśpieniu, ~22 µA średnio w ruchu, ~10 µA w
bezruchu), dla CR2032 z ~200 mAh użytecznej pojemności, przy domyślnych ustawieniach:

| Użycie | Średni prąd | Czas pracy |
|---|---|---|
| głównie leży (np. w szufladzie, portfelu w domu) | ~10 µA | **~2,3 roku** |
| mieszane: ~6 h dziennie w ruchu (torba, klucze) | ~13 µA | **~1,8 roku** |
| cały czas w ruchu | ~22 µA | **~1 rok** |

Nie uwzględnia samorozładowania baterii, niskich temperatur ani częstego dzwonienia (dioda ~2 mA).
Dłuższy `period` (`-d 4`) wyraźnie wydłuża czas pracy w ruchu – patrz [docs/power.md](docs/power.md).

## Dokumentacja

| | |
|---|---|
| [docs/flashing.md](docs/flashing.md) | pinout, podłączenie sondy SWD, pierwsze wgranie |
| [docs/setup.md](docs/setup.md) | klucze Apple/Google, konfiguracja, OTA |
| [docs/usage.md](docs/usage.md) | zachowanie tagu, przycisk, dioda |
| [docs/game-mode.md](docs/game-mode.md) | tryb kontrolera do gier |
| [docs/power.md](docs/power.md) | oszczędzanie baterii, debugowanie poboru |
| [docs/measurements.md](docs/measurements.md) | pomiary Power Profilerem |

## Szybki start

Wymagania: nRF5 SDK 17.1.0 w `~/nrf5/nRF5_SDK_17.1.0_ddde560` (ze zbudowanym micro-ecc), Arm GNU
Toolchain w `~/nrf5/`, `nrfutil` + `nrfutil install nrf5sdk-tools`, OpenOCD z sondą SWD (sprawdzone:
J-Link), `pip install bleak cryptography`. Ścieżki: `make SDK_ROOT=... GNU_INSTALL_ROOT=.../bin/`.

```sh
make                                          # build/triki_full.hex
make recover flash OPENOCD_IF=interface/jlink.cfg   # pierwsze wgranie – kasuje firmware Żabki
tools/gen_apple_key.py -n TAG                 # klucz Apple -> keys/TAG.keys
tools/triki_config.py -a abcdefgh -K keys/TAG.keys -k keys/TAG_keyfile -f <EID> -n NoweHaslo
make dfu && tools/triki_dfu.py build/triki_app_dfu.zip -K keys/TAG.keys   # kolejne wersje przez OTA
```

Przy pierwszym `make` powstaje `keys/dfu_private.pem` – **klucz podpisu OTA, zrób kopię**. Katalog
`keys/` nie trafia do gita.

Opcje builda (po zmianie `make clean`):

| Opcja | |
|---|---|
| `APP_VERSION=n` | wersja do OTA (musi być wyższa niż wgrana) |
| `DCDC=0` | bez przetwornicy DC/DC – tylko dla płytek bez cewek przy pinie DCC (Triki je ma) |
| `LFCLK=RC` | płytka bez kwarcu 32,768 kHz |
| `IMU_INT2=1`, `LED_ACTIVE_HIGH=1` | inne podłączenie przerwania IMU / diody |
| `DEBUG_CHR=1`, `WDT=0` | tylko do debugowania, patrz [docs/power.md](docs/power.md) |

## Struktura

```
app/      aplikacja (src/, config/, armgcc/)     tools/  konfiguracja, OTA, klucze
bootloader/  secure bootloader BLE                docs/   dokumentacja i zdjęcia
board/    pinout Triki, APPROTECT                 keys/   klucze (poza gitem)
```

## Licencja

MIT (`LICENSE`). Pliki z nRF5 SDK (bootloader, `sdk_config.h`, skrypty linkera) zachowują licencję
Nordic Semiconductor – patrz `NOTICE`.
