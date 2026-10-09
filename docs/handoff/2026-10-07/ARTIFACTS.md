# Карта документов, артефактов и доказательств

Все пути ниже относительны `D:/autorun fork`, кроме явно указанных Linux/SD paths.
Это карта чтения, не команда запуска старых installers.
Не публиковать owned game binaries/private logs/keys/QR/account screens из artifacts.

## Основной набор следующего чата

- `NEXT_CHAT_REMOTE_DEBUG.md` — точка входа с финальным state/приоритетами.
- `autorun/docs/handoff/2026-10-07/` — тематические отчёты, state JSON,
  hashes/source snapshot и полный исторический handoff.
- `artifacts/handoffs/NEXT_CHAT_REMOTE_DEBUG_before_migration_20261007.md`
  — неизменённая исходная копия до переписывания root handoff.
- Прежний ещё более старый архив `artifacts/handoffs/NEXT_CHAT_REMOTE_DEBUG_20261005.md`.

## Этапы этого чата

| Каталог artifacts | Содержимое / основные evidence |
| --- | --- |
|cpu-scheduling-20261007|mode0–3 NRO/ELF/journal, addon-analysis.json, sd-addon-and-policy.json, final-policy.json|
|backend-research-20261007|fresh-metadata.json, Screenshots NULL probes, primary source metadata|
|backend-implementation-20261007|Screenshots -2 NRO/ELF/installation; не успешная Ninja acceptance|
|backend-callbacks-20261007|callback ABI -3 runtime.nro/.elf, installation.json, ninja-input.jpg|
|pipeline-cpu-20261007|optional pipeline thread CPU timing build/fixtures|
|clock-language-20261007|runtime -4 ELF/NRO, -5 NRO, two install journals, HOC/Hekate before/after/KIPs, language defaults, stall decoded|
|four-core-20261007|policy4 -6 ELF/NRO, sdk-patch.json/build/installation, native Mesa scheduling evidence|
|cumulative-20261007|RAM-cache -7 ELF/NRO, SDK object patch/build, installation and GDB config before/candidate/backup, last52-thread symbols|
|post-cumulative-20261007|первые полные последние Dishonored/TheForest/AC/runtime logs, collection/analysis, Forest symbols/stall details|
|global-debug-fix-20261007|native -8 + current sysbot, all safety fixtures/GDB/clean Forest/comparison/recovery journals|

`artifacts/steamworks-minimal`: current steam_api.dll467968B и steam_api64.dll443904B,
build.json/source hashes, notices/licenses, generated fixtures/probes и dated results.
Последние payload hashes — ENVIRONMENT_AND_BUILD/current inventory.
Результат чужого ABI callback: `check-20261006T223522975547Z/results.json`.
Проба Screenshots: `check-20261006T222402173307Z/results.json`.
UTC в имени test не противоречит Tbilisi7 октября.

## Последний debugger/control checkpoint

В `artifacts/global-debug-fix-20261007`:

| Файл | Что означает |
| --- | --- |
|runtime-passive.nro/.elf|current -8 и exact symbols|
|native-build.json/.log|native build result|
|native-installation.json|readback -8/stall-snapshot toggle, before reboot lifecycle|
|sys-botbase.nsp/.elf|current FS-isolated sysmodule + matching debug ELF|
|previous.nsp|старый extended sysbot de124371...|
|previous-safe-title.nsp|intermediate cbff8090...|
|installation.json|PM-only intermediate install, **не current FS isolation**|
|fs-installation.json|current5485be... readback, subsequent reboot documented in recovery|
|gdb-check.gdb/.py/.txt/.json|успешный short idle attach/list/detach|
|gdb-handoff.*|correct final relocated native stacks/frame-pointer words|
|gdb-restoration.json / restore-gdb.py|исходный config restored, reboot subsequently выполнен|
|autorun_runtime.log|36328B native Starting game/audio case, **не clean guest log**|
|clean-TheForest.log|latest guest stage4; source150528B, redacted saved150459B|
|clean-autorun_runtime.log|latest runtime157054B|
|clean-forest-recovery.json|agent-only HOME/close/DBI/FTP/full-log route succeeded|
|comparison.json / compare.py|Dishonored aggregate interval comparison, caveats|
|title-test.c / attach-test.py/.c|PM/helper/debug handle host fixtures|
|test-files-worker.py/.c|production enqueue/worker with deliberately blocked FS, ASan/UBSan|
|SDL_switchaudio-*.c|official researched audren/audout source, **не installed patches**|
|AC-launcher.exe|owned438272B wrapper copy for static analysis; не запускать/публиковать|

Некоторые builder outputs в одном artifact каталоге обновлялись следующей sysbot сборкой;
current build.log/.nsp относятся к FS worker, не автоматически PM-only прошлому этапу.
Ранние hashes/journals/previous binaries сохраняют историю.
Hardcoded PID/base в gdb scripts были для прошлого process138; не rerun на новом PID.
finish-forest.py включает control input и collection, не read-only универсальная проверка.
install.py/prepare-native.py/install-files.py уже исполнены — новые transaction scripts
создавать по currentstate, не bypass их oldhash/unusedbackup assertions.

## Точные log runs, которые не нужно получать повторно

Все `runs/` ниже — `autorun/tools/remote_switch/runs/`.

