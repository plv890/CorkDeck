# Архив прежней передачи контекста

Это неизменённая копия прежнего handoff перед миграцией 7 октября. В ней сосуществуют обновление 7 октября и историческое состояние 6 октября. Старые версии, состояния консоли и формулировки о непроверенном GDB не считать актуальными. Текущее состояние — в README.md, CURRENT_STATE.json и основном NEXT_CHAT_REMOTE_DEBUG.md. Документ не создаёт новых разрешений.

---

# Задача нового чата: совместимость, производительность и быстрые загрузки Autorun на настоящем Switch

## Обновление 7 октября 2026: актуальная диагностика

Подробное актуальное состояние: `autorun/DEBUG_AND_COMPATIBILITY_20261007.md`.
Датированные пункты ниже описывают прежнюю передачу контекста; новые install journals имеют приоритет.

Native установлен `2026.10.07-8`, hash `ad3e2eff3cc0c83939f6187951e5523d4bbf9c176817b6b353e0f11599e75368`.
Накопительный профиль: CPU workers policy4 (core0–3), shader RAM32MiB/512slots,
FEX L2enabled, DXVKcompilerThreads2/GPLTrue; TSO остаётся1, disk JIT cache не реализован.
`stall-snapshots.txt=0`: пассивная диагностика без SVC приостановки потоков.
Matching ELF: `artifacts/global-debug-fix-20261007/runtime-passive.elf`.

Sys-botbase после установленного и перезагруженного исправления:
`5485be29787001270af2b4e2491c48d124d04f4d49a8a9a8a0bfb4010830d62b`.
Title lookup теперь PM-only; failed debug handle очищается/проверяется.
SD операции выполняет отдельный single-flight worker: зависший FS IPC не блокирует
кнопки, аргументы копируются, ответ владеет своим dup socket; SO_SNDTIMEO2s.
ASan/UBSan fixture с заблокированным FS passed; console concurrent title/snapshot passed.
Backup `/atmosphere/contents/430000000000000B/exefs.nsp.before-fs-isolation-20261007`.

GDB был повторно включён с прямого согласия пользователя; короткий attach/detach
и native stacks успешно получены на Autorun PID138. SDK GDB без Python:
auto-load off, auto-solib-add off, sysroot /debug, bounded timeout, mandatory detach.
Сразу после GDB на Home следующая попытка The Forest остановилась на Starting game,
до FEX. Native main ждал SDL audio thread в launcher_audio_close;
audio thread — audrenWaitFrame. Физическая HOME в этом эпизоде работала.
Эта native остановка отличается от прежнего The Forest stage4 внутри гостя.

Исходный system_settings.ini восстановлен; пользователь уже перезагрузил консоль.
Чистый запуск без предварительного GDB прошёл переход меню и начал The Forest,
stage4 без первого present за полное пассивное окно 660s, без ошибок чтения.
Удалённо успешно выполнены HOME/X/A → закрытие Autorun → Right/A → DBI →
Down x12/A/Down x2/A → FTP Browse SD. Полные новые логи сохранены:
`artifacts/global-debug-fix-20261007/clean-TheForest.log`, `clean-autorun_runtime.log`.
66 потоков практически не используют CPU, пассивные server-pipe/futex waits сохраняются.
Сейчас консоль оставлена в DBI FTP. Общая причина guest stage4 пока не установлена.
Не объявлять причинность GDB или нестабильность разгона доказанными.
Assassin's Creed wrapper требует отсутствующий legacy Steam.dll/client runtime;
Steamworks SDK/SteamCM не заменяют настоящий клиент, EXE/auth bypass не применялись.
Сравнение Dishonored разных сеансов обнадёживает, но не является контролируемым FPS A/B.


Продолжи существующий проект. Постоянный автоматический Steam login и минимальное локальное Steamworks-окружение уже реализованы. Следующие цели: общие исправления запуска, производительность Wine/FEX/графического слоя, существенное ускорение загрузки и установки, единый UI. Не начинай настройку и исследования Steam-сессии заново. Цикл: измерение → воспроизведение → исправление общего механизма → локальная проверка → аппаратная проверка → evidence/rollback.

Передача контекста: **6 октября 2026 года, Asia/Tbilisi**. Документ передаёт состояние и реальные решения пользователя, не создаёт новых разрешений сам по себе. Новые сообщения пользователя имеют приоритет. Старые датированные разделы других файлов не переопределяют свежие install journals и аппаратные факты.

## Проект и последнее проверенное состояние

- Workspace `D:\autorun fork`; repo `D:\autorun fork\autorun`; upstream https://github.com/autorunhq/autorun . Ветка `feature/remote-switch-broker`, последний проверенный HEAD `737a2505b4f91b272f0d951d51bf3dab80fef96d`.
- **Много незакоммиченных изменений** Steam/session, compatibility, local SDK, display и diagnostics. Не reset/checkout поверх них. Сначала status/diff и применимые AGENTS.md; в последнем поиске AGENTS.md не найден.
- Target: Horizon homebrew/Tegra X1, Wine + FEX/Box64 + Mesa/NVK + DXVK/VKD3D. OpenGL через Mesa nvc0 (Balatro), Vulkan через NVK (Dishonored D3D9 → DXVK → Vulkan). Не менять платформу/весь stack без конкретного основания.
- Installed native **2026.10.06-11**, **40 563 768 bytes**, SHA-256 **`cfcfb85852363ba44799d77d01fdc23d3d40eee9f51d7f7af71dc802ac581dd7`**. SD `/switch/wine/wine-nx-runtime.nro`, `/switch/wine/build-manifest.json`.
- PC `artifacts/performance-continuous/runtime-performance.nro`, matching `.elf`, `installation.json`, `validation.json`, `captured-performance.json`. Readback verified. Hardware: 660 s Dishonored, меню, 4544 successful presents на game second656, fatal=0, collector после10min active. Это acceptance диагностики/запуска, не benchmark overhead или доказательство30FPS.
- Backup -10 `.before-performance-continuous-20261006`; backup -9 `.before-performance-20261006`; более ранние сохранены. **-10 содержит ошибку profiler; для отката диагностики предпочтительнее -9**. Откат согласованного NRO/manifest с учётом performance.txt.
- Manifest pins: Wine `8770f0a8d241a06b11397cff04d6c159f39eb349`, Box64 `2f130fab1d6e1a4ee8a71dc60cfdfcc839ad192a`, Mesa-switch `d4a00ea0ab3f59afb967cc5d779e4263d237bd77`; log DXVK3.1.1. `nx-amd64-fex-2640` — runtime build string, не diagnostic revision/достаточное доказательство версии FEX.
- Последний checkpoint после чтения gameplay log: **HOME, active title None, DBI закрыт, FTP5000 закрыт**. Перед вводом дешёво проверить title/reachability; это не гарантия текущего экрана. Старые snapshots могут оставаться active=1 после exit.

