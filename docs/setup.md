# Klucze, konfiguracja, OTA

## Klucze

- **Apple:** `tools/gen_apple_key.py -n NAZWA` → `keys/NAZWA.keys` (klucz prywatny – zrób kopię,
  bez niego nie odczytasz lokalizacji; importuje się go do OpenTagViewer-triki) i `keys/NAZWA_keyfile`
  (dla firmware). Pasuje też `*_keyfile` z macless-haystack (używany jest pierwszy klucz).
- **Google:** tracker rejestruje się na koncie narzędziem
  [GoogleFindMyTools](https://github.com/leonboe1/GoogleFindMyTools): `python main.py` → zaloguj →
  `r` → skopiuj „Advertisement Key” (20-bajtowy EID).

Stały EID działa tylko, jeśli ktoś co kilka dni wysyła Google identyfikatory na najbliższe dni
(prawdziwy tracker robi to przez telefon). Robi to codziennie
[googlefind-service](https://github.com/alex-so-3/googlefind-service) – bez niej tag po ~4 dniach
znika z Find Hub.

## Wgranie kluczy i ustawień

Przez BLE, w dowolnym momencie (`-K` szuka tagu po jego ramce Apple):

```sh
tools/triki_config.py -a abcdefgh -K keys/NAZWA.keys -k keys/NAZWA_keyfile -f <EID> -n NoweHas1
tools/triki_config.py -a NoweHas1 -K keys/NAZWA.keys --info      # bateria, wersja, ustawienia
tools/triki_config.py -a NoweHas1 -K keys/NAZWA.keys --ring      # miganie LED
tools/triki_config.py -h                                         # interwał, moc TX, próg ruchu, ...
```

**Zmień domyślne hasło** `abcdefgh` (`-n`) – tag przyjmuje połączenia w każdej chwili. Bez `-K`
narzędzie szuka tagu po nazwie „TrikiTag” w trybie konfiguracji (przycisk 3 s).

Na macOS znaleziony tag jest zapamiętywany (`~/.cache/find-my-triki/`), kolejne uruchomienia łączą
się bez skanowania i czekają do 90 s na ramkę przyjmującą połączenie (`--connect-timeout`,
`--rescan`).

Klucze można też wkompilować (`keys/triki_keys.h` poza gitem):
`tools/make_default_keys.py -k keys/NAZWA_keyfile -f <EID> && make clean && make`.

Usługa konfiguracyjna jest zgodna z Everytag (`conn_beacon.py` działa; `-l` to tu czas do
bezruchu, a `-m` próg ruchu w mg).

## OTA

`make dfu APP_VERSION=<wyższa niż obecna>`, potem jedno z:

- aplikacja OpenTagViewer-triki → tag → *Device settings* → aktualizacja firmware → `build/triki_app_dfu.zip`,
- `tools/triki_dfu.py build/triki_app_dfu.zip -K keys/NAZWA.keys` (z komputera, bez dongla Nordica;
  tag już w bootloaderze: `--bootloader`),
- nRF Connect / nRF Device Firmware Update → „TrikiTagDFU” (tag do bootloadera: przycisk 10 s).

Bootloader przyjmuje tylko paczki podpisane Twoim kluczem, a stara aplikacja zostaje, dopóki nowa
nie zostanie odebrana i sprawdzona – przerwany transfer nic nie psuje.

Jeśli połączenie z Maca raz po raz kończy się timeoutem, choć tag nadaje, pomaga restart Bluetooth:
`blueutil -p 0 && sleep 3 && blueutil -p 1` (`brew install blueutil`).

macOS nie odświeża zapamiętanego układu usług BLE po aktualizacji. Jeśli `triki_dfu.py` nie widzi
usługi DFU (`Characteristic 8ec90003… was not found`) albo `triki_config.py` dostaje błędy długości,
wyczyść tę pamięć (sparowanych urządzeń nie rusza):
`sudo rm -f /Library/Bluetooth/com.apple.MobileBluetooth.ledevices.other.db* && sudo pkill bluetoothd`
