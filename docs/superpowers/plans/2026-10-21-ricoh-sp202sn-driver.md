# RICOH SP 202SN Printer Application — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** macOS 26+ печатает на RICOH SP 202SN по сети через локальный PAPPL printer application, без PPD и без Ghostscript.

**Architecture:** `libddst` (чистая C-библиотека) превращает 1-bit строки в поток DDST = PJL-кадры + JBIG1 (libjbig). Printer application на PAPPL принимает PWG/URF растр от macOS, дизерит в 1-bit, гонит через `libddst` в `socket://<ip>:9100`. Доставка — Homebrew tap с `brew services`.

**Tech Stack:** C11, clang, POSIX Makefile, libjbig (Homebrew `jbigkit`), PAPPL (Homebrew tap `shawnz/printing`, своя копия формулы), системный CUPS 2.3.4 (`cups-config`), `ipptool` (в составе macOS) для интеграции.

**Spec:** `docs/superpowers/specs/2026-10-21-ricoh-sp202sn-driver-design.md` (+ `docs/research-2026-10-21.md`)

## Global Constraints

- Платформа: macOS 26.7.1, Apple Silicon; сборка clang из Command Line Tools; cmake не используется.
- Рантайм-зависимости только `pappl`, `jbigkit`; Ghostscript/ImageMagick/netpbm — только в тестах (`pbmtojbg`).
- Разрешение 600×600 dpi; бумага A4/Letter/A5/B5/Legal/Executive; источники TRAY1, MANUAL; 1 плоскость, 1 бит.
- JBIG: эквивалент `pbmtojbg -p 72 -o 3 -m 0 -q` → order=3 (ILEAVE|SMID), options=72, mx=0; golden-тест байт-в-байт.
- Все PJL-строки оканчиваются CRLF; UEL = `\x1b%-12345X`.
- Никаких коммитов агентами; коммитит оркестратор после своего `make test`.
- Лицензия MIT; код `ricoh-sp100` / `ricoh-universal-ddst-driver` не копировать.
- Имена: бинарь `ricoh-sp200-printer-app`, библиотека `libddst.a`, tap `klinkov/ricoh`.

## Review Focus

1. Ширина строки не кратна 8: последние биты байта должны быть 0 (белые), иначе JBIG кодирует мусор на правом поле → тест в Task 3.
2. Страница 0 строк или `ddst_page_end` без `ddst_page_start` → ошибка, не падение → тест в Task 2.
3. Ошибка записи в сокет посреди JBIG → `ddst_*` возвращают false, PAPPL-job переходит в aborted, сокет закрывается, повторная печать работает → тест в Task 6 (mock-приёмник закрывает соединение после N байт).
4. macOS шлёт URF 8-бит серый с `cupsWidth` чуть меньше ожидаемого для A4 (поля из `media-*-margin`) → строки паддятся белым до PAPERWIDTH, не обрезаются → тест в Task 6.
5. Неизвестное имя бумаги от клиента (например `na_index-4x6_4x6in`) → отказ задания с понятной ошибкой, не `PAPER=` пустой → тест в Task 4.

---

### Task 1: PAPPL собирается и запускается на macOS 26 (этап 0b)

**Files:**
- Create: `Formula/pappl.rb` (копия из `shawnz/homebrew-printing` с их патчами, class `Pappl`)
- Create: `docs/pappl-build.md`
- Create: `tests/smoke/hello-pappl.c`

**Interfaces:**
- Produces: установленный `pappl` в `$(brew --prefix)`; `pkg-config --cflags --libs pappl` работает; факт, какие патчи нужны.

