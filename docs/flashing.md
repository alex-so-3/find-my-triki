# Pinout i pierwsze wgranie

Pierwszy raz firmware wgrywa się przez SWD, każda kolejna wersja może przyjść przez OTA
([setup.md](setup.md#ota)).

## Pola testowe SWD

![Pola testowe SWD na płytce Triki](pinout.jpg)

*Zdjęcie: [Piwencjusz/zabka-triki-hardware](https://github.com/Piwencjusz/zabka-triki-hardware)
(`photos/TIKIpinout.jpg`), zmniejszone.*

Wszystkie pola są po stronie z nRF52810; bateria zostaje w koszyku pod spodem i zasila płytkę.

| Pole | Gdzie (na zdjęciu) | Do czego |
|---|---|---|
| 3V3 | duży cynowany styk u góry (styk baterii) | napięcie odniesienia sondy (VTref), **nie zasilanie** |
| GND | pole na prawo od przycisku, wyżej | masa |
| nRESET | pole obok GND, bliżej przycisku | reset (P0.21), pomaga przy `recover` |
| SWDIO | dolne pole po prawej | dane SWD |
| SWCLK | pole obok SWDIO, bliżej dolnego styku baterii | zegar SWD |

Pola są małe i bez otworów – najwygodniej sprężynowe igły na statywach (PCBite albo podobne):

![Wgrywanie sondami PCBite](flashing-setup.jpg)

## Sonda

J-Link (złącze 20-pin): 1 VTref → 3V3, 4 GND → GND, 7 SWDIO, 9 SWCLK, 15 RESET → nRESET.
**Pinu 19 (5 V) nie podłączać.** Sonda CMSIS-DAP (np. Pico z `debugprobe`) – te same sygnały; bez
wejścia VTref podłącza się tylko GND i sygnały.

## Wgrywanie

1. `make`
2. Czy sonda widzi chip: `openocd -f interface/jlink.cfg -c "transport select swd" -f target/nrf52.cfg -c "init; exit"`
   → `SWD DPIDR 0x2ba01477` (na oryginalnym firmware może zgłosić zablokowany chip – normalne).
3. Odblokowanie – **kasuje na zawsze oryginalny firmware Żabki**:
   `make recover OPENOCD_IF=interface/jlink.cfg`
4. Wgranie całości: `make flash OPENOCD_IF=interface/jlink.cfg` (bez `OPENOCD_IF` – CMSIS-DAP).
5. Dioda mignie raz długo (5 razy krótko = brak kluczy, wgraj je przez BLE – [setup.md](setup.md)).

Później przez SWD można wgrać samą aplikację, bez kasowania kluczy: `make flash-app OPENOCD_IF=...`.
`cannot read IDR` / `Error connecting DP` = igła nie trzyma pola, dociśnij sondy.

## APPROTECT

Nowsze nRF52810 mają „wzmocnioną” blokadę debugowania: SWD jest zablokowane po każdym resecie, chyba
że UICR.APPROTECT = 0x5A **i** firmware przy starcie wpisze 0x5A do APPROTECT.DISABLE. Hex bootloadera
zawiera wpis UICR, a bootloader i aplikacja odblokowują SWD (`board/triki_approtect.h`), więc po
wgraniu tego firmware'u `recover` nie jest już potrzebny.

## Piny

Z `board/triki_board.h`, sprawdzone na prawdziwym Triki. Schemat w zabka-triki-hardware jest
odtworzony ze ścieżek i w dwóch miejscach się nie zgadza: P0.04 i zamienione SCK/MOSI pamięci NOR
(sprawdzone odczytem jej identyfikatora JEDEC `C2 28 14`).

| Pin | Funkcja |
|---|---|
| P0.00, P0.01 | kwarc 32,768 kHz |
| P0.04 | **nie ruszać** – to nie SA0 czujnika (SA0 jest na płytce w stanie niskim, adres 0x6A); stan niski na P0.04 kosztuje ~60 µA |
| P0.05 / P0.06 | LSM6DSL SDA / SCL |
| P0.09 | LSM6DSL INT1 (wybudzenie ruchem) |
| P0.10 | LSM6DSL INT2 (nieużywane) |
| P0.12 | LSM6DSL CS (stan wysoki → I²C) |
| P0.14, P0.15, P0.18, P0.20 | MX25R8035F: CS, MISO, SCK, MOSI |
| P0.21 | nRESET |
| P0.25 | przycisk (aktywny stanem niskim) |
| P0.28 | LED (aktywna stanem niskim) |

Przy pinie DCC są cewki 10 µH + 15 nH, więc przetwornica DC/DC działa (domyślnie włączona).
