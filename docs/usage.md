# Zachowanie tagu

| Stan | Co robi tag |
|---|---|
| Ruch | jedno zdarzenie reklamowe co 0,5 s × `period`, na zmianę Apple / Google (domyślnie każda sieć co 2 s) |
| Bezruch (po `still_timeout`, domyślnie 10 min) | co 5 s × `period` (każda sieć co 20 s) |
| Przebudzenie | czujnik ruchu (próg ~63 mg) → od razu ramka i powrót do trybu „ruch” |
| Połączenie | każda ramka Apple przyjmuje połączenie – aplikacja może w każdej chwili połączyć się, żeby zadzwonić, zmienić ustawienia albo wgrać firmware (bez hasła połączenie jest zrywane po 20 s). Połączenie liczy się jak ruch, więc schowany tag zaczyna nadawać często i łatwiej go namierzyć |
| Tryb konfiguracji | reklama „TrikiTag” przez 120 s |
| DFU | bootloader „TrikiTagDFU”, po 2 min bez połączenia powrót do aplikacji |

## Przycisk

- **krótko**: stan baterii (3 mignięcia = pełna, 2 = OK, 1 = słaba); w trybie konfiguracji – wyjście,
- **podwójne kliknięcie**: tryb gry ([game-mode.md](game-mode.md)),
- **3 s**: tryb konfiguracji,
- **10 s**: restart do bootloadera DFU,
- **trzymany przy włożeniu baterii**: bootloader DFU (ratunek, gdy aplikacja nie działa).

## Dioda (zamiast brzęczyka)

- start: jedno długie mignięcie; 5 szybkich = brak kluczy; 2 wolne = nie wykryto czujnika,
- „odtwórz dźwięk” (DULT, np. z aplikacji): miga 10 s albo do polecenia stop,
- połączenie z poprawnym hasłem: jedno dłuższe mignięcie,
- zapis ustawień: 3 szybkie mignięcia, potem restart,
- bootloader DFU: świeci ciągle, gaśnie po połączeniu.

Dzwonienie z samej aplikacji Google Find Hub nie działa: pełny protokół FMDN (Fast Pair) nie mieści
się w nRF52810, dlatego tag nadaje tylko stały EID, a dzwoni się nim przez DULT.