- [ ] **Step 1:** `brew tap shawnz/printing`; `brew install shawnz/printing/pappl` одной командой в фоне, лог в `/tmp/pappl-install.log`. Если формула не собирается — чинить в своей копии `Formula/pappl.rb` и ставить `brew install --build-from-source ./Formula/pappl.rb`.
- [ ] **Step 2:** Проверить: `pkg-config --modversion pappl` печатает версию; `ls $(brew --prefix)/include/pappl/pappl.h`.
- [ ] **Step 3:** Написать `tests/smoke/hello-pappl.c`: `papplMainloop(argc, argv, "1.0", NULL, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL)`-минимум, который поднимает систему с именем `hello-pappl`.
- [ ] **Step 4:** Собрать: `clang $(pkg-config --cflags pappl) tests/smoke/hello-pappl.c $(pkg-config --libs pappl) -o build/hello-pappl`. Expected: без ошибок.
- [ ] **Step 5:** Запустить `build/hello-pappl server -o server-port=8010` в фоне; `curl -s http://localhost:8010/` отдаёт HTML; `dns-sd -B _ipp._tcp local.` (4 с, убить) показывает хотя бы системную запись или пустой список без ошибок. Остановить свой процесс.
- [ ] **Step 6:** Записать в `docs/pappl-build.md`: версия PAPPL, список применённых патчей, команда установки, что не завелось.

---

### Task 2: libddst — PJL-кадры и состояние задания (без JBIG)

**Files:**
- Create: `Makefile` (цели `all`, `libddst.a`, `test`, `clean`; `CFLAGS=-std=c11 -Wall -Wextra -Werror -O2`; `JBIG_CFLAGS/LIBS` через `pkg-config` нет — использовать `$(brew --prefix jbigkit)/include|lib`)
- Create: `src/ddst/ddst.h`, `src/ddst/ddst.c`
- Create: `tests/test_util.h` (минимальный ASSERT/RUN без фреймворка), `tests/test_pjl.c`

**Interfaces:**
- Produces (`src/ddst/ddst.h`):
```c
typedef size_t (*ddst_write_fn)(void *ctx, const void *buf, size_t len); /* возвращает len при успехе */
typedef enum { DDST_SOURCE_TRAY1 = 0, DDST_SOURCE_MANUAL = 1 } ddst_source_t;
typedef enum { DDST_DOTCOUNT_DIV100 = 0, DDST_DOTCOUNT_RAW = 1 } ddst_dotcount_mode_t;
typedef struct {
  const char *filename;   /* NULL → "job" */
  const char *username;   /* NULL → "user" */
  time_t      timestamp;  /* 0 → time(NULL) */
  ddst_dotcount_mode_t dotcount_mode;
} ddst_job_info_t;
typedef struct {
  const char   *paper;       /* "A4", "LETTER", ... — уже имя PJL */
  unsigned      width_px, height_px;  /* PAPERWIDTH/PAPERLENGTH и геометрия битмапа */
  unsigned      resolution;  /* 600 */
  unsigned      copies;      /* >=1 */
  ddst_source_t source;
} ddst_page_info_t;
typedef struct ddst_job ddst_job_t;
ddst_job_t *ddst_job_start(const ddst_job_info_t *info, ddst_write_fn write, void *ctx); /* пишет преамбулу */
bool        ddst_page_start(ddst_job_t *job, const ddst_page_info_t *page);   /* выделяет битмап, строки белые */
bool        ddst_page_write_row(ddst_job_t *job, const uint8_t *row);         /* packed 1bpp, MSB первый, 1=чёрный, (width_px+7)/8 байт */
bool        ddst_page_end(ddst_job_t *job);   /* кодирует JBIG, пишет заголовок страницы + данные + трейлер */
bool        ddst_job_end(ddst_job_t *job);    /* пишет EOJ+UEL, освобождает job; возвращает false если были ошибки записи */
void        ddst_job_abort(ddst_job_t *job);  /* дописывает PAGESTATUS=END (если страница открыта), EOJ, UEL; освобождает */
const char *ddst_last_error(const ddst_job_t *job);
unsigned    ddst_page_rows_written(const ddst_job_t *job);
```
- В этой задаче `ddst_page_end` пишет `IMAGELEN=0` и пустые данные (JBIG — Task 3), но DOTCOUNT считает уже здесь (popcount по битмапу; при `DIV100` делить на 100 с округлением вниз).

