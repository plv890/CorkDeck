# Удалённое управление, диагностика и восстановление

Все сведения — последний подтверждённый checkpoint. Состояние не опрашивалось
заново при миграции документов. Пользователь хочет экономить screenshots/image tokens,
переиспользовать маршруты, давать медленным играм5–10 минут и не повторять проверки без причины.

## Каналы и их фактический статус

| Канал | Назначение | Последнее состояние |
| --- | --- | --- |
|192.168.70.59:6000 sys-botbase|buttons/title/JPEG/bounded direct SD tails|работает, новый FS isolation NSP активирован reboot|
|:5000 DBI FTP Browse SD|полные logs/update/readback|последний экран DBI FTP, два full logs успешно сохранены|
|:8181 Autorun HTTP|authenticated status/live/perf|Autorun закрыт; key прежнего запуска истёк|
|:22225 Atmosphere GDB|native debugging|временно работал; исходные настройки восстановлены/reboot, TCP затем не подключился|
|:5001 background FTP|старый sys-ftpd experiment|отключён после resource/WLAN fatal; не reenable blindly|
|Hekate USB Mass Storage|recovery/SD access|ранее работал, physical stage/disk verification/eject нужны|
|Horizon USB DBI/NSLoader|альтернативный transfer|ранее не заработал; не считать готовым каналом|

Title IDs: Autorun `0548EABB35576000`, DBI `03DB117EE17B6000`.
GTA reference title `0100B00B51230000` не target наших controller tests.
sysbot program ID `430000000000000B`, version string2.5 / arVersion1.
HOME без активного приложения возвращает blank title → Python None; это нормально.
None не доказывает выбранную плитку и не исключает hbmenu applet.
DHCP/IP могут измениться: не сканировать сеть без необходимости, проверить known host first.

## Модель управления и подтверждений

1. Использовать последнее известное состояние, recents/selection и title guard.
2. Известный batch выполнять без скриншота после каждой кнопки.
3. Screenshot только unknown state, mismatch/error или visual acceptance.
   Если broker механически сохранил JPEG, не обязательно открывать/emit его.
4. TCP send success — доставка, не доказательство исполнения кнопки.
   Подтверждение: ожидаемый title, FTP connect, fresh runtime state или нужный final screen.
5. Mismatch/timeout — остановить очередь, не повторять A/Play вслепую.
6. Release/detach в finally; одной attachment достаточно для sequence.
7. При ручной игровой сессии «управление не перехватывай» — только чтение,
   пока пользователь не разрешит управление/не изменит задачу.
8. Касание/яркость не проверяют HOME. Физическая HOME подтверждается самим действием,
   а пропажа всех сетевых портов сама по себе не доказывает system hang.

Пользователь разрешал самостоятельные scoped installs/tests/general fixes.
Этот файл сохраняет существующие решения, не создаёт новой авторизации.
Не просить снова разрешение на обычную обратимую работу уже согласованного объёма.
Новые firmware/global changes не следуют автоматически из разрешения affinity test.

## Проверенные маршруты

`tools/remote_switch/navigation.py`:
sequence → guard → UNUSED priming → wait1s → guard перед каждой кнопкой → detach finally.
A/B/MINUS settling0.75s; directions0.25s.
click_ready → UNUSED discovery1s → press0.2s → release/detach.

### HOME и закрытие

- `home_primed`: UNUSED → wait0.3s → HOME → wait0.7s → detach.
  UNUSED upstream workaround должен использовать тот же virtual device до HOME.
- Active Autorun/DBI → HOME → X → A → wait≥2s → titleNone.
- Только HOME приостанавливает application, не закрывает его.
- Физическая Power/reboot через имеющийся controller protocol недоступна.
  В проверенном DBI меню не найден доступный нам надёжный reboot route.
- Прямое переключение HOME→другое app→system close dialog пользователь описывал,
  но точный confirmation/default не оформлен helper. Не выдумывать этот batch.

### DBI905-ru → FTP SD

- Fresh launch: дать примерно12s на startup/network rows; main **первая строка**.
- `Down×12 → A → Down×2 → A` → FTP **Browse SD**, port5000.
- Из FTP: B stops server, возвращает main с FTP row selected.
- Тогда reopen: `A → Down×2 → A`, без Down×12.
- После полного close/relaunch main снова первая строка: короткий reopen route неверен.
- Не путать FTP SD с NAND/saves/game-install режимами.
- FTP connection/readback — подтверждение маршрута, QR/network screen публиковать не нужно.

### Порядок HOME recents и launcher

- После закрытия Autorun: Autorun первая плитка, DBI справа; Right/A запускает DBI.
- После закрытия DBI: DBI первая, Autorun справа; Right/A запускает Autorun.
- На последнем reboot HOME selected DBI first; один targeted screenshot подтвердил,
  затем Right/A успешно запустил Autorun.