## Авторизация и принципы пользователя

Разрешены самостоятельные owned installs/tests/fixes, **автоматическая установка готовых сборок без повторных вопросов**, временный DBI FTP, приоритетное закрытие/открытие приложений даже при действиях пользователя. Если он отдельно просит наблюдать ручной тест без управления, соблюдать этот режим до нового разрешения.

Локальная PC репродукция разрешена для относящихся к проекту ошибок, когда ускоряет диагностику: собственные synthetic PE/ABI/thread/filesystem/crypto/profiler samples. **Игровые EXE на PC не запускались и не запускать**; imports/disassembly для анализа применялись.

- Общие исправления для всех игр либо подтверждённых engine/API/architecture/driver patterns, не title-specific EXE patches. Работающие игры тоже получают улучшения.
- Уже работающую Ninja проверять после **мажорных общих изменений**, перед release checkpoint или при конкретной regression; не после каждой сборки.
- Steamworks предельно лёгкий, без полного клиента/overlay/браузера/сетевого gameplay. Cloud вторичен. Не обещать настоящий DRM/auth backend.
- Performance logs **всю игровую сессию**, не пять минут; bounded memory/частота, overhead измерить.
- По последнему отзыву download1–2MB/s слишком медленный; **10–20MB/s — желаемый ориентир исследования, не измеренный потолок**.
- UI единый/понятный; сохранять settings/saves/manual overrides.

### Главный приоритет удалённого управления: эффективность токенов и действий

1. Управлять как конечным автоматом: известное состояние + запомненные кнопки/counts/waits + дешёвый title/result check. Не распознавать каждый шаг по картинке заново.
2. Known routes не требуют новых screenshots перед каждой кнопкой/каждым batch. Использовать последнее подтверждённое состояние и ожидаемый selection. Screenshot — unknown state, mismatch/error или нужная visual acceptance.
3. Пакетные sequences, release/detach finally, guard перед переходами, обновление состояния после успеха.
4. **Прямое переключение приложений:** пользователь сообщил HOME → выбрать другое → запуск → подтвердить системный запрос закрыть текущее. Предпочесть это отдельному HOME/X/A close и отдельному launch, когда цель именно switch. Точный selection/default подтверждения пока не оформлен проверенным helper: не выдумывать fixed кнопки. Один раз проверить при необходимости, затем переиспользовать без repeated screenshots.
5. Explicit close нужен для HOME без приложения, полного restart test или deploy. При switch-to-DBI перед NRO replacement убедиться, что Autorun действительно завершён.
6. Recents меняются: хранить порядок после собственного последнего launch, не абсолютные индексы навсегда. Title None на HOME нормален.
7. Broker может механически сохранять JPEG: **не открывать/emit без необходимости**. Known state — Controller/navigation.sequence без лишнего capture. Экономить image tokens и сетевой/GPU traffic.
8. Guard mismatch → остановить очередь, один targeted check, исправить модель. Token efficiency не означает продолжать известный неверный маршрут.

### Главный принцип разработки: сначала готовое проверенное решение

Пользователь отдельно попросил **приоритет готовым открытым функциям/модулям**, особенно Nintendo Switch GitHub projects, перед написанием с нуля.

- Сначала искать в existing repo/dependencies/local references, затем primary upstream Switch/homebrew/Wine/FEX/Mesa projects. Не создавать дубль уже рабочего backend.
- Найденное решение оценивать по Horizon/ARM/native ABI, API/service availability, ресурсам/threads/listeners, memory/performance, maintenance и возможности отключения.
- Фиксировать URL, commit/version, лицензию и attribution/notices. Проверять разрешённость переноса/связки; открытый UI не означает открытый native backend.
- Prefer небольшой переиспользуемый component/патч. GitHub success/Android/Linux working не равно Switch acceptance: local fixtures и target tests сохраняются.
- Если пригодного решения нет или адаптация тяжелее/опаснее небольшой реализации, написать своё с кратким обоснованием.
- Не переносить закрытый GameNative bootstrap/Steam binaries/Android Bionic libraries. Не устанавливать набор sysmodules ради одной функции и не обновлять весь stack в процессе reuse.

## Реализовано: постоянная Steam-сессия

Официальные CM authentication, QR/Guard, библиотека/Steam Families, licensed Windows depots/CDN entitlement, Library/Installed/Downloads и registration работают. Пользователь выполнил QR login; около617 доступных игр подтверждено ранее. Не просить новый login без server rejection.

Код `horizon-wine/source/steam/{steam_session.c,steam_crypto.c,steam_cm.c}`, provider/UI; актуальные STEAM.md sections:

- Automatic on this console: encrypted **vault v2**, AES-256-GCM/fresh salt+nonce, SPL device-unique KEK option1/fixed generation4, два domain-separated CMAC → ephemeral key. Key/passphrase рядом на SD не сохраняется; boot-sealed handles не persist.
- Account/SteamID/refresh token encrypted. Password/Guard/access token не сохраняются. Нет plaintext/software fallback при derive failure.
- Привязка к **консоли**, не изоляция от другого privileged homebrew на ней; не secure enclave приложения.
- v1/passphrase отдельный, без silent conversion; atomic `.tmp`/`.previous`, Logout cleanup.
- Startup existing CM worker restore. Online только после matching server logon/nonzero session, не наличие vault.
- Transport break:3 retries/waits2/4/8s. Rejection/malformed/local decrypt failure прекращают; exhaustion → settings reconnect/restart, не indefinite monitor.
- Resume staging сохраняется; interrupted installs автоматически не replay. Logout local deletion ≠ server revocation.

