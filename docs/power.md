# Pobór prądu

Zmierzone wartości i to, co ile kosztuje: [measurements.md](measurements.md). W skrócie: ~6,8 µA w
uśpieniu, ~20–25 µA średnio w ruchu, ~10 µA w bezruchu.

Dalej zejść można ustawieniami (`tools/triki_config.py`): dłuższy `period` (`-d 4` albo `-d 8` –
ramki rzadziej, średnia w ruchu proporcjonalnie niższa), krótszy czas do bezruchu (`-l`), niższa moc
TX (`-p 0`, kosztem zasięgu).

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

Przy pomiarze PPK2 przez `ppk2-api`: PPK2 odcina zasilanie układu, gdy program zamknie port, więc
pomiar i zasilanie trzyma jeden długo działający proces.