- [ ] **Step 1: Write failing tests** в `tests/test_pjl.c` (сбор вывода в буфер через `ddst_write_fn`):
  - `test_job_preamble_exact`: `ddst_job_start` с filename="t.pdf", username="shade", timestamp=2026-10-21 12:34:56 UTC (ставить `TZ=UTC` в тесте через `setenv`) → буфер == `"\x1b%-12345X@PJL\r\n@PJL SET TIMESTAMP=2026/10/21 12:34:56\r\n@PJL SET FILENAME=t.pdf\r\n@PJL SET COMPRESS=JBIG\r\n@PJL SET USERNAME=shade\r\n@PJL SET COVER=OFF\r\n@PJL SET HOLD=OFF\r\n"`.
  - `test_page_frame_exact`: страница A4 16×8 px, copies=2, MANUAL, все строки белые → после `ddst_page_end` в буфере ровно: `"@PJL SET PAGESTATUS=START\r\n@PJL SET COPIES=2\r\n@PJL SET MEDIASOURCE=MANUAL\r\n@PJL SET MEDIATYPE=PLAINRECYCLE\r\n@PJL SET PAPER=A4\r\n@PJL SET PAPERWIDTH=16\r\n@PJL SET PAPERLENGTH=8\r\n@PJL SET RESOLUTION=600\r\n@PJL SET IMAGELEN=0\r\n@PJL SET DOTCOUNT=0\r\n@PJL SET PAGESTATUS=END\r\n"`.
  - `test_job_end_exact`: после `ddst_job_end` хвост буфера == `"@PJL EOJ\r\n\x1b%-12345X"`.
  - `test_dotcount_div100_and_raw`: 16×8, 3 строки 0xFF 0xFF (48 чёрных) → `DOTCOUNT=0` при DIV100 и `DOTCOUNT=48` при RAW.
  - `test_page_end_without_start_fails`: `ddst_page_end` сразу после `ddst_job_start` → false, `ddst_last_error` непустой, буфер не изменился.
  - `test_short_page_pads_white`: написано 2 строки из 8 → `ddst_page_end` true, `ddst_page_rows_written`==2 (недостающие строки белые, DOTCOUNT не растёт).
  - `test_write_error_propagates`: write-callback возвращает 0 → `ddst_job_start` возвращает NULL (ошибка на преамбуле) либо, если преамбула прошла, `ddst_page_end` false и `ddst_job_end` false.
- [ ] **Step 2:** `make test` → FAIL (нет `ddst.c`).
- [ ] **Step 3:** Реализовать `src/ddst/ddst.c`: битмап страницы целиком в памяти (`(width+7)/8 * height` байт, calloc), `vsnprintf` в `char line[128]` + write; `ddst_page_end` пока пишет IMAGELEN=0. Popcount через `__builtin_popcountll` по 8-байтовым словам, хвост побайтно.
- [ ] **Step 4:** `make test` → все `test_pjl` PASS, `-Werror` чистый.

---

### Task 3: JBIG-кодирование через libjbig + golden против pbmtojbg

**Files:**
- Modify: `src/ddst/ddst.c` (`ddst_page_end`), `Makefile` (линк `-ljbig`)
- Create: `src/ddst/pbm.h`, `src/ddst/pbm.c` (чтение P4; нужно тестам и `pbm2ddst`)
- Create: `tests/test_jbig.c`, `tests/golden/make-golden.sh`, `tests/golden/{text,photo,odd-width}.pbm` (генерировать скриптом детерминированно, без внешних картинок: текстоподобный узор, псевдослучайный «шум» с seed, ширина 4961 px)

**Interfaces:**
- Produces: `bool pbm_read(const char *path, unsigned *w, unsigned *h, uint8_t **rows)` — P4, `rows` packed MSB-first, 1=чёрный, `(w+7)/8` байт на строку.
- Произведённый JBIG байт-в-байт == `pbmtojbg -p 72 -o 3 -m 0 -q in.pbm out.jbg`.