- Recents меняются после собственных launches/действий пользователя: не вечные индексы.
- Last clean Autorun Home selected **The Forest first**; Assassin’s Creed next,
  Dishonored next. Не использовать старую команду «A значит Dishonored».
- Launcher/HOME Nintendo labels. In-game Xbox mapping: A→NintendoB, B→A, X→Y, Y→X.
  Broker `--layout xinput` делает эту подстановку, не менять HOME layout.
- Steam settings от подтверждённого состояния: MINUS→Right→Down×3→A.
  Section/row remembers selection; не повторять route при неизвестной странице.
- Header Steam library: Up→Right×2→A от выбранной game card; library L cycles
  Downloads→Installed→All games. Только при том же подтверждённом состоянии.

## CLI и инструменты

Ниже cwd **D:/autorun fork/autorun**. В workspace root добавить `autorun/` к пути tools.
Не копировать placeholders как реальные key/output paths.

```powershell
python tools/remote_switch/broker.py --host 192.168.70.59 probe
python tools/remote_switch/broker.py --host 192.168.70.59 direct-logs --live
python tools/remote_switch/broker.py --host 192.168.70.59 click A --ready --expected-title 0548EABB35576000
python tools/remote_switch/broker.py --host 192.168.70.59 navigate close-title --expected-title 0548EABB35576000 --start-confirmed
python tools/remote_switch/broker.py --host 192.168.70.59 navigate dbi-ftp-sd --expected-title 03DB117EE17B6000 --start-confirmed
python tools/remote_switch/observe.py --host 192.168.70.59 --game TheForest --source direct --seconds 660 --interval 10 --recovery-seconds 60 --no-screenshots
```

Это **примеры условий**, не начальная последовательность для текущего DBI FTP.
Observer **не запускает игру**: --game — label, не selector. Play отправляется другим шагом один раз.
Для известных маршрутов можно Controller/navigation.sequence напрямую, без broker screenshots.
Смотрите `QUICK_NAVIGATION.md`, `broker.py --help`; не передавать arbitrary raw shell/network commands.

Файлы PC modules: controller, transport, navigation, direct_files, credentials,
diagnostics, observe, performance, performance_data, ftp_logs, debugger, sample_download.
Runs `tools/remote_switch/runs/<UTC-id>/action.json/observation.json/logs/JPEG`.
Python warning real location допускается только при успешном exit/result.

## FTP, limits, credentials и файлы

DBI FTP уже foreground, login в существующих scripts без нового секретного account.
DBI absolute RETR/STOR/SIZE работают; **LIST/NLST абсолютного directory могут вернуть []**.
Для inventory: `cwd(directory)`, затем `LIST` текущего directory.
Пустой absolute LIST не доказывает отсутствие games/dependencies.

Обычный broker FTP tail16KiB, `--whole`512KiB; большие логи он пропустит.
Scoped full collectors для известных четырёх/двух имён ограничены8MiB/file,
проверяют bytes=size и применяют redaction. Нельзя читать весь SD/все account файлы ради одного log.
Direct channel tail2KiB/file, максимум32 whitelist names. Закрытые snapshots читаются во время игры;
active autorun_runtime.log может возвращать open error из-за sharing, это не отсутствие logger.

`direct-stage-test/direct-activate-test` — отдельный probe NRO≤2MiB под
`/switch/autorun-remote-test`, digest stage/owned previous/readback/no active applet.
Это **не uploader production40MiB Autorun** и не arbitrary SD writer.
Production update — DBI FTP с transaction/hash/backup.

HTTP8181 требует временный `X-Autorun-Key`; credentials.py хранит key DPAPI Windows user.
Не печатать keys/Guard/password/tokens/signed URLs. Старый DPAPI file path ≠ valid current key.
Key после app exit/restart недействителен, pause/resume network перед Play может его сохранить.
Steam vault ≠ LAN diagnostic key. sysbot/direct protocol без auth — существующий trusted LAN scope,
не public forwarding. Redaction arbitrary game stdout не абсолютная гарантия privacy.

## Live logs и passive performance

HTTP GET `/status`, `/files`, `/tail/<name>`, `/live`, `/perf`;
POST `/perf/start`, `/perf/stop`. Mutating GET rejected.
`/live` —16KiB memory ring try-lock; redact/send вне lock, не SD open/flush.
`remote-snapshot.txt=1`: existing flusher чередует закрытые a/b.log примерно каждые2s при изменениях.
Direct read tails2KiB, потому часть counters/threads в них не видна.

