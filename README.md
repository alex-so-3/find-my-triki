# find-my-triki

Alternatywny firmware dla kontrolera **Triki z Żabki** (nRF52810 + LSM6DSL + MX25R8035F), który
zamienia go w lokalizator widoczny jednocześnie w dwóch sieciach:

- **Apple Find My** – jako tag [OpenHaystack](https://github.com/seemoo-lab/openhaystack) ze stałym
  kluczem,
- **Google Find Hub** – jako tracker ze stałym, wcześniej wyliczonym identyfikatorem (EID), jak w
  [Everytag](https://github.com/vasimv/Everytag).

Opcjonalnie działa też dalej jako kontroler ruchu (tryb gry – patrz niżej).

Firmware jest oparty na nRF5 SDK 17.1.0 i SoftDevice S112 7.2.0. Bootloader i aplikacja
współdzielą stos BLE, więc aplikacja zajmuje ~21 KB i mieści się bezpieczne OTA dual-bank (nowa
wersja wgrywa się obok starej i zastępuje ją dopiero po sprawdzeniu).

Sprzęt opisuje [zabka-triki-hardware](https://github.com/Piwencjusz/zabka-triki-hardware). Pinout
używany przez firmware został sprawdzony na prawdziwym urządzeniu: kwarc 32,768 kHz, LED na P0.28
(aktywna stanem niskim), przycisk na P0.25, LSM6DSL na I²C P0.05/P0.06 z przerwaniem na P0.09.

## Do czego służy w praktyce

- Lokalizacje z sieci Apple i Google oraz dzwonienie, konfiguracja i aktualizacja firmware z
  telefonu: [OpenTagViewer-triki](https://github.com/alex-so-3/OpenTagViewer-triki) – fork [OpenTagViewer](https://github.com/parawanderer/OpenTagViewer).
- Lokalizacje Google i codzienne odświeżanie identyfikatorów: usługa [googlefind-service](https://github.com/alex-so-3/googlefind-service)
  (Docker).
- Wszystko da się też zrobić z komputera narzędziem `tools/triki_config.py`.

## Zachowanie

| Stan | Co robi tag |
|---|---|
| Ruch | co 0,5 s × `period` jedno zdarzenie reklamowe, na zmianę Apple / Google (domyślnie każda sieć co 2 s) |
| Bezruch (po `still_timeout`, domyślnie 10 min) | co 5 s × `period` (domyślnie każda sieć co 20 s) |
| Przebudzenie | IMU (wake-up, próg domyślnie ~63 mg) zgłasza przerwanie → od razu pakiet i powrót do trybu „ruch” |
| Połączenie | ramka Apple jest łączliwa: aplikacja może w każdej chwili połączyć się z tagiem, żeby nim zadzwonić, zmienić ustawienia albo wgrać firmware (bez poprawnego hasła połączenie jest zrywane po 20 s) |
| Tryb konfiguracji | reklama „TrikiTag” z możliwością połączenia przez 120 s (dla narzędzi szukających tagu po nazwie) |
| Tryb gry | patrz niżej |
| DFU | bootloader „TrikiTagDFU”, 2 min bez połączenia → powrót do aplikacji |

Przycisk (P0.25):

- **krótko**: stan baterii na LED (3 mignięcia = pełna, 2 = OK, 1 = słaba). W trybie konfiguracji
  krótkie naciśnięcie go kończy.
- **podwójne kliknięcie**: tryb gry (2 mignięcia; 3 szybkie = tryb gry nie jest skonfigurowany).
- **3 s** (jedno mignięcie przy trzymaniu): tryb konfiguracji.
- **10 s** (seria szybkich mignięć): restart do bootloadera DFU.
- **trzymany przy włożeniu baterii / resecie**: bootloader DFU (ratunek, gdy aplikacja nie działa).

LED (P0.28) – zamiast brzęczyka:

- start: jedno długie mignięcie; 5 szybkich = brak kluczy; 2 wolne = nie wykryto IMU,
- „odtwórz dźwięk” (standard DULT, np. z aplikacji): miga przez 10 s albo do polecenia stop,
- po połączeniu z poprawnym hasłem: jedno dłuższe mignięcie; w trybie konfiguracji krótkie co sekundę,
- zapis ustawień: 3 szybkie mignięcia, potem restart,
- bootloader DFU: świeci ciągle, gaśnie po połączeniu telefonu.

Dzwonienie z samej aplikacji Google Find Hub nie działa: pełny protokół FMDN (Fast Pair) nie
mieści się w nRF52810 (~187 KB flash / ~33 KB RAM przy 192/24 KB), dlatego tag nadaje tylko
statyczny EID. Dzwoni się nim przez standard DULT (np. z forka OpenTagViewer).

## Wymagania

- nRF5 SDK 17.1.0 w `~/nrf5/nRF5_SDK_17.1.0_ddde560`
  (<https://www.nordicsemi.com/Products/Development-software/nRF5-SDK>),
  ze zbudowanym micro-ecc: `cd $SDK/external/micro-ecc && git clone https://github.com/kmackay/micro-ecc && make -C nrf52nf_armgcc/armgcc GNU_INSTALL_ROOT=...`
- Arm GNU Toolchain (arm-none-eabi) w `~/nrf5/arm-gnu-toolchain-13.3.rel1-darwin-arm64-arm-none-eabi`
- `nrfutil` + `nrfutil install nrf5sdk-tools` (klucze, ustawienia bootloadera, paczki DFU)
- OpenOCD i programator SWD – sprawdzone z J-Linkiem; inne sondy obsługiwane przez OpenOCD
  (np. CMSIS-DAP) powinny działać
- `pip install bleak cryptography` dla narzędzi w `tools/`

Ścieżki można nadpisać: `make SDK_ROOT=... GNU_INSTALL_ROOT=.../bin/`.

## Budowanie

```sh
make                 # build/triki_full.hex = SoftDevice + bootloader + aplikacja + ustawienia
make dfu             # build/triki_app_dfu.zip – podpisana paczka OTA
```

Przy pierwszym `make` powstaje `keys/dfu_private.pem` – **klucz podpisu OTA. Zrób kopię**,
bez niego nie wgrasz już nic przez OTA (zostanie tylko SWD). Katalog `keys/` nie trafia do gita.

Opcje (po zmianie `make clean`):

| Opcja | Kiedy |
|---|---|
| `APP_VERSION=n` | wersja aplikacji do DFU (musi być wyższa niż wgrana) |
| `DCDC=1` | mniejszy pobór prądu, ale **tylko jeśli przy pinie DCC jest cewka** – bez niej chip nie wystartuje. Na Triki niesprawdzone, domyślnie wyłączone |
| `LFCLK=RC` | płytka bez kwarcu 32,768 kHz (Triki go ma) |
| `IMU_INT2=1` | przerwanie IMU z INT2 na P0.10 zamiast INT1 na P0.09 (Triki: INT1) |
| `LED_ACTIVE_HIGH=1` | płytka z LED aktywną stanem wysokim (Triki: niskim) |

Bootloader zawsze używa oscylatora RC, więc OTA działa niezależnie od kwarcu.

## Klucze

- **Apple:** `tools/gen_apple_key.py -n NAZWA` → `keys/NAZWA.keys` (klucz prywatny – zrób kopię,
  bez niego nie odczytasz lokalizacji) i `keys/NAZWA_keyfile` (dla firmware). Pasuje też
  `*_keyfile` z `generate_keys.py` z [macless-haystack](https://github.com/dchristl/macless-haystack)
  (używany jest pierwszy klucz). Plik `.keys` importuje się do forka OpenTagViewer.
- **Google:** tracker rejestruje się na koncie narzędziem
  [GoogleFindMyTools](https://github.com/leonboe1/GoogleFindMyTools): `python main.py` → zaloguj
  się → `r` → skopiuj „Advertisement Key” (20-bajtowy EID).

### Google: odświeżanie co < 4 dni

Stały EID działa tylko, jeśli właściciel co kilka dni wyśle Google listę identyfikatorów na
najbliższe ~4 dni (prawdziwy tracker robi to przez telefon z Androidem). Robi to codziennie usługa
[googlefind-service](https://github.com/alex-so-3/googlefind-service). Bez niej tag po kilku dniach znika z sieci Google.

### Wgranie kluczy

W firmware (opcjonalnie, plik `keys/triki_keys.h` poza gitem):

```sh
tools/make_default_keys.py -k keys/NAZWA_keyfile -f 00112233445566778899aabbccddeeff00112233
make clean && make
```

Albo przez BLE po wgraniu. `-K` szuka tagu po jego ramce Apple (tag w bezruchu nadaje rzadko –
porusz nim, żeby szybciej go znaleźć); bez `-K` narzędzie szuka tagu po nazwie w trybie
konfiguracji (przycisk 3 s):

```sh
tools/triki_config.py -a abcdefgh -K keys/NAZWA.keys -k keys/NAZWA_keyfile -f 00112233... -n NoweHas1
tools/triki_config.py -a NoweHas1 -K keys/NAZWA.keys --info        # bateria, wersja, ustawienia
tools/triki_config.py -a NoweHas1 -K keys/NAZWA.keys --ring        # miganie LED
```

Domyślne hasło to `abcdefgh` – zmień je (`-n`), bo tag przyjmuje połączenia w każdej chwili.
Usługa konfiguracyjna jest zgodna z Everytag, więc działa też jego `conn_beacon.py` (z tą różnicą,
że `-l` ustawia czas do bezruchu, a `-m` próg ruchu w mg).

## Pierwsze wgranie

1. Podłącz sondę SWD do pól testowych: SWDIO, SWCLK, GND, zasilanie odniesienia (VTref ↔ 3V3) i
   najlepiej RESET. Styki lubią się rozłączać – błąd `cannot read IDR` zwykle oznacza, że trzeba
   docisnąć sondę.
2. Odblokuj chip – **kasuje na zawsze oryginalny firmware Żabki** (nie da się go odczytać ani
   odtworzyć):
   ```sh
   make recover OPENOCD_IF=interface/jlink.cfg
   ```
3. Wgraj: `make flash OPENOCD_IF=interface/jlink.cfg` (bez `OPENOCD_IF` – `interface/cmsis-dap.cfg`).
4. LED powinna mignąć raz długo (albo 5 razy krótko, jeśli nie ma jeszcze kluczy).
   SWD zostaje otwarte (patrz APPROTECT).

Później tylko aplikacja (zachowuje klucze i ustawienia): `make flash-app OPENOCD_IF=...`.
Wariant z nrfjprog: `PROBE=jlink make flash` (niesprawdzony).

## OTA

1. `make dfu APP_VERSION=<wyższa niż obecna>`
2. Najprościej: fork OpenTagViewer → ekran tagu → *Device settings* → aktualizacja firmware →
   `build/triki_app_dfu.zip`. Aplikacja sama przełącza tag do bootloadera (standardowe Buttonless
   DFU) i wysyła paczkę.
3. Ręcznie: tag do bootloadera (przycisk 10 s albo `tools/triki_config.py ... --dfu`), potem
   nRF Connect / nRF Device Firmware Update → „TrikiTagDFU” → paczka.

Bootloader przyjmuje tylko paczki podpisane Twoim kluczem. Stara aplikacja zostaje w pamięci, dopóki
nowa nie zostanie w całości odebrana i sprawdzona; przerwany transfer nic nie psuje.

## Tryb gry

Triki jest kontrolerem ruchu. Firmware może – opcjonalnie – zachowywać się jak oryginalny
kontroler, tak żeby współpracować z oprogramowaniem, które go używa (aplikacja Triki, projekty typu
[TrikiVR](https://github.com/CreoleVR/TrikiVR) / SlimeVR). Protokół pochodzi z
[TrikiEmu](https://github.com/Maku-hub/TrikiEmu) (MIT).

- Włączenie: **podwójne kliknięcie** przycisku (2 mignięcia).
- Bez połączenia tag wraca do trybu lokalizatora po 3 minutach. Po rozłączeniu zostaje w trybie gry
  jeszcze 2 minuty, żeby gra mogła szybko połączyć się ponownie.
- W trakcie gry przycisk jest przyciskiem gry (bateria / konfiguracja / DFU nie reagują).
- Poza trybem gry **nic się nie zmienia** – zero dodatkowego poboru prądu.
- Tag rozgłasza się z usługą UART (NUS `6e400001-…`), baterią i wersją firmware, a po komendzie
  START wysyła ramki IMU (akcelerometr + żyroskop, ~100 Hz, ±16 g / ±2000 dps) i stan przycisku.

Tożsamość (adres BLE + nazwa) jest **konfigurowalna**, domyślnie tryb gry jest wyłączony:

- przez BLE:
  ```sh
  tools/triki_config.py -a <hasło> -K keys/NAZWA.keys --game-mac AA:BB:CC:DD:EE:FF --game-name "Triki 1234567890"
  ```
  wyłączenie: `--game-mac 0`,
- albo w `keys/triki_keys.h` (poza gitem):
  ```c
  #define DEFAULT_GAME_MAC { 0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA }   /* adres LSB-first */
  #define DEFAULT_GAME_NAME { 'T','r','i','k','i',' ','1','2','3','4','5','6','7','8','9','0' }
  #define DEFAULT_GAME_NAME_LEN 16
  ```

**Zapisywanie wyników w grach nie działa** – wymaga sekretnego klucza z oryginalnego firmware, który
znika przy odblokowaniu chipa. Sama rozgrywka działa. Komendy zapisu wyniku (`0a`/`09`) są ignorowane.

## APPROTECT

Oryginalny Triki ma włączoną blokadę debugowania (APPROTECT), a nowsze rewizje nRF52810 mają ją
„wzmocnioną”: po każdym resecie SWD jest zablokowane, chyba że UICR.APPROTECT = 0x5A **i** firmware
przy każdym starcie wpisze 0x5A do APPROTECT.DISABLE. Hex bootloadera zawiera wpis UICR, a bootloader
i aplikacja odblokowują SWD na starcie (`board/triki_approtect.h`), więc po wgraniu tego firmware’u
SWD zostaje dostępne.

## Pobór prądu

Nie zmierzony jeszcze na sprzęcie. Główne składniki: IMU w trybie 12,5 Hz low-power (~10 µA),
reklamy (~1–5 µA zależnie od stanu i mocy TX), nRF w System ON z RTC (~2 µA). Flash SPI jest
usypiany (deep power-down), żyroskop wyłączony. Najwięcej da niższa moc TX (`triki_config.py -p 1`)
i dłuższy `period` (`-d 4` albo `-d 8`).

## Struktura

```
app/            aplikacja (src/, config/sdk_config.h, armgcc/)
bootloader/     secure bootloader BLE (z przykładu SDK pca10040e_s112_ble)
board/          pinout Triki, obsługa APPROTECT
tools/          triki_config.py, gen_apple_key.py, make_default_keys.py, mergehex.py
keys/           klucze i tokeny (poza gitem – nigdy go nie commituj)
```

## Licencja

MIT (plik `LICENSE`). Pliki pochodzące z nRF5 SDK (bootloader, `sdk_config.h`, skrypty linkera)
zachowują licencję Nordic Semiconductor – patrz `NOTICE`.