- [ ] **Step 1:** Прочитать `pbmtojbg.c` из исходников jbigkit (`brew unpack jbigkit --destdir=/tmp/jbigkit-src` в scratchpad) и записать в комментарий в `ddst.c`: что делают `-p 72`, `-o 3`, `-m 0`, `-q`, какой `l0` (высота полосы) получается по умолчанию при `-q` без `-s`, какие `jbg_enc_options`/`jbg_enc_init` аргументы это даёт. Важно: `-q` в pbmtojbg переключает в «sequential» режим — воспроизвести точно.
- [ ] **Step 2: Write failing tests** `tests/test_jbig.c`:
  - `test_golden_text`, `test_golden_photo`, `test_golden_odd_width`: загрузить `tests/golden/X.pbm`, прогнать через `ddst_*` (A4, 600, 1 копия), вырезать из буфера байты между `IMAGELEN=N\r\n` и `@PJL SET DOTCOUNT` — длина N и содержимое == файл `tests/golden/X.jbg`, созданный `make-golden.sh` через `pbmtojbg`.
  - `test_odd_width_padding_bits_zero`: `odd-width` 4961 px: вызывающий пишет строку с мусором в хвостовых 7 битах последнего байта → библиотека обнуляет их (JBIG тот же, DOTCOUNT не учитывает).
- [ ] **Step 3:** `make test` → golden FAIL.
- [ ] **Step 4:** Реализовать: в `ddst_page_end` — `jbg_enc_init(&enc, w, h, 1, &bitmap_ptr, out_cb, job)`, `jbg_enc_options(&enc, order, options, l0, mx, my)` по значениям из Step 1, `jbg_enc_out`, `jbg_enc_free`. Вывод JBIG копить во временный буфер (нужен IMAGELEN до данных), затем писать заголовок, данные, трейлер.
- [ ] **Step 5:** `make test` → PASS. Если не совпадает — сравнивать `xxd` первые 20 байт (BIH-заголовок: Dl, D, P, flags, xd, yd, l0, mx, my, order, options) и чинить параметры, не golden.

---

### Task 4: Дизеринг и таблица бумаги

**Files:**
- Create: `src/ddst/dither.h`, `src/ddst/dither.c`, `src/ddst/paper.h`, `src/ddst/paper.c`
- Create: `tests/test_dither.c`, `tests/test_paper.c`

**Interfaces:**
- Produces:
```c
typedef struct ddst_dither ddst_dither_t;
ddst_dither_t *ddst_dither_new(unsigned width);                     /* Floyd–Steinberg с serpentine; порог 128 */
void ddst_dither_row(ddst_dither_t *d, const uint8_t *gray8, uint8_t *out_1bpp); /* gray 0=чёрный 255=белый → 1=чёрный */
void ddst_dither_free(ddst_dither_t *d);
void ddst_threshold_row(const uint8_t *gray8, unsigned width, uint8_t *out_1bpp); /* <128 → чёрный; для текста */
typedef struct { const char *pwg_name; const char *pjl_name; unsigned width_px_600; unsigned height_px_600; } ddst_paper_t;
const ddst_paper_t *ddst_paper_lookup(const char *pwg_media_size_name); /* NULL если нет */
```
- Таблица (мм → px при 600 dpi, округление к ближайшему): `iso_a4_210x297mm`→A4 4961×7016; `na_letter_8.5x11in`→LETTER 5100×6600; `iso_a5_148x210mm`→A5 3496×4961; `jis_b5_182x257mm`→B5 4299×6071; `na_legal_8.5x14in`→LEGAL 5100×8400; `na_executive_7.25x10.5in`→EXECUTIVE 4350×6300. (Значения уточняются после этапа 0a; хранить в одном месте.)

- [ ] **Step 1: Write failing tests:**
  - `test_threshold_row`: gray `{0,127,128,255,0,0,0,0}` → `0xC8`.
  - `test_dither_flat_50_percent`: 64×64 gray=128 → доля чёрных 45–55 %.
  - `test_dither_white_black`: gray 255 → все 0; gray 0 → все 1.
  - `test_paper_lookup_a4`: `iso_a4_210x297mm` → `A4`, 4961, 7016. `test_paper_lookup_unknown`: `na_index-4x6_4x6in` → NULL.