Freshness: `[REMOTE-SNAPSHOT] run/sequence/tick/build` плюс actual title.
run — runtime instance, не AppID/EXE hash. Старый active1 файл может остаться после exit.
Advancing sequence/tick важнее последней надписи на диске.
`REMOTE-STATE`: active/stage/total_s/stage_s/successful presents/window_frames/fatal/age.
Stage4 означает Starting program/DLL init до первого успешного кадра,
**не доказывает**, что graphics backend не загружен. The Forest уже загружал DXVK/Mono.
Presentation attempts/successful presents/cursor frames/playable — разные понятия.

Fatal kinds:1GC,2render,3DLL init,4unhandled/native exception parking,
5native FAIL,6known dialog,7fatal NTexit. err/fixme/STALL не fatal.
Fatal0 не исключает hang/OC corruption/process exit между snapshots.

Passive `performance.txt=1` сохраняет bounded96 buckets/try-lock/drops,
report≈10s, minute generations и session elapsed на всю сессию.
QPC1/1024 per-thread; NT waits/delay1/64; instrumented Vulkan calls1/1.
PERF-CALL: Wine TID hex (0 для native без WineTLS), native caller/savedWOW64EIP,
count/sum/max wallns/≥1ms/errors; optional current-thread pipeline CPU.
Wall включает conversion/driver/lock wait/descheduling; это не GPU execution time.
Native QPC sampling не измеряет весь FEX/WOW64 boundary. Saved EIP может указывать thunk.
Не складывать overlapping windows/parallel call sums в wall time и не сравнивать
sampled QPC count непосредственно с all Vulkan counts.
In-flight бесконечное ожидание не попадает в completed-call totals; cross-generation limitations остаются.
Top rows не полная histogram, actual frame-time p95/p99 ещё не реализован.

`profile.txt` intrusive2ms sampler — отдельный. Stall snapshots теперь отдельно
`stall-snapshots.txt=0` (legacy default1); passive perf сам threads не останавливает.
WAIT-PENDING сообщает16 aged rows/address/handle/expected/kind, не owner/wake graph.
MESA job_starts core counters — выборка в начале job, не residency/time distribution.
SYSTEM-CPU idle tick queries не доказаны как непрерывный полный four-core/process trace.
clkrst result0 = service read, не сам по себе proof physical maximum/OC stability.
Свободные MB heap не равны RAM bandwidth/load percent.

## GDB: что теперь умеем и как не повторить неудачу

User дал явное временное разрешение. Успешны native attach/info threads/detach
на ненагруженном Autorun, затем matched-symbol stacks зависшего native launcher.
SDK GDB **без Python**. До этого один plain capture зависал с packet errors,
после него пользователь подтвердил system HOME не отвечала и reboot.
Не смешивать этот эпизод с последующим native audio hang, где physical HOME работала.

В следующем успешном session: auto-load off, auto-solib-add off,
sysroot /debug, remotetimeout3, pagination/confirm off, hook-quit detach,
subprocess deadline25s, stop собственного GDB container при timeout.
Не опрашивать sysbot/SD observer одновременно с GDB. Не attach ко всем system processes:
stub зависит от сетевых services; это может уничтожить канал debugging.

Exact current ELF: `artifacts/global-debug-fix-20261007/runtime-passive.elf`.
PID138/base0x574ebfc000 относятся к **прошлому** запуску, после reboot изменились.
Проверять несколько SVC offsets/page alignment/осмысленные frames до интерпретации.
Первая неверная relocation была отвергнута; ложные corrupt stack reports из неё не evidence.
Native optimized unwind иногда обрывается; frame-pointer memory chain дала audio close caller.
Без guest TEB/JIT/transition correlation это не полноценный x86 stack debugger.

После attach на Home новый Play завис в SDL audio teardown до FEX;
без GDB тот же переход прошёл. Причинность GDB не установлена,
но предварительный attach нельзя включать как «безвредный обязательный health check» перед игрой.
После этой диагностики config restored/reboot; сейчас GDB не включать просто для inventory.

## Последняя аппаратная проверка и восстановление

Clean The Forest observation660.3s: fresh logs, zero read errors, active1/stage4/presents0.
После него **сам агент**: HOME/X/A closed Autorun→titleNone→Right/A launched DBI→
full FTP route→FTP connected→saved two full logs.
Journal `artifacts/global-debug-fix-20261007/clean-forest-recovery.json`.
Это acceptance нового controller pipeline, не cure The Forest.

При будущей потере связи: bounded read retries → сохранить уже полученное evidence →
если канал отвечает, known HOME/close; иначе минимальное physical action пользователя.
Не blind button loops/repeated Play, не считать timeout game failure автоматически.
Не обещать software recovery от любого GPU/kernel/system hang.