**Hardware:** full HOME/X/A close → green authenticated OK без QR/Guard/passphrase, repeated NRO updates сохранили vault. Synthetic crypto/session/CM/UI passed. **Power reboot, expiry/revocation и все game-return/network-break варианты не считать fully hardware validated.** Возвращаться при auth changes, основную login-задачу не начинать заново.

Артефакты `artifacts/steam-persistent-session`, `artifacts/download-diagnostics`. Старые STEAM.md «RAM-only/manual unlock/no saved session» — история.

## Реализовано: минимальное локальное Steamworks-окружение

Собственный ограниченный game-facing API слой с ABI/layout reference из pinned GBE headers. **Не полный GBE executable и не штатный Steam client.** CM/download и game-facing APIs разные.

- Source `tools/steamworks/{local.cpp,generate.py,build.py,probe.cpp,check.py,pe.py}`; `references/gbe_fork` pin `7103add7ca6ef3ef8353279219880068f2494fc8`.
- Generator preprocess Windows SDK отдельно x86/x64, exact versioned vtables/packing/calling convention. Raw C++ regex был недостаточен из-за macros/legacy slots.
- Init/account/AppID, доступные coherent User/Apps/Utils/Friends/context APIs, callbacks/local RemoteStorage. Unsupported unavailable/default; **не full SDK compatibility**.
- Нет своих network workers/full client/overlay/voice/browser. Offline BLoggedOn; ownership bound current account/AppID, не universal license. Нет реальных tickets/Denuvo/Steam DRM backend.
- Adjacent `autorun-steam.ini`: public binding/inferred legacy versions, не refresh token. Local saves `autorun-steam-local/<SteamID>/<AppID>`, leaf/traversal/ADS guards, quota64MiB/file16MiB, temp+FlushFileBuffers+atomic replacement/recovery; cloud ack не имитируется.
- Global SD `/switch/wine/steamworks-local/`: DLLs/build.json/licenses.
  - x86 steam_api.dll464896bytes, SHA `8e9087f2156aa7397e70b12cf87204123e72b4d18448e77e06dacaa3d7b49331`.
  - x64 steam_api64.dll440832bytes, SHA `d7bbb6abee6796742267cff9ed515b548864347e3700c60f8c337082c526c02e`.
- Readback `artifacts/steamworks-minimal/component-installation.json`; x86/x64 own probes `check-20261006T161336784158Z/results.json`: ABI/init/callback/storage/ownership paths passed, не все APIs.

Generic GUI preparer `steam/steam_local.c/h`, `launcher.c:prepare_program`:

1. Trusted `autorun-steam-install.ini` ancestor search≤8, install marker/AppID/owner.
2. Только adjacent SDK matching EXE architecture.
3. Preserve `.autorun-original`, infer legacy versions из original, config ready before launch.
4. PE/pinned hash, `.autorun-part`/readback/recovery/idempotence; allowlist previous own payloads, unknown mods/symlinks/conflicts не overwrite.
5. Missing global component → log MISSING/original остаётся; остальные conflict/IO → safe launch error.

Balatro launch и Dishonored menu/gameplay подтверждены. Nested SDK/другие entrypoints не все покрыты, unknown interfaces/achievements/locale не полноценны. Latest per-game target hashes отдельно после auto-upgrade не перепроверены: **global component readback ≠ target readback**.

SDK changes синхронно generator/runtime/probes/component hashes/native pins/previous allowlist. Preserve originals/saves. `package-amd64.py --steamworks-local <PATH>` включает source/licenses/component, end-to-end packaging pending. One-shot deploy_test.py со старыми hashes не rerun. Research docs имеют historical «ещё не реализовано» checkpoints, current code/evidence важнее.

## Общая совместимость и игровые результаты

- WOW64 access/suspend/resume/genuine safe-point snapshots при ordinary waits, capture lifetime/absence guards; NtGetNextThread skips terminated retained objects. Не fake success/zero guest state.
- Default ProfilesDirectory/primary-token read support исправил ранний Balatro LÖVE filesystem blocker, затем local SDK Steam init.
- **FEX_TSOENABLED=1 сохранять**: controlled Dishonored A/B устранил один render crash, затем отдельно NULL SteamRemoteStorage. Не TSO=0 наугад.
- -9 display max1280×720, oversized/stale reset, default EmulateModeset/Wine scale+inverse input, explicit overrides сохраняются. Source tests passed; **Balatro hardware resolution/input alignment полностью pending**.
- Exact normal/delayed Steam SDK advisory; official VC++v14 downloads/hashes/probes есть, MSI/child-process/prerequisite milestone на Switch fully pending. Не ставить всё подряд.

| Игра | Доказанная стадия | Осталось |
|---|---|---|
| 10 Second Ninja X | Hub/input -7, point36–60FPS | Не retested -11; major control |
| Balatro | OpenGL/LÖVE launch с local SDK | Resolution/input/saves, не full playthrough |
| Dishonored RHCP | -11 menu; пользователь gameplay/menu~20 | Pipeline/translation/polling/cache stalls;30FPS unproven |
| Catlateral Damage | Старый GC/GetThreadContext; later wait fix убрал прежний marker | Black/stall unresolved |
| Beat Hazard | BASS.dll found/init aborted/c0000005 | Audio/init general cause; не Beat Saber |
| Long Live Santa! | Старый600s black/no presents/heartbeat alive | Stall unknown; WineD3D trial без gain/profile restored |

Dishonored сначала может несколько минут показывать курсор до окна/меню. **Без concrete fatal ждать≥10min**. Black/stale snapshots не crash. Нативные GTA/Dying Light порты/год игры не гарантируют FPS через stack. First frame/menu/gameplay/playable различать.

## Консоль и способы связи

OLED/Horizon21.1.0/Ultra3.0/Atmosphère `1.11.2-master-b918458b3-dirty`, trusted LAN **192.168.70.59**. Boot **Hekate → Launch → Autorun**, специальный low-address kernel/loader. `/atmosphere/kips/autorun-loader.kip`, kip1 только `[Autorun]`, backup `hekate_ipl.ini.before-autorun-loader-20261005`, PC autorun-profile-repair.json. Stock может Address space unavailable. Firmware/kernel/loader/autoboot automatically не менять.

