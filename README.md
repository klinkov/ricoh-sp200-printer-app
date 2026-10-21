# ricoh-sp200-printer-app

Printer Application (PAPPL) для RICOH SP 200-серии (цель: SP 202SN) на macOS 26+.
macOS печатает по AirPrint/IPP на `localhost:8000`, приложение дизерит растр и шлёт DDST/JBIG на принтер (порт 9100).

Дизайн: docs/superpowers/specs/. План: docs/superpowers/plans/.

## Установка

Из tap (Homebrew требует явно доверять сторонним tap — и PAPPL-тапу `shawnz/printing`, и этому):

```sh
brew tap shawnz/printing && brew trust shawnz/printing
brew tap klinkov/ricoh && brew trust klinkov/ricoh
brew install ricoh-sp200-printer-app
```

Локально из клона (Homebrew принимает формулы только из tap, поэтому свой локальный tap):

```sh
brew tap-new --no-git local/ricoh
cp Formula/ricoh-sp200-printer-app.rb "$(brew --repo local/ricoh)/Formula/"
brew install --HEAD local/ricoh/ricoh-sp200-printer-app
```

`--HEAD` собирает закоммиченное состояние ветки `main` репозитория (не рабочую копию).

Зависимости: `shawnz/printing/pappl` (формула в `Formula/pappl.rb`), `jbigkit`.

## Запуск сервиса

```sh
brew services start ricoh-sp200-printer-app
curl -s -o /dev/null -w '%{http_code}\n' http://localhost:8000/   # 200
```

Лог: `$(brew --prefix)/var/log/ricoh-sp200-printer-app.log`. Веб-панель: http://localhost:8000.

## Добавление принтера

Через веб-панель (Printers → Add) или из консоли:

```sh
ricoh-sp200-printer-app add -d sp202sn -m ricoh-sp200 -v ricoh-socket://192.168.1.50:9100
# или обычный сокет:
ricoh-sp200-printer-app add -d sp202sn -m ricoh-sp200 -v socket://192.168.1.50:9100
```

`ricoh-socket://` корректно завершает задание (дожимает хвост буфера); `socket://` — запасной вариант.

## Очередь в macOS

System Settings → Printers & Scanners → «+» → принтер виден как AirPrint (`sp202sn @ localhost`). Или из консоли:

```sh
lpadmin -p SP202SN -E -v ipp://localhost:8000/ipp/print/sp202sn -m everywhere
lp -d SP202SN файл.pdf
```

## Локальная сеть (macOS)

При первой печати macOS спросит «ricoh-sp200-printer-app хочет найти устройства в локальной сети» — нажать «Разрешить».
Если отказали или запрос не появился, подключение к принтеру падает с `No route to host`, а очередь CUPS уходит на паузу.
Исправление: System Settings → Privacy & Security → Local Network → включить `ricoh-sp200-printer-app`, затем перезапустить сервис:

```sh
brew services restart ricoh-sp200-printer-app
```

## Если очередь CUPS остановилась

```sh
cancel -a SP202SN
cupsenable SP202SN
cupsaccept SP202SN
lpstat -p SP202SN
```

## Удаление

```sh
brew services stop ricoh-sp200-printer-app
brew uninstall ricoh-sp200-printer-app
lpadmin -x SP202SN
rm -f ~/Library/Application\ Support/ricoh-sp200-printer-app.state   # сохранённые принтеры
brew untap klinkov/ricoh
```