- [ ] **Step 2:** `make test` → FAIL. **Step 3:** реализовать. **Step 4:** `make test` → PASS.

---

### Task 5: Инструменты этапа 0a (`pbm2ddst`, `ddst-send`, `ddst-probe`)

**Files:**
- Create: `tools/pbm2ddst.c`, `tools/ddst-send` (sh), `tools/ddst-probe` (sh), `tools/testgrid.py`? — нет: `tools/mkgrid.c` (генерирует P4 A4 с рамкой 10 мм от края, сетка 10 мм, крест в центре, цифры не нужны)
- Modify: `Makefile` (цель `tools`)
- Create: `docs/hardware-test.md` (чек-лист из §6/§7 спеки, шаг за шагом, с командами)

**Interfaces:**
- `pbm2ddst [-p A4|LETTER…] [-s tray1|manual] [-c N] [-d div100|raw] in.pbm > out.ddst` — использует `pbm_read`, `ddst_paper_lookup` по PJL-имени (добавить `ddst_paper_lookup_pjl`), `ddst_*`.
- `ddst-send <host> [port=9100] <file>`: `nc -w 30`; код возврата nc.
- `ddst-probe <host>`: для портов 80 443 515 631 9100 8080 — open/closed (`nc -z -G 2`); `curl -s http://host/eSCL/ScannerCapabilities | head -c 300`; `dns-sd -L` не нужен; SNMP `snmpget` если есть в системе (нет — пропустить с пометкой).

- [ ] **Step 1:** тест `tests/test_tools.sh`: `mkgrid > grid.pbm`; `pbm2ddst grid.pbm > grid.ddst`; `grep -c 'PAGESTATUS=END' grid.ddst` == 1; `IMAGELEN` в файле == фактической длине между заголовком и `DOTCOUNT` (считать `python3 -I`-скриптом или awk по байтам — проще: `pbm2ddst --check` режим не делать, считать в sh через `tail -c`).
- [ ] **Step 2:** реализовать, `make tools test` → PASS.
- [ ] **Step 3:** `docs/hardware-test.md`: протокол этапа 0a с критериями из §7 спеки (0a), поля для записи результатов, и куда писать итог (`docs/protocol.md`).

---

### Task 6: PAPPL printer application + mock-интеграция (этап 2; после Task 1 и 4)

**Files:**
- Create: `src/app/main.c` (`papplMainloop`), `src/app/driver.c`, `src/app/driver.h`
- Create: `tests/integration/mock-sink.c` (TCP-сервер на 127.0.0.1:9101, пишет всё в файл; опция `--close-after N` байт), `tests/integration/run.sh`, `tests/integration/page.pwg` (генерировать `ipptool`/`cupsfilter`? — проще: PAPPL принимает `image/png`; использовать PNG 3 страниц нельзя → три `image/png` задания или один `image/pwg-raster` 3-страничный, созданный маленькой C-утилитой `tests/integration/mkpwg.c` через `cupsRasterOpen`)
- Modify: `Makefile`

**Interfaces:**
- Consumes: `ddst_*`, `ddst_dither_*`, `ddst_paper_lookup`.
- Driver: имя `ricoh-sp200`, `pappl_pr_driver_data_t`: `format="application/vnd.ricoh-ddst"`, `raster_types = PAPPL_RASTER_TYPE_SGRAY_8 | PAPPL_RASTER_TYPE_BLACK_1`, `x_resolution[0]=y_resolution[0]=600`, `num_resolution=1`, `media[]` = 6 PWG-имён из Task 4, `source[]={"tray-1","manual"}`, `sides_supported=PAPPL_SIDES_ONE_SIDED`, `color_supported=PAPPL_COLOR_MODE_MONOCHROME`, `ppm=22`, `kind=PAPPL_KIND_LASER`.
- Callbacks: `rstartjob_cb` → `ddst_job_start` с write-callback на `papplDeviceWrite`; `rstartpage_cb` → `ddst_paper_lookup(options->media.size_name)`, при NULL `papplJobError` + false; `rwriteline_cb` → если `cupsBitsPerPixel==8` дизеринг (`print-content-optimize=text` → threshold), иначе копия; паддинг/обрезка строки до `width_px`; `rendpage_cb` → `ddst_page_end`; `rendjob_cb` → `ddst_job_end`; при отмене (`papplJobIsCanceled`) → `ddst_job_abort`.
- Auto-add: `driver_cb` + `autoadd_cb` сопоставляет device-id `MFG:RICOH;…MDL:SP 20x` → `ricoh-sp200`.