HOME forwarder **0548EABB35576000** (39-bit), DBI **03DB117EE17B6000**. Требовательные игры не Album/applet.

| Канал | Возможности | Ограничения |
|---|---|---|
| TCP6000 sys-botbase2.5/direct-SD v1 | Title/input/screenshot/closed snapshots | Independent Autorun; unauthenticated LAN/tails2KiB |
| TCP8181 in-app LAN | status/files/tail/live/perf | Temporary enabled service/current X-Autorun-Key/expires exit |
| TCP5000 DBI foreground FTP Browse SD | Logs/NRO/component update | Autorun closed/DBI active/temporary/scoped bounds+hash+backup |
| Background5001/GDB22225 | Старые experiments | **Disabled/not working**, wlan2001-0132/0x10801 KernelOutOfResource; не reenable blindly |
| Hekate Mass Storage | SD/recovery | Worked/physical stage/verify disk/safe eject |
| Horizon USB DBI/NSLoader | Альтернатива | Здесь не работал, не считать готовым |

Новые sysmodules для profiler не нужны. Full debugger/recovery от любого kernel/GPU hang не принят. Specific blocker → closed report/маленькая probe/matching ELF, не boot-loop. No public forwarding/raw RAM dumps/arbitrary memory writes/cheats.

### PC tools и запомненные маршруты

PowerShell cwd `D:\autorun fork\autorun`, Python **F:\anaconda3\python.exe**. «Failed to find real location» бывает при success, смотреть exit/result. Modules `tools/remote_switch/{broker,transport,controller,navigation,direct_files,diagnostics,credentials,observe,performance,performance_data,sample_download}.py`.

```powershell
& 'F:\anaconda3\python.exe' tools/remote_switch/broker.py --host 192.168.70.59 --timeout 15 probe
# Только unknown state / visual evidence, не перед каждой кнопкой
& 'F:\anaconda3\python.exe' tools/remote_switch/broker.py --host 192.168.70.59 --timeout 15 screenshot
& 'F:\anaconda3\python.exe' tools/remote_switch/broker.py --host 192.168.70.59 direct-logs --live
# Known selected Autorun on HOME, actual title None
& 'F:\anaconda3\python.exe' tools/remote_switch/broker.py --host 192.168.70.59 click A --ready --expected-title none
& 'F:\anaconda3\python.exe' tools/remote_switch/broker.py --host 192.168.70.59 click A --layout xinput --ready --expected-title 0548EABB35576000
# Если нужно explicit close, а не direct switch
& 'F:\anaconda3\python.exe' tools/remote_switch/broker.py --host 192.168.70.59 navigate close-title --expected-title 0548EABB35576000 --start-confirmed
# Known DBI main first row
& 'F:\anaconda3\python.exe' tools/remote_switch/broker.py --host 192.168.70.59 navigate dbi-ftp-sd --expected-title 03DB117EE17B6000 --start-confirmed
# Read-only, запускает игру другой шаг; --game только label
& 'F:\anaconda3\python.exe' tools/remote_switch/observe.py --host 192.168.70.59 --game 'Dishonored-test' --seconds 660 --interval 15 --source direct --no-screenshots
# DBI FTP уже active; game closed; whole cap512KiB
& 'F:\anaconda3\python.exe' tools/remote_switch/broker.py --host 192.168.70.59 ftp-logs --ftp-port 5000 --file Dishonored.log --whole
```

- HOME suspend; explicit close **home-primed→X→A→wait2s→titleNone**. home-primed UNUSED/wait/HOME same controller, detach finally. Не повторять explicit close когда direct switch достаточно.
- navigation.sequence UNUSED+1s, .75s после A/B/MINUS, .25s directional, guard перед кнопкой/detach finally. click_ready attach1s/press.2s/release. Known sequence без лишнего capture preferred.
- Fresh DBI905-ru **main first row**: **Down×12→A→Down×2→A**, ждать8s доFTP/IP. **B** stops FTP, verify refusal/timeout5000.
- В том же DBI послеB FTP row selected: reopen **A→Down×2→A**. **После полного close/relaunch shortcut неверен: снова first row**. Прежняя ошибка открыла SD browser; B/B→main/full route исправили.
- HOME после close Autorun DBI часто second/RightA, после close DBI Autorun second/RightA. Использовать known last recents, не вечные индексы. Direct launch+system close prompt может сократить route.
- Last fresh -11 launcher Dishonored first→A. Library reorder помнить.
- Xbox A→NintendoB, B→A, X→Y, Y→X; launcher/HOME Nintendo.
- Steam settings от нужного launcher state: **MINUS→Right→Down×3→A**. Known route не распознавать по каждому screenshot. LAN diagnostics там.

Broker action.json/JPEG в ignored runs/<UTC-id>; не публиковать account/QR/Wi-Fi password/key screens. Installed direct autorun_runtime.log path ещё не updated, использовать direct-logs --live. Staging `/switch/autorun-remote-test/probe.nro`≤2MiB, не runtime uploader.

ftp-logs --whole512KiB cap, bigger пропускает; обычный tail16KiB. Latest full494592bytes scoped/redacted cap8MiB/offset0. Для большего явный bound/tail, не вся SD.

## Live logging и непрерывный profiler

Документы PERFORMANCE_DIAGNOSTICS.md/REMOTE_DEBUGGING.md/tools/remote_switch/LIVE_LOGS.md.

- /live current16KiB memory ring try-lock, redact/send outside lock, no SD open/flush. remote-snapshot.txt=1 existing flusher alternating closed logs/remote-snapshot-a.log/-b.log~2s on changes. TCP6000 reads tail2KiB; full saved logs для истории. 0/missing disables next start.
- REMOTE-SNAPSHOT run/sequence/tick/build = runtime instance, не AppID/test. Freshness advancing tick/sequence+actual title. Old active1 послеexit возможен.
- REMOTE-STATE stages/total/stage_s/successful presents/window frames/fatal/age. Preparation→translator→DLL→guest→graphics→successful presentation. Attempts/success/cursor/playable не равны.
- Fatal1GC/2render/3DLLinit/4unhandled including strict native EXC parking/5nativeFAIL/6known dialog/7fatalNTexit. err/fixme/stall не fatal, fatal0 не исключает все failures.
- Observer read-only/title guard/recovery120s default/no-screenshots. «Три timeout=crash» неверно; temporary init outage/stale/fatal различать. Unknown при insufficient evidence.

