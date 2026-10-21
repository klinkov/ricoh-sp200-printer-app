# Сканер SP 202SN — разведка (2026-10-21)

Воспроизведение: `tools/scan-probe [ip]` (только чтение, без побочных эффектов).

## Найдено
- Открытые TCP (1-1024, 1865, 2000, 5000, 8000-8100, 9000-9500, 49152-49200, 54921-54925, 60000): только 80, 515, 9100. 631 в этом скане не отвечает на connect (в protocol.md отмечен слушающим — флаппинг/ограничение по соединениям); сканерных портов (54921, 1865, 49152+ и т.п.) нет.
- HTTP: `/eSCL/ScannerCapabilities`, `/eSCL/ScannerStatus`, `/WSD`, `/StableWSDiscoveryEndpoint/...` — все 404.
- Веб-панель (Web Image Monitor): ссылки только H02 (статус), H05/H07 (IPv4/IPv6), H11 (device info), H12 (interface), Login.htm. Страниц про Scan/Scan to/адресную книгу нет.
- WS-Discovery Probe (multicast и unicast на 3702): от принтера ответа нет (отвечали только другие хосты .5-.9, .85).
- SNMP Ricoh enterprise 1.3.6.1.4.1.367: девять OID — модель «SP 202SN», версия 1.02, MAC, IP, счётчик; ничего про сканер/сканы. hrDeviceTable: 4 записи (1 printer, 3 other), все «RICOH SP 202SN DDST».
- sysDescr «Network Scanner C model / Facsimile C model» — общая строка семейства прошивки DDST, не доказательство наличия сканера: 202SN — print-only (SN = сетевая печать), сканеры/факс у MFP-собратьев (SP 203SFN, 204SN/204SFN). Подтверждения спецификации 202SN в сети не найдено, но ни один сканерный признак на устройстве не обнаружен.

## Веб-источники
- sane-devel, SP204SN AIO network scanner (2020-03): eSCL не заявлен, спецификация — TWAIN/WIA (вероятно WSD), `ricoh2` backend не подтверждён: https://lists.alioth.debian.org/pipermail/sane-devel/2020-March/037628.html , .../037634.html , .../037646.html
- Спецификация SP 204SN (print/copy/scan, TWAIN): https://assets.ricoh-usa.com/j2jqn9lauv41/4CztT8x20ll47Dk4klZ3p5/9b27e416fe2a161812d000b4aab6c574/aficiosp204sn.pdf
- Готовых реализаций, дампов трафика Windows-драйвера «Scanner driver» для SP 200 series или Smart Device Connect-поддержки найти не удалось; спецификации SP 202SN в поиске нет.

## Вывод: NO-GO
- У 202SN, скорее всего, физического сканера нет (печатная модель). Сетевые признаки согласуются: нет сканерных портов, eSCL/WSD, пунктов Scan в панели.
- Даже для MFP-собратьев (204SN) протокол проприетарный (TWAIN/WIA, нет eSCL), без публичных реализаций.

## Рекомендация
- Убрать сканирование из scope драйвера. Остаётся только печать.
- Единственная проверка, которая может изменить вывод: осмотреть корпус (есть ли планшет/ADF) и серийную табличку. Если сканера нет — закрыть задачу. Если есть: захват трафика Windows-драйвера в VM (Wireshark на мосту), оценка 1-2 недели reverse engineering на SCAN-протокол; WSD/eSCL-пути маловероятны.

## Повторная проверка (2026-10-21, вечер): сетевое сканирование есть — push-модель

Пользователь сообщил, что под Windows сетевой сканер работает. Разбор руководства серии (Ricoh SP 21x, тот же Web Image Monitor; SP 204SN datasheet) даёт:

- **Scan to E-mail / Scan to FTP / Scan to Folder** — «available only through a network connection»: принтер сам отправляет скан (SMTP / FTP / SMB) по адресату из адресной книги. Адресаты регистрируются **только в Web Image Monitor** (вход администратора → «Scan Destination» / «One Touch Button» → Add New Type: Folder; поля Name, Service Name = `\\IP-или-имя\шара`, Domain, Login User Name, Password, Directory, форматы). С панели принтера адресаты не задаются. В списке поддерживаемых получателей папок указан Mac OS X.
- **TWAIN/WIA** — pull-модель: TWAIN-драйвер с CD, только Windows; datasheet SP 204SN относит TWAIN/WIA к USB. В сети принтер не открывает сканерных портов (см. выше), т. е. «сетевой TWAIN» для этой серии — это, по сути, тот же push через адресную книгу либо USB.
- Следствие для macOS: **драйвер не нужен**. Достаточно SMB-шары на Mac (System Settings → Sharing → File Sharing) и записи Folder-адресата в Web Image Monitor. Сканирование запускается с панели принтера (Scanner → Address Book → адресат → Start); файл (PDF/JPEG/TIFF) появляется в папке. Альтернатива — Scan to FTP на локальный FTP-сервер, но SMB у macOS встроенный.

**Вывод: GO** для схемы «Scan to Folder → SMB-шара Mac» (конфигурация + документация, без кода). Pull-сканирование из Image Capture — NO-GO (нужен реверс USB/TWAIN).

Состояние Mac на момент проверки: шара `Public` с guest-доступом объявлена, но `smbd` не запущен (File Sharing выключен).