| Run / log | Значение |
| --- | --- |
|20261006T215348Z-bf17ea97/Dishonored.log|policy1 core3-only workers|
|20261006T220248Z-979041f5/Dishonored.log|policy3 ordinary-priority broad masks|
|20261006T220717Z-23986c3c/10SNX.log|Ninja policy3 crash|
|20261006T220917Z-b266df9b/10SNX.log|Ninja policy0 crash|
|20261006T222843Z-da7cb447/10SNX.log|Ninja -2 SteamAPI_RunCallbacks crash|
|20261007T131922Z-observe-de2ba4f5|manual user OC test, log-only|
|20261007T132432Z-observe-c73abce9|second user OC launch|
|20261007T133555Z-b2293b54/Dishonored.log|oc_before comparison / 4636 final user hang|
|20261007T150056Z-observe-bd4fbae7|policy4 launch/first ready result|
|20261007T152001Z-9bf6ca67/Dishonored.log|policy4 full retained comparison,2758 frames|
|20261007T185040Z-observe-08393ab8|native Starting game active0/stage0 after GDB|
|20261007T191619Z-observe-85ee443b|clean -8 guest660.3s, active1/stage4/0presents, no read errors|

Предыдущий full Dishonored cumulative — `artifacts/post-cumulative-20261007/Dishonored.log`.
Historical inherited late gameplay — `artifacts/dishonored-gameplay/Dishonored.log`
и performance-windows.json. Не сравнивать эти logs как одинаковую save/route.
Old observations могут содержать stale snapshots после app exit; saved full exit log важнее.

## Исходники и reference

- `autorun/horizon-wine/source`: runtime/thread_profile/shader_ram_cache/setup_boot,
  diagnostics/steam/performance. Основные fixtures — `autorun/horizon-wine/tests`.
- `autorun/dlls/ntdll/unix`: Horizon threads/sync/context/server/env.
- `autorun/dlls/winevulkan`: generated thunks/performance instrumentation.
- `autorun/tools/steamworks`: local.cpp/generate.py/probe.cpp/build.py/check.py/pe.py.
- `autorun/tools/remote_switch`: controller/transport/navigation/observe/direct_files/HTTP/FTP.
- `references/mesa-switch`: pinned Mesa u_queue.c/disk_cache.c changes.
- `references/sys-botbase/sys-botbase/source`: current installed module source;
  не находится внутри git Autorun, сохранить отдельно при переносе машины.
- `references/gbe_fork`: pinned ABI headers/licenses, не full runtime install.

## GTA addon исследование

User provided local Download paths и разрешил чтение matching SD files:
IPS `5F526EBA66851A11D6B9687C86B340BB8BE5C8C1.ips` (129B),
NPDM (1604B). Exact source paths и eight IPS records в addon-analysis.json;
SD matching в sd-addon-and-policy.json. Hashes:
IPS43678134c2046a17c6325fc80eab0402cd47036ab92e85ee87dde775399e59c9;
NPDMbb4e5775535c280e546891be4e145e55d354cf5ef6b35cc9f0aea3a522bf2079.
Parsed permission0x030073b7 cores0–3/priority28–59; наш0x030073f7 cores0–3/28–63.
Instructions consistent with Worker3Only масками8/7, но original main/symbols
нет для complete caller/control-flow. Не переносили IPS/NPDM/GTA EXE в Autorun,
никакого универсального «модуля скорости» оттуда не извлечено.

## Тематическая история в корне repo

CPU_SCHEDULING_RESEARCH/EXPERIMENT, BACKEND_RESEARCH/FRESH_RESEARCH/IMPLEMENTATION,
CLOCK_AND_LANGUAGE, DISHONORED_INVESTIGATION/OC_HANG, FOUR_CORE_RESEARCH/EXPERIMENT,
CUMULATIVE_OPTIMIZATION, POST_CUMULATIVE_RESULTS, DEBUG_AND_COMPATIBILITY,
OPTIMIZATION_ROADMAP — все suffix20261007.md.
Старые начала документов отражают этап: policy0/HOME/GDB-pending не current truth.
Источники и отрицательные результаты там сохраняются, не стирать неудобные failures.

Inherited `STEAM.md`, `STEAMWORKS_COMPATIBILITY.md`,
`STEAMWORKS_OPEN_SOURCE_RESEARCH.md`, `COMPATIBILITY_TEST_20261006.md`,
`DOWNLOAD_TEST_20261006.md`, `PERFORMANCE_DIAGNOSTICS.md`,
`DISHONORED_GAMEPLAY_PERFORMANCE_20261006.md`, `BUILDING.md` читать по scope.
Не перечитывать всю Wine/Mesa codebase и все reports в каждом новом чате.

`artifacts/handoffs/handoff-validation-20261007.json` — результат проверки пакета:
локальные ссылки, JSON, десять artifact hashes, state/SDK consistency,
включение полного отчёта и совпадение root handoff с сохранённой новой копией.
Эта проверка не отправляла никаких команд Switch и не повторяла hardware tests.

## Проверки и их границы

Этот пакет сохранил уже выполненные результаты, не выдаёт их за новые reruns:

- CPU/thread_profile и RAM cache fixtures ASan/UBSan passed.
- setup_boot/HOC recognizer, console language/preparer/settings fixtures passed.
- SDK owned x86/x64 availability/hook/callback/storage/ownership probes passed,
  foreign/MSVC callback layouts покрыты после исходного пропуска.
- PM title, debug handle и FS blocked worker production-code fixtures ASan/UBSan passed.
- Remote navigation4 и remote25 unit tests прошли в последнем этапе (29 всего).
- Native/sysbot build и install readbacks passed; hardware masks/language/clocks/control отдельно.
- FEX unaligned fixture blocked missing checkout, Clang SMC sanitizer failure не объяснён;
  GCC ASan/UBSan SMC rerun passed. Full ARM core acceptance не объявлялась.
- Настоящее coherent latest Ninja acceptance/controlled FPS/OC stability ещё pending.

Сохранить licenses/notices при packaging, не включать private run data/keys в commit.