Passive -11 source/performance.c/h, performance_wine.h:

- **performance.txt=1 всю session**, fixed96 buckets/try-lock/drops/report~10s/minute generations/session elapsed. Remote stop/game exit stops.
- QPC1/1024 per-thread, NTmultiwait/delay1/64, instrumented Vulkan submit/present/fence/acquire/graphics+compute pipelines all x86/x64 thunks. Existing THREADS CPU/core.
- PERF-CALL kind/hexWineTid(0native безWineTLS)/native caller/savedWOW64EIP/count/sum/max wallns/≥1ms/errors. **Wall**, не CPU/GPU timestamps; QPC native не весь FEX/WOW64 boundary; savedEIP может thunk.
- PERF-CLOCK read-only clkrst CPU/GPU/EMC: result0=service response, **не proof physical overclock/governor**. Values service/monitor расходились (76.8/1785 vs614.4/~1588). RAM% monitor может bandwidth/load, не free allocation.
- Top8/deterministic sampling/drops: не sum overlapping workers/не compare sampledQPC vs allVulkan percentages. In-flight invisible доreturn, minute-crossing calls могут теряться. Next instrumentation limitation, не full tracing.
- Старый profile.txt sampler separately pauses threads every2ms; passive collector не делает. Stall watcher собственные snapshots. Intrusive sampler только по конкретной причине.
- -10 NULL WineTEB nativeQPC worker/FAR0x1488/ELF RVA0xf6300. **-11 nullable UnixTLS+identity capture beforehandler**, native fixture passed. Collector defect, не game/hardware limitation.
- Controlled overhead/FPS A/B pending, не zero-cost promise. Matching ELF/image mapping для symbols; ARM stack не guest root cause.

HTTP existing8181 GET/status/files/tail/live/perf, **POST/perf/start/stop**, current key/mutating GET rejected/startcontinuous. Enabled service required; afterrestart old key invalid.

```powershell
& 'F:\anaconda3\python.exe' tools/remote_switch/broker.py --host 192.168.70.59 --lan-key-file '<current DPAPI file>' live-logs
& 'F:\anaconda3\python.exe' tools/remote_switch/performance.py --host 192.168.70.59 --lan-key-file '<current DPAPI file>' --output '<report.txt>' read
& 'F:\anaconda3\python.exe' tools/remote_switch/sample_download.py --host 192.168.70.59 --lan-key-file '<current DPAPI file>' --output '<benchmark dir>' --seconds 600
```

credentials.py DPAPI for Windows user. Old artifacts/remote-switch-setup/http-live-key.dpapi — storage path, не validcurrent key. No plaintext CLI/reports. Network pause/resume beforePlay keepskey, exit нет. Debugkey≠Steam token. performance_data.py/observer numericJSON.

## Последние Dishonored performance факты

DISHONORED_PERFORMANCE_20261006.md, DISHONORED_GAMEPLAY_PERFORMANCE_20261006.md, PERFORMANCE_DIAGNOSTICS.md; full artifacts/dishonored-gameplay/Dishonored.log + performance-windows.json.

- -11 run000001b2932c2d82, observe20261006T173320Z-observe-d0b9940c: presentation~121s, menu after11min point17.4FPS. User gameplay/menu~20.
- Extended25min/25generations; approximate presents600–1000s17.43/s,1100–1300s11.81/s,1300–1535s4.20/s. No exact menu→level/input marker, не controlledFPS.
- Pipeline max4.21s. Late age1429.9s/window63s: **tid4c=Wine76=dxvk-shader-l**80calls/59.53s wall/max3.16s, следующий388calls/55.22s.
- **tid48=Wine72=dxvk-cs** pipelinecall **3.53s** — critical candidate, не толькоbackground.
- RenderingThread86–92% одногоcore/total1.4–1.7cores; sampled NtQPC8–11M estimated/progress. Full boundary/polling site unknown.
- Late heapfree442MB/sectionfailures0, immediateOOM unproven; учитывать другие limits. Cache/SD writes substantial, cumulativeIO неwall sum.
- GPL advertised, already3 compilerthreads, Mesa memory/diskcache source есть; не «cache disabled»/«просто большеthreads». Reference≠linkedSDK flags proof.

Первая performance задача: hit/miss/bounded shader+pipeline identities и lookup/deserialize/compile/link-wait/cacheIO отдельно. Unique new shaders vs повторная дорогая работа, убрать доказанные задержки из dxvk-cs/bounded reuse. Затем rendering QPC/full translation boundary и NVK/Maxwell GPL/async profiles. Preserve correctness/monotonicity/APCs/TSO;30FPS unproven.

## Roadmap по приоритетам

### Этап A. Общие ошибки запуска и совместимость

Работать над thread/context/sync, process creation/MSI, SDK interfaces/callbacks/storage/locale/entrypoints, filesystem/security/profile APIs, audio/media и graphics initialization. Приоритет общего воспроизводимого механизма над числом проверенных игр.

Порядок: fresh EXE/build/profile/run → quiet logs сразу → startup минимум десять минут без concrete fatal → поиск готового решения → synthetic PC reproduction, когда полезно → минимальный общий fix с positive/negative tests → hardware candidate → working/manual EXE control после major changes → matrix/evidence/rollback. Не скрывать fatal и не возвращать фиктивный success.

Local SDK — первый рабочий этап, не полный Steam client. Dependencies milestone доказать на Switch прежде чем расширять. Cat/Beat/Santa использовать как представителей классов; не скачивать случайные DLL и не делать EXE patches.

### Этап B. Производительность трансляции, графики и frame pacing

Начать с pipeline/cache и критического dxvk-cs. По необходимости добавить cache keys/hits, native stage costs, полную стоимость переходов, in-flight waits/frame gaps и correlation с CPU/core/actual clocks. Не включать unlimited per-call stdout. Искать существующие Switch/Mesa/FEX инструменты перед новой реализацией.

Compare одинаковые cold/warm launches и сцены, сохраняя cache/data. Compiler flags и cache identity проверять по SDK records. GPL/async comparison — общий NVK/Maxwell capability profile с проверкой корректности; не менять renderer только ради названия Vulkan.

