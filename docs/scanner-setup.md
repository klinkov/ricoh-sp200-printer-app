# Сканирование SP 202SN на Mac без драйвера: Scan to Folder → SMB-шара

Принтер сам отправляет скан по SMB в папку на Mac. Запуск — с панели принтера. Драйвер, PAPPL и
printer-app не участвуют.

## 1. Mac: включить File Sharing и завести папку

1. Создать папку, например `~/Scans`.
2. System Settings → General → Sharing → **File Sharing** → включить. Нажать ⓘ рядом:
   - Shared Folders: «+» → `~/Scans`.
   - Users: для этой папки дать **Read & Write** либо отдельному локальному пользователю (рекомендуется:
     Users & Groups → создать «scanner», только для этого), либо «Everyone» + в Options включить
     «Share files and folders using SMB» для нужного пользователя.
   - Options… → «Share files and folders using SMB» → отметить пользователя, под которым будет входить
     принтер (macOS попросит его пароль, чтобы включить SMB-аутентификацию).
3. Запомнить имя Mac (System Settings → General → Sharing → «Local hostname», например
   `Mac-mini-M4-Pro.local`) или IP (сейчас `192.168.1.126`; лучше закрепить его на роутере, иначе адресат
   на принтере устареет).
4. Проверка с другого устройства или с самого Mac: `smbutil view //scanner@192.168.1.126` должен показать
   шару `Scans`.

## 2. Принтер: добавить адресата в Web Image Monitor

1. В браузере открыть `http://192.168.1.60/` → «Вход в систему» (пароль администратора; по умолчанию у
   Ricoh этой серии он пустой или `admin` — если не менялся).
2. Меню **Scan Destination** (в русской панели — «Адресат сканирования»; у некоторых прошивок —
   «One Touch Button») → **Add New Type** → **Folder** (Папка).
3. Заполнить:
   | Поле | Значение |
   |---|---|
   | Name | `Mac` (до 16 символов, покажется на панели) |
   | Quick Dial | по желанию, чтобы вызывать одной кнопкой |
   | Service Name | `\\192.168.1.126\Scans` (IP или имя Mac + имя шары; 64 + 32 символа) |
   | Domain | пусто |
   | Login User Name | `scanner` (пользователь Mac с доступом к папке) |
   | Password | его пароль |
   | Directory | пусто (или подпапка внутри шары) |
   | File format (B&W / Color) | PDF (или JPEG/TIFF) |
   | Scan size / Resolution / Density | по вкусу; 300 dpi для документов |
4. Submit. Если есть кнопка «Test»/«Connection test» — нажать.

## 3. Сканирование

На принтере: клавиша **Scanner** → положить оригинал (стекло или ADF) → **Address Book** → выбрать
`Mac` → **Start** (B&W или Color). Файл появится в `~/Scans`.

## Если не работает

- Принтер пишет ошибку соединения: проверить, что Mac не спит (Energy → «Prevent automatic sleeping»
  или Wake for network access), File Sharing включён, в Options отмечен SMB для пользователя, IP не
  поменялся. Guest-доступ принтер может не поддерживать — задавать пользователя и пароль.
- Firewall macOS: разрешить входящие для «smbd» (System Settings → Network → Firewall → Options).
- Проверка с Mac, что SMB вообще отвечает: `nc -z 192.168.1.126 445`.
- Журнал на принтере: Web Image Monitor → Reports → Scanner Journal.

## Вариант Б: сканы на SSD роутера Keenetic (192.168.1.1), Mac не нужен

Роутер (Netcraze Giga, KeeneticOS 5.1) уже раздаёт SSD по SMB: шара `SDD` (NTFS, 250 ГБ, смонтирована как
`/tmp/mnt/586298136297F3CA`), режим `cifs permissive` — анонимный (гостевой) доступ с правом записи.
Папка под сканы создана: `/tmp/mnt/586298136297F3CA/Scans` (2026-10-21). Плюсы: принтер шлёт на роутер,
который всегда включён; папка видна с любого устройства в сети как `smb://192.168.1.1/SDD/Scans`.

1. Роутер (проверка, менять ничего не нужно): веб-интерфейс → «Приложения» → «Сеть Windows» (SMB) —
   включено, в списке папок есть `SDD`, доступ «Без пароля» (permissive). Если захочется закрыть гостевой
   доступ — там же выключить «анонимный доступ» и дать права пользователю `shade` (у него уже есть тег
   `cifs`); тогда в адресате принтера указать логин/пароль этого пользователя.
   То же из CLI роутера: `ssh keenetic` → `ndmc -c "show running-config" | grep -A4 '^cifs'`.
2. Принтер: Web Image Monitor → Scan Destination → Add New Type → Folder:
   | Поле | Значение |
   |---|---|
   | Name | `SSD` |
   | Service Name | `\\192.168.1.1\SDD` |
   | Domain | пусто |
   | Login User Name | пусто (гость); при отключённом анонимном доступе — `shade` |
   | Password | пусто; либо пароль `shade` |
   | Directory | `Scans` |
   | File format | PDF |
3. Сканирование — как в разделе 3, адресат `SSD`. Файлы: на Mac — Finder → Go → Connect to Server →
   `smb://192.168.1.1/SDD/Scans` (гость) или `smb://shade@192.168.1.1/SDD/Scans`.

Проверка гостевой записи с Mac (`mount_smbfs -N //guest@192.168.1.1/SDD`) из сессии Claude не прошла по
причине macOS TCC (доступ к сетевым томам у приложения не разрешён — `Operation not permitted` даже на
`ls`), а не из-за роутера; проверить руками через Finder или первым сканом. Если принтер пишет ошибку
доступа — переключиться на вариант с пользователем `shade`.

Примечания:
- NTFS на роутере (`tntfs`, umask 02) — имена файлов без ограничений, права не важны.
- Старая Keenetic-шара `32G` (ext4-флешка) тоже доступна гостю, но под сканы выбран SSD.
- Scan to FTP на роутер возможен (Keenetic FTP-сервер, «Приложения» → FTP), но SMB уже включён, FTP
  включать незачем.

## Альтернативы

- **Scan to E-mail**: Web Image Monitor → Network Settings → SMTP (сервер, порт, логин) и адресат типа
  E-mail. Работает с любым почтовым ящиком, не зависит от того, включён ли Mac.
- **Scan to FTP**: на Mac встроенного FTP-сервера нет; нужен сторонний (например `pyftpdlib`). Смысла
  мало при наличии SMB.
- **TWAIN/WIA** (сканирование из приложения): только по USB и только Windows-драйвер; на Mac не
  поддерживается. Image Capture принтер не увидит.
