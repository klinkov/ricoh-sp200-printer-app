# SP 202SN — факты с железа

## Сеть (2026-10-21, принтер 192.168.1.60)

- SNMP v1 `public` работает. `sysDescr` = `RICOH SP 202SN DDST V1.02 / RICOH Network Printer C model / RICOH Network Scanner C model / RICOH Network Facsimile C model`.
- Порты: 80 (веб-панель `Web-Server/3.0`), 515 (LPD), 631 (слушает, но IPP `Get-Printer-Attributes` → `server-error-operation-not-supported`, использовать нельзя), 9100 (raw). Закрыты: 443, 8080, 8000, 9101, 9220, 54921 и др.
- Bonjour: принтер ничего не анонсирует (`_printer`, `_pdl-datastream`, `_ipp`, `_uscan`, `_scanner` — пусто). eSCL (`/eSCL/ScannerCapabilities`) — пустой ответ. Сканер: протокол проприетарный, см. этап 4.
- Транспорт для драйвера: `socket://192.168.1.60:9100` (основной), LPD 515 — запасной.

## SNMP OID для тестов этапа 0a

| Что | OID | Значение 2026-10-21 |
|---|---|---|
| Счётчик страниц (prtMarkerLifeCount) | `1.3.6.1.2.1.43.10.2.1.4.1.1` | 9219 |
| Статус (hrPrinterStatus) | `1.3.6.1.2.1.25.3.5.1.1.1` | idle(3) |
| Тонер уровень (prtMarkerSuppliesLevel) | `1.3.6.1.2.1.43.11.1.1.9.1.1` | 60 |
| Тонер максимум (prtMarkerSuppliesMaxCapacity) | `1.3.6.1.2.1.43.11.1.1.8.1.1` | 100 |

Команда: `snmpget -v1 -c public 192.168.1.60 <OID>`. Уровень тонера в целых процентах — для проверки DOTCOUNT печатать много (десятки) тёмных страниц либо смотреть счётчик в веб-панели (80), если там есть точнее.

## Ещё не подтверждено (этап 0a)
- Режим DOTCOUNT (popcount vs /100).
- Реальные PAPERWIDTH/PAPERLENGTH и поля для A4.
- Нужен ли `PAGESTATUS=START` на каждую страницу.
- Поведение при обрыве посреди страницы.

## Веб-панель (Web Image Monitor, http://192.168.1.60/)
- `H02_Home_status.htm`: «Общий счётчик» = 9172 (отличается от SNMP prtMarkerLifeCount 9219 — разные счётчики; для этапа 0a фиксировать оба), тонер — только графическая шкала 0/50/100.
- `H11_device_info.htm`: серийник, память 32 MB. Точного счётчика тонера нет → DOTCOUNT проверять через SNMP `prtMarkerSuppliesLevel` на большом объёме печати либо принять /100 (вариант фильтра, заявившего SP 202SN) и наблюдать.