QPC fast path или замена доказанного polling — после cost/caller measurement, с сохранением monotonicity/frequency/epoch, waits/APCs и TSO. Один существенный параметр за раз; сопоставимые settings/cache/clocks, frame pacing и graphics correctness. Отдельно измерить logging on/off overhead.

### Этап C. Существенное ускорение download/install

Высокий отдельный приоритет: не откладывать навсегда за игровыми патчами. Пользователь сейчас видит 1–2 MB/s и хочет исследовать 10–20 MB/s. Исторические пики 9 MB/s не считать устойчивой скоростью.

Уже есть CURL multi reuse, bounded queue/writer/batching, decode+integrity, Resume/final SHA/history и 500 ms verification heartbeat. Compressed network bytes не равны installed/verified bytes. Progress только после digest match; verify rate=0 с последующим reset window.

`DOWNLOAD_TEST_20261006.md`, build 2026.10.05-4, четыре concurrent requests:

- 12 Is Better Than 6: 1,273,018,898 installed bytes / 652,430,352 network bytes / 322.463 s; resumed staging; average network 1.93 MiB/s по whole wall time.
- Balatro fresh: 92,337,448 installed / 85,304,414 network / 32.082 s; 2.54 MiB/s; write+sync 2.244 s, decode 10.930 s, final verify 4.443 s. Только запись на SD не объясняет задержку.
- Пять reused connections на 716/105 requests, четыре active requests достигались; integrity retries=0, error=0, HTTP=200. Parallel cumulative counters не wall-time percentages. -1 heartbeat улучшил visibility, не доказал speed gain; transfer на -11 ещё не benchmarked.

Практический план:

1. Matched fresh small owned depot, большие файлы и many-small-files на одном SD/network. Compressed receive и verified install throughput отдельно; без screenshot traffic.
2. Разделить DNS/connect/TLS/first-byte/body, CPU decrypt/decompress/chunk SHA, queue scheduling/backpressure, SD open/metadata/write/sync/readback, final SHA/resume scan/registration. Current decode_ms агрегирует несколько стадий.
3. Compare concurrency 2/4; выше только при доказанном headroom/benefit. CDN host/reuse aggregate без signed URLs/tokens. Отделить Wi-Fi/CDN/TLS/CPU.
4. Использовать готовые проверенные Switch/network/crypto/storage components, где подходят. Synthetic PC decode/hash/writer models, затем target ISA/crypto и optimized paths с эквивалентной корректностью. PC gain не proof на Tegra.
5. Scoped scratch SD/network probes с bounds/cleanup: sequential buffers и мелкие файлы, не raw disk benchmark/overwrite userdata. Реальный потолок поможет проверить ориентир 10–20 MB/s.
6. Proven metadata/sync bottleneck → batching/coalescing; decode/SHA → CPU/scheduling; CDN → host strategy. Whole-file SHA buffer 32 KiB сравнить с большими. Повторные проходы исключать только с доказательством equivalent integrity.
7. Сохранить TLS/entitlement/chunk+file checks/durability/atomic old installation. Cancel после stop workers чистит staging; network break сохраняет Resume. Peak rate не end-to-end speed.

### Этап D. Единый UI

Развивать existing launcher/Steam screens, использовать общие UI components, не второй backend/mass cosmetic rewrite. Shared spacing/fonts/colors/buttons/back/focus для Library/Installed/Downloads/Settings/Dependencies. Понятные receive/decode/verify/install/prepare/launch/pause/error/resume stages; сохранять manual settings и saves.

Общий display policy 1280×720 и inverse input mapping: проверить Balatro после 960×540/manual override. Nintendo/XInput labels не путать. Technical diagnostics отдельным доступным экраном/export, обычный flow понятный. Loading indicator не должен reinitialize software console поверх framebuffer: раньше это abort-ило процесс.

### Этап E. Профили, runner и local matrix

Detector PE/imports/engine/API/version с Unknown для packed/dynamic cases. Profile priority: AppID+build/runtime → engine/API/architecture/driver → conservative defaults. Versioned diff/apply/undo/manual settings. Install/Steam init не означает Playable.

Runner на существующих modules: select/install/launch → immediate logs → startup → gameplay → report → close/checkpoint. Bounded recovery, release finally, resume без duplicate installs. Связать AppID/EXE hash/runtime/SDK/profile/cache/clocks/runtime instance/PC test ID; unique game identity ещё задача.

Local matrix сначала. Public telemetry/database — отдельное согласие. Community ARM/Proton/Android — dated references, не Horizon proof. Не удалять пользовательские данные ради места.

## Сборка, deploy и проверка

Готовая среда: **WSL AvatarLab**, mirror **/opt/autorun-build**, Docker **autorun/switch-dev:2026.09.28.4-fixed**, mount **/work**. Windows authoritative, mirror не synced automatically. Копировать changed headers/generated sources/CMake явно; не переустанавливать среду.

```sh
# Inside Docker /work
cmake --build horizon-wine/build-switch-amd64 --target wine-nx-runtime-nro -j4
# Local SDK inside WSL
python3 '/mnt/d/autorun fork/autorun/tools/steamworks/build.py'
```

Wrapper `artifacts/dishonored-analysis/build-current.py`, full `build-current.log`. Native mirror outputs `horizon-wine/build-switch-amd64/wine-nx-runtime.nro/.elf` копировать в новый dated folder. Diagnostic header `source/steam/steam_diagnostics.h`: currently 2026100611 / 2026.10.06-11.

SDK uses pinned GBE / LLVM-MinGW20260505 / static O2 x86+x64. Windows `tools/steamworks/check.py` запускает только собственные probes и проверяет source/payload hashes/imports. Vulkan hooks менять согласованно: `dlls/winevulkan/make_vulkan`, `vulkan_thunks.c`, `nx_performance.h`.

Deploy: known state/title → close Autorun или switch to DBI с подтверждённым termination → FTP → assert old NRO/manifest hash → backup → temp stage → readback SHA → atomic coherent replacement → final hash/rollback journal → B/port refusal → forwarder → candidate observation. Не писать active NRO/DLL/metadata; сохранить vault/games/prefix/cache/settings.

