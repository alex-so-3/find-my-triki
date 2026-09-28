# Pobór prądu

Zmierzone Nordic Power Profiler Kit II (zasilanie 3,0 V zamiast baterii, domyślne ustawienia):

| | Prąd |
|---|---|
| uśpienie między ramkami | **~6,8 µA** |
| średnio w ruchu (ramka co 1 s) | ~20 µA |

Uśpienie zgadza się z dokumentacją: nRF52810 z zegarem RTC ~1,1 µA, czujnik LSM6DSL budzący ruchem
przy 1,6 Hz ~4,5 µA, pamięć NOR w deep power-down ~0 µA. Pomiary poszczególnych elementów:

| Co | Zmiana w uśpieniu |
|---|---|
| P0.04 w stanie niskim (stare wersje) | +60 µA |
| pamięć NOR nieuśpiona (stare wersje: zamienione piny SCK/MOSI) | +6,5 µA |
| czujnik 1,6 Hz zamiast wyłączonego | +1,6 µA |
| watchdog, DC/DC | ~0 (DC/DC w ruchu obniża średnią o ~20%) |

Dalej zejść można ustawieniami: dłuższy `period` (`-d 4` albo `-d 8`), krótszy czas do bezruchu
(`-l`), niższa moc TX (`-p 0`, kosztem zasięgu).

## Debugowanie

`make DEBUG_CHR=1` dodaje do usługi konfiguracyjnej charakterystykę `ec` (tylko po haśle), przez
którą bez przeflashowania można zmieniać tryb pinów, czytać i pisać rejestry czujnika i wysyłać
komendy SPI – przy pomiarze na żywo od razu widać, co zmienia prąd:

```sh
tools/triki_config.py -a HASLO -K keys/NAZWA.keys --gpio                 # stan pinów
tools/triki_config.py -a HASLO -K keys/NAZWA.keys --pin 18=low            # off|low|high|in|pulldown|pullup
tools/triki_config.py -a HASLO -K keys/NAZWA.keys --i2c-read 10 --i2c-write 10=00
```

Pełna lista komend jest w nagłówku `app/src/config_svc.c`. `make WDT=0` buduje bez watchdoga.
Buildów debugowych nie zostawiaj na tagu na stałe.