- [ ] **Step 1:** Написать `tests/integration/run.sh` (падает): собирает всё, запускает `mock-sink` → `out.ddst`, запускает `ricoh-sp200-printer-app server -o server-port=8011` в фоне, `ricoh-sp200-printer-app add -d test -m ricoh-sp200 -v socket://127.0.0.1:9101`, печать `ipptool -tv -f tests/integration/page3.pwg ipp://localhost:8011/ipp/print print-job.test` (файл теста из `/usr/share/cups/ipptool/print-job.test` или свой минимальный), ждёт завершения; проверяет: `grep -c PAGESTATUS=END out.ddst` == 3, один `@PJL EOJ`, каждая `IMAGELEN` равна длине данных, `PAPERWIDTH=4961`. Второй прогон с `--close-after 5000`: job в состоянии `aborted` (`ipptool get-jobs`), сервис жив, третий прогон снова печатает 3 страницы.
- [ ] **Step 2:** Реализовать `main.c`, `driver.c`.
- [ ] **Step 3:** `make integration` → PASS. Убить свои процессы.
- [ ] **Step 4:** Ручная проверка: `ricoh-sp200-printer-app server` → «Принтеры и сканеры» видит `test` как AirPrint; печать страницы из TextEdit в mock → `out.ddst` с одной страницей, `cupsWidth` в логе ≤ 4961, строки паддятся.

---

### Task 7: Homebrew tap + brew services + README (этап 3; после Task 6)

**Files:**
- Create: `Formula/ricoh-sp200-printer-app.rb` (`depends_on "pappl"`, `"jbigkit"`; `system "make", "install", "PREFIX=#{prefix}"`; `service do run [opt_bin/"ricoh-sp200-printer-app", "server", "-o", "server-port=8000"]; keep_alive true; log_path var/"log/ricoh-sp200-printer-app.log" end`; `test do` — `--version`)
- Modify: `Makefile` (`install`, `PREFIX`), `README.md` (установка, добавление принтера по IP через веб-панель `http://localhost:8000`, удаление)

- [ ] **Step 1:** `brew install --build-from-source ./Formula/ricoh-sp200-printer-app.rb` → ставится; `brew services start ricoh-sp200-printer-app`; `curl localhost:8000` отвечает. `brew services stop`.
- [ ] **Step 2:** Создать GitHub-репозиторий `klinkov/homebrew-ricoh` (оркестратор/пользователь, не агент), проверить критерий §10 спеки: `brew uninstall` обоих + `brew untap`, затем `brew install klinkov/ricoh/ricoh-sp200-printer-app` с нуля.

---

### Task 8: Этап 0a на железе (ждёт включённого принтера)

- [ ] Выполнить `docs/hardware-test.md` целиком через `tools/`; результаты (порт, DOTCOUNT-режим, реальные PAPERWIDTH/LENGTH и поля, политика отмены) записать в `docs/protocol.md`; внести в `src/ddst/paper.c` и значение по умолчанию `dotcount_mode`.
- [ ] Аппаратная проверка Task 6 по чек-листу §6 спеки.

---

### Task 9: Сканер — разведка (этап 4, после Task 8)

- [ ] `tools/ddst-probe` + `dns-sd -B _uscan._tcp` + `curl /eSCL/ScannerCapabilities`; если eSCL есть — `docs/scanner.md` с инструкцией (macOS Image Capture подхватывает сам); если нет — `docs/scanner-research.md` с планом захвата трафика родного драйвера и отдельный дизайн-документ. Отчёт go/no-go.