**`artifacts/performance-continuous/install-runtime.py` уже исполнен, expected old=-10 и backups заняты: не rerun.** Для next update current hash=-11 выше, новые revision/suffix/artifact/journal. Другие old scripts тоже one-shot. Update ZIP не clean full install; optional SDK packaging end-to-end pending.

Tests по изменённому механизму:

- `horizon-wine/tests/check-performance-host.sh`: ASan/UBSan sampling/timing/capacity/concurrency/expiry/continuous/native NULL TLS/accelerated rotation/final report passed.
- `check-lan-log-host.sh`: auth/redaction/bounds/restart/old routes/start-read-stop passed; `tests/fault_watch.c` native marker positive/negative passed.
- `check_horizon_wait_context.py`, `check_horizon_suspend.py`, `check_wow64_thread_context.py`, `check_shared_cpu_context.py` и thread/TLS/handle/input/user-profile fixtures: latest results, не historical failure автоматически.
- `check_horizon_display_modes.py`, `steam_local_host.c` passed; host checks не hardware acceptance.
- Из `tools/remote_switch`: `python -m unittest test_performance_data test_observe` — 7 passed. Navigation/remote/direct tests при changes; Steam host/UI/CDN/shell suites по scope.
- ASan/UBSan в WSL доступен; старое «ASan не работал» не актуально для этих fixtures. Native -9/-10/-11 built; -10 negative collector hardware, -11 corrected/validated.

Checkpoint: source/diff/hashes/matching ELF/component licenses/install readback/rollback/hardware duration+limitations. Small logical commits, без ignored keys/owned binaries/private logs, без reset existing diff. Не full suite и не Ninja после каждой мелочи.

## Документы и первый результат следующего чата

Reading order относительно repo:

1. Этот handoff, current git/installation journals.
2. `REMOTE_DEBUGGING.md`, `PERFORMANCE_DIAGNOSTICS.md`, `tools/remote_switch/LIVE_LOGS.md`.
3. `DISHONORED_GAMEPLAY_PERFORMANCE_20261006.md`, `DISHONORED_PERFORMANCE_20261006.md`.
4. `DOWNLOAD_TEST_20261006.md`, актуальные верхние sections `STEAM.md`.
5. `STEAMWORKS_COMPATIBILITY.md`, `STEAMWORKS_OPEN_SOURCE_RESEARCH.md`, `COMPATIBILITY_TEST_20261006.md`: history + latest code/evidence, не old headline как current truth.
6. `BUILDING.md`, точные source/fixtures изменяемого механизма, не весь Wine/Mesa.

Owned official depots, без fake licenses/DRM bypass; local SDK не full auth backend. Проверять licenses/notices/reference code, не переносить closed Android binaries. Password/Guard/tokens/cookies/headers/signed URLs не логировать/публиковать. DPAPI separate; arbitrary stdout redaction не абсолютная гарантия.

При kernel/GPU/network hang могут пропасть все software channels: bounded retry → evidence → минимальное physical action. Не blind loops/firmware changes. Реальный auto-review rejection сообщать конкретно, не обходить; уже имеющееся разрешение не спрашивать повторно без основания.

**Первый ожидаемый результат:** дешёво восстановить known console state, затем конкретно исследовать late pipeline/cache/compile/wait или исправить general startup bug, с reuse-first подходом. Отдельное high-priority направление — downloader breakdown и первый integrity-preserving speed experiment. Не restart Steam login/SDK survey, не сотни installs/repeated screenshots. Общая compatibility/performance, существенный download gain и единый UI с evidence.

Scheduled/night jobs или formal goal только по отдельной просьбе с реальным механизмом. Не обещать работу после конца чата. Различать observed facts/hypotheses/host checks/hardware acceptance. Original handoff archived `artifacts/handoffs/NEXT_CHAT_REMOTE_DEBUG_20261005.md`.

## P.S. Микроисследование Dishonored и документация backend

Дополнение по просьбе пользователя: дальнейшая работа уже затрагивает backend. **Перед изменением механизма читать официальные документацию, release notes, исходники и tests соответствующего проекта.** Ищем существующие решения авторов и Switch-портов, не изобретаем настройки/оптимизации по названию параметра. Это дополняет reuse-first правило выше.

### Какие официальные источники брать на вооружение

