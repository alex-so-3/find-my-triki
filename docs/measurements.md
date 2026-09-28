# Pomiary poboru prądu

Nordic Power Profiler Kit II w trybie source meter: 3,0 V podawane zamiast baterii na styk 3V3 i
pole GND, bateria wyjęta. Odczyt przez [ppk2-api](https://pypi.org/project/ppk2-api/), okna po
10 s. „Uśpienie” to mediana prądu między ramkami, „średnio” – średnia z całego okna. Firmware v17,
domyślne ustawienia (`period` 2, moc TX 0 dBm, próg ruchu 63 mg, bezruch po 10 min). W v17 w ruchu
połączenie przyjmowała co czwarta ramka Apple; od v18 każda (aplikacje z krótkim limitem czasu nie
trafiały w tag) – w ruchu to według szacunku ~1–2 µA więcej, uśpienie i bezruch bez zmian.

Skrypt odczytu gubił część próbek (~40 tys. z ~100 tys. na sekundę), więc średnie są przybliżone
(±20–30%); mediana uśpienia jest stabilna co do 0,1 µA.

## Wyniki

| Stan | Prąd |
|---|---|
| uśpienie między ramkami | **6,8 µA** |
| ruch – ramka co 1 s (Apple/Google na zmianę) | **~20–25 µA** średnio |
| bezruch – ramka co 10 s | **~10 µA** średnio (~7,5 µA w oknach bez ramki, ~13,5 µA z ramką) |
| miganie diodą (dzwonienie) | ~2 mA |
| bez tagu (zero przyrządu) | 0,1 µA |

Sprawdzone przy okazji: przejście w bezruch po 10 min, wybudzenie ruchem przy czujniku na 1,6 Hz,
dzwonienie i jego zatrzymanie.

## Co ile kosztuje w uśpieniu

Każda pozycja zmierzona osobno (zmiana jednego elementu, reszta bez zmian):

| Element | Wpływ |
|---|---|
| P0.04 w stanie niskim (wersje ≤ v11: firmware ustawiał go jako „SA0” czujnika) | +60 µA |
| pamięć NOR nieuśpiona (wersje ≤ v14: zamienione piny SCK/MOSI, komenda uśpienia nie docierała) | +6,5 µA |
| czujnik LSM6DSL 1,6 Hz zamiast power-down | +1,6 µA |
| podciągnięcia pinów NC czujnika wyłączone | 0 |
| watchdog | ~0 |
| przetwornica DC/DC | 0 w uśpieniu; w ruchu średnia ~20% niższa niż z LDO |

Historia uśpienia: v7 72 µA → v12 13,4 µA (P0.04) → v17 6,8 µA (pamięć NOR).

## Zgodność z dokumentacją

6,8 µA ≈ nRF52810 w System ON z RTC na kwarcu i pełnym RAM (1,1 µA, `ION_RAMON_RTC_LFXO`) +
LSM6DSL w low-power 1,6 Hz (4,5 µA, AN5040 tab. 7, przy 1,8 V) + MX25R8035F w deep power-down
(0,007 µA) + ~1 µA reszty (m.in. czujnik zasilany 3 V zamiast 1,8 V).
