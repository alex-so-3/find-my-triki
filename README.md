# find-my-triki

Alternatywny firmware dla kontrolera **Triki z Żabki** (nRF52810 + LSM6DSL + MX25R8035F). Zamienia
go w lokalizator widoczny jednocześnie w dwóch sieciach:

- **Apple Find My** – jako tag [OpenHaystack](https://github.com/seemoo-lab/openhaystack),
- **Google Find Hub** – jako tracker ze stałym identyfikatorem (EID), jak
  [Everytag](https://github.com/vasimv/Everytag).

Do tego: dzwonienie diodą (standard DULT), konfiguracja przez BLE, bezpieczne OTA, pobór ~7 µA w
uśpieniu i opcjonalny tryb kontrolera do gier.

Do pracy z tagiem:
- [OpenTagViewer-triki](https://github.com/alex-so-3/OpenTagViewer-triki) – aplikacja na Androida
  (lokalizacje z obu sieci, dzwonienie, ustawienia, aktualizacja firmware),
- [googlefind-service](https://github.com/alex-so-3/googlefind-service) – usługa (Docker) dla sieci
  Google; bez niej tag po kilku dniach znika z Find Hub,
- `tools/` – to samo z komputera.

## Dokumentacja

| | |
|---|---|
| [docs/flashing.md](docs/flashing.md) | pinout, podłączenie sondy SWD, pierwsze wgranie |
| [docs/setup.md](docs/setup.md) | klucze Apple/Google, konfiguracja, OTA |
| [docs/usage.md](docs/usage.md) | zachowanie tagu, przycisk, dioda |
| [docs/game-mode.md](docs/game-mode.md) | tryb kontrolera do gier |
| [docs/power.md](docs/power.md) | zmierzony pobór prądu, debugowanie |

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
