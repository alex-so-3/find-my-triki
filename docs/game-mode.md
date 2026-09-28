# Tryb gry

Firmware może działać jak oryginalny kontroler Triki – dla aplikacji Triki i projektów typu
[TrikiVR](https://github.com/CreoleVR/TrikiVR) / SlimeVR. Protokół pochodzi z
[TrikiEmu](https://github.com/Maku-hub/TrikiEmu) (MIT).

- Włączenie: **podwójne kliknięcie** (2 mignięcia; 3 szybkie = tryb nie jest skonfigurowany).
- Bez połączenia tag wraca do trybu lokalizatora po 3 minutach, a po rozłączeniu czeka jeszcze
  2 minuty na ponowne połączenie.
- W trakcie gry przycisk jest przyciskiem gry.
- Poza trybem gry nic się nie zmienia – zero dodatkowego poboru prądu.
- Tag udostępnia usługę UART (NUS `6e400001-…`), baterię i wersję, a po komendzie START wysyła
  ramki IMU (akcelerometr + żyroskop, ~100 Hz, ±16 g / ±2000 dps) i stan przycisku.

Tożsamość (adres BLE + nazwa) jest konfigurowalna, domyślnie tryb gry jest wyłączony:

```sh
tools/triki_config.py -a <hasło> -K keys/NAZWA.keys --game-mac AA:BB:CC:DD:EE:FF --game-name "Triki 1234567890"
tools/triki_config.py -a <hasło> -K keys/NAZWA.keys --game-mac 0      # wyłączenie
```

albo w `keys/triki_keys.h` (poza gitem): `DEFAULT_GAME_MAC` (LSB-first), `DEFAULT_GAME_NAME`,
`DEFAULT_GAME_NAME_LEN`.

**Zapisywanie wyników w grach nie działa** – wymaga sekretnego klucza z oryginalnego firmware, który
znika przy odblokowaniu chipa. Sama rozgrywka działa.
