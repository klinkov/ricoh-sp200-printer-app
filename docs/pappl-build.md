# PAPPL on macOS 26 (Task 1)

- PAPPL version: 1.4.9 (`pkg-config --modversion pappl`), installed to /opt/homebrew (header: include/pappl/pappl.h).
- Install: `brew tap shawnz/printing && brew trust shawnz/printing && brew install shawnz/printing/pappl`
  (`brew trust` is required by current Homebrew, else "untrusted tap" error on dependency pdfio).
  Dependencies built/pulled: pdfio 1.6.0 (tap, from source), jpeg-turbo, libusb, libpng, openssl@3.
- Patches: only the one inline in the tap formula (SF Symbol "printer.fill" for the menu-bar icon,
  pappl/system-status-macos.m). No fixes were needed; build took ~6 s. Formula copy: Formula/pappl.rb (tap commit 5308bc9).
- Smoke: `clang $(pkg-config --cflags pappl) tests/smoke/hello-pappl.c $(pkg-config --libs pappl) -o build/hello-pappl`;
  `build/hello-pappl server -o server-port=8010`; `curl http://localhost:8010/` -> HTTP 200, HTML (1520 bytes).
  `dns-sd -B _ipp._tcp local.` shows no entries (no printers defined yet), no errors.
- Gotchas:
  - `papplMainloop` does not listen by itself: system_cb must call `papplSystemAddListeners(s, NULL)` and apply the `server-port` option.
  - Without `papplSystemSetFooterHTML` (or footer_html) the web home page SEGFAULTs in `papplClientHTMLFooter`->`papplLocGetString` (PAPPL 1.4.9). Always set a footer.
  - Port is plain HTTP on 8010 when started as non-root; stop the server via SIGTERM (`papplMainloop` handles it).