| Слой | Primary sources | Что искать для нашей задачи |
|---|---|---|
| FEX: x86/x64 → ARM, Wine backend | [FEX repository и docs](https://github.com/FEX-Emu/FEX), [официальный сайт](https://fex-emu.com/), [release notes](https://github.com/FEX-Emu/FEX/releases), [разбор стоимости memory ordering](https://fex-emu.com/Scourge-of-emulation/) | JIT/ABI/thunks, code cache, guest/native transitions, atomics/alignment/TSO, доступные CPU features |
| Box64, когда именно этот backend задействован | [Upstream Box64](https://github.com/ptitSeb/box64) | Dynarec/native wrappers/memory ordering, реальные counters и options нашей revision |
| Wine/WOW64 | [Wine upstream](https://gitlab.winehq.org/wine/wine), локальные dlls/ntdll и dlls/wow64 | Thread context, waits/APCs, QPC units/epoch, callback ABI, host/guest ownership |
| DXVK: D3D9/10/11 → Vulkan | [README версии 3.1.1](https://github.com/doitsujin/dxvk/blob/v3.1.1/README.md), [upstream releases](https://github.com/doitsujin/dxvk/releases), [developer guidelines](https://github.com/doitsujin/dxvk/wiki/Developer-guidelines) | Pipeline libraries/worker compilation/cache lifetime/synchronization. Guidelines частично о D3D11: не переносить их механически на D3D9 |
| Mesa/NVK | [NVK documentation](https://docs.mesa3d.org/drivers/nvk.html), [environment/cache documentation](https://docs.mesa3d.org/envvars.html), local references/mesa-switch | NAK/NIR compilation, pipeline linking/cache keys/hit-miss/deserialization, driver flags и Horizon IO/locking |
| VKD3D-Proton для D3D12 игр | [Upstream VKD3D-Proton README/source](https://github.com/HansKristian-Work/vkd3d-proton) | D3D12 pipeline/cache/resource/synchronization; Dishonored D3D9 через него не идёт |

Документацию использовать выборочно по измеренному пути. FEX/Box64 переводят CPU instructions; DXVK/VKD3D переводят graphics APIs; NVK — driver/compiler; Wine обеспечивает Windows APIs. Не приписывать задержки одного слоя другому только потому, что весь процесс называется эмуляцией.

Сначала определить **реально linked version/fork/build flags** из manifest, build journal и source. Current upstream docs могут описывать возможности, отсутствующие в pinned Horizon port. Dishonored использует DXVK GPLAsync-вариант: upstream DXVK — основа, fork-specific behavior дополнительно сверять в его исходниках. Wine GitLab может блокировать web fetch; использовать локальные исходники и проверенный upstream mirror, не случайные пересказы.

Linux/Android setup instructions не выполнять на Horizon. Новые ARM ISA extensions не включать, если target CPU их не поддерживает. Работающие upstream оптимизации переносить небольшими проверяемыми patches с license/commit/evidence, не автоматическим обновлением всего stack.

### Перспективные причины, проверки и возможный выигрыш

**1. Компиляция/связывание pipeline на критическом пути — самый сильный текущий след.**

Локальный log: late compiler worker провёл59.53s из63s внутри pipeline calls; dxvk-cs отдельно задержан3.53s. Измерение уже в native Wine/Vulkan thunk и ниже, поэтому дорогой участок не обязательно является стоимостью самой x86 instruction translation. Но call time включает conversion, compile, link, waits и descheduling, а не только CPU работу компилятора.

DXVK объясняет, что GPL переносит часть подготовки шейдеров раньше, однако игры Unreal Engine, загружающие shader непосредственно перед draw, могут сохранять stutter. Поэтому долгое ожидание в меню не гарантирует прогрев будущего уровня. [DXVK 3.1.1: Graphics Pipeline Library](https://github.com/doitsujin/dxvk/blob/v3.1.1/README.md).

Проверить отдельные stages и кто ждёт готовый результат; убрать подтверждённую работу/ожидание из dxvk-cs, сохранять корректность изображения. **Выигрыш:** прежде всего устранение многосекундных stalls и улучшение frame pacing, не автоматические30FPS.

**2. Недостаточное повторное использование кэша — вероятная, но не доказанная причина.**

Кэш уже существует и записывается. Надо отличить новые shader/state combinations от повторного создания одинакового результата, memory eviction, disk miss или дорогого deserialize. Записывать bounded identities/hits/misses и сравнить одинаковый игровой участок после прогрева/повторного запуска.

Mesa предоставляет `MESA_SHADER_CACHE_SHOW_STATS` для hit/miss статистики; upstream печатает её при завершении. Наличие/поведение в нашей сборке проверить, а forced HOME close может потерять этот вывод. Single-file Fossilize cache имеет собственные ограничения: не предполагать, что обычный max-size option ограничивает его. [Mesa cache options](https://docs.mesa3d.org/envvars.html).

Исторический DXVK2.0 release отдельно предупреждал о pipeline lifetime/memory поведении32-bit игр. Это полезная гипотеза для нашей32-bit Dishonored, **не доказательство идентичного поведения DXVK3.1.1/fork**: сверить current implementation. [Историческая release note](https://github.com/doitsujin/dxvk/releases/tag/v2.0).

**Выигрыш:** меньше повторной компиляции, более быстрые повторные запуски/переходы. Ограниченный reuse/LRU предпочтительнее удержания всего: late heap free442MB, cache growth нельзя игнорировать.

**3. Активное ожидание и стоимость guest/WOW64/native перехода — второй основной путь.**

RenderingThread86–92% core и множество sampled QPC calls согласуются с polling, но могут быть нормальным engine timing. Требуется конкретный caller и полная цена transition, не только native counter function. Проверить readiness polling, timer call bridge и реальную полезную работу между опросами.

**Выигрыш:** разгрузка критического CPU потока и возможный рост устойчивого FPS. Общий fast path/корректное ожидание после доказательства; не добавлять sleep или менять frequency/epoch/APCs произвольно.

**4. Сериализация worker/cache/IO — объяснение простаивающих ресурсов, которое нужно проверить.**

Уже есть три DXVK compiler threads. Больше threads не гарантирует parallel work: можно ждать один результат/lock или SD cache. Разделить lock wait, queue latency, disk read/write и compile CPU time; определить, где другие ядра могли бы выполнять полезную работу.

**Выигрыш:** более ранняя подача команд GPU, меньше stalls, эффективнее использование ядер. SD write batching только после attribution; cumulative IO counters нельзя складывать в wall time.

**5. JIT/atomics/TSO оптимизации — следующий, более требовательный этап.**

FEX действительно развивает JIT/thunks и memory-ordering optimizations, но эффект зависит от ARM hardware features и конкретного workload. Современные ARM результаты не гарантируют того же на Tegra. Официальный разбор memory model и release notes здесь обязательны. [FEX memory-ordering analysis](https://fex-emu.com/Scourge-of-emulation/).

Искать hot guest blocks/transitions и переносимые готовые fixes. Сохранять TSO=1: у нас это уже устраняло конкретный crash. Unsafe-region shortcuts не применять без доказательства корректности. **Выигрыш:** общий CPU throughput, но размер прироста заранее неизвестен и эта работа не устраняет сама по себе native pipeline stalls.

### Минимальный следующий эксперимент и критерий прогресса

Повторить **один и тот же участок** с одинаковыми settings/build/clocks, отдельно первый и прогретый проход. Нужны cache identity/hits, pipeline stage durations, dxvk-cs waits, RenderingThread caller/full transition cost и frame time. Учесть collector overhead и ограничения minute generations/in-flight calls.

Если прогретый проход резко лучше — cache/compile path приоритетен. Если compiler успокоился, а RenderingThread остаётся насыщен — исследовать CPU/translation/polling. Если measured GPU execution становится пределом — graphics workload/driver/аппаратные ограничения. Это способы различить причины, не заранее выбранный виновник.

Для стабильных30FPS обычный кадр должен укладываться примерно в33.3ms. Удаление секундных зависаний и достижение такого steady-state budget — разные результаты. Текущий log делает расследование рациональным, но не доказывает ни30FPS, ни физический предел консоли. Исправления искать в общих слоях, чтобы выигрывали другие игры.
