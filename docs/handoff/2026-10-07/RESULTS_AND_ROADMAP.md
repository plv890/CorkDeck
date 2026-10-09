# Результаты, открытые вопросы и ближайший roadmap

Это актуальное продолжение roadmap, не список автоматически принятых настроек.
Консольные тесты в этой миграции не повторяются. Различаем user observation,
host fixture, hardware applied setting, root cause и controlled performance result.
Полный итоговый диагностический отчёт данного чата включён ниже после roadmap.

## Что было сделано по этапам

| Этап | Что получили | Что этим не доказано |
| --- | --- | --- |
|Исследование core3/GTA addon|разделили process permissions, thread masks и готовую работу; parsed IPS/NPDM matched SD/local|универсальный kernel unlock/GTA FPS gain|
|CPU policies1/3|аппаратные queried masks8/F, обычные priorities / background63|победа одной политики, одинаковый compiler count/сцена|
|Steamworks Screenshots|exact legacy/current ABI и bounded hook state|full overlay/capture/library support|
|Callback ABI|foreign MSVC vtable bug воспроизведён и исправлен synthetic x86/x64|последняя Ninja аппаратно полностью принята|
|Clocks/HOC|App PD conflict + double Loader устранены; CPU2499/EMC3000|разгон стабилен для всех workload / root cause hangs|
|Console language|Wine LCID0419, inheritance в Steam/local SDK|assets каждой игры русские автоматически|
|Policy4/Mesa|ordinary/bkg roles и native hooks, broad masks applied|exclusive core3, exact workload residency, causal FPS|
|Cumulative32MiB/L2/DXVK2/GPLTrue|installed/readback, usable Dishonored, RAM stats|это лучшая комбинация / persistent FEX disk cache|
|Passive waits/-8|pending wait ages и отключённые forced snapshots|lock owner/wake graph / guest stack|
|GDB|native attach/matched symbols/detach succeeded|full system/guest analyzer / безопасный FPS health check|
|sysbot isolation|production fixtures и реальный HOME→DBI→FTP route|точный источник каждого прежнего network timeout|

## Dishonored

Пользователь после исправления разгона сообщал прежние10–15 →20–25FPS.
После policy4 ориентировочно ещё+5FPS; после cumulative — меньше фризов,
заметно лучше стабильность, местами30+, возможное снижение среднего.
Это ценные наблюдения, но разные сцены/игровой путь/cache warmness не позволяют
превратить их в controlled среднее или causal gain каждого патча.

До cumulative были пользовательски подтверждённые hangs на778 и4636 presents;
затем отдельные счётчики807/2758 в разных сеансах. Не смешивать их в один запуск.
Main в последних сохранённых native snapshots ожидал server pipe;
RenderingThread/dxvk-cs/compiler threads — futex/alert waits.
Critical section02491ED4/owner0 warning встречался и когда frames затем росли.
По одному warning нельзя объявить lock причиной. Явного VK_ERROR_DEVICE_LOST/OOM
или fatal crash не было, это не исключает OC corruption или бесконечное ожидание.

| Retained сеанс | Медиана положительного прироста presents/с | Zero-growth intervals | Самая долгая completed pipeline |
| --- | ---: | ---: | ---: |
|Разгон до policy4|9,6|6 /60s|842ms|
|Policy4|15,4|5 /72s|274ms|
|Cumulative|19,05|2 /21s|520ms|

Intervals включают меню/loading. Это не count фризов, не p95/p99 frame time.
Последний cumulative: runtime606s/7478 presents, последний game PERF517.051s.
Часы различаются, нельзя делить эти счётчики произвольно для «среднего gameplayFPS».
Самая поздняя heap used2675/free414MiB; учитывать memory headroom других pools.

RAM cache418hits/14721misses≈2,76%;bytes5423516,puts14721/evictions14209.
512slots связывают раньше32MiB. Сначала keys/reuse/индексация/стоимость mutex;
не увеличить бюджет просто потому, что есть свободная память.

Исторический inherited late gameplay: dxvk-shader-l59.53s wall из63s window,
dxvk-cs pipeline3.53s, RenderingThread86–92%core и много QPC polling.
Это объясняет приоритет compile/cache/waits/full transition cost,
а не доказывает, что GPU/SD/FEX один является единственным bottleneck.

## The Forest: обязательно два отдельных случая

1. **Гостевой stage4:** Unity5.6.5p4/Mono/DXVK уже загрузились,
   GfxDeviceWorker был активен, первый кадр не появился. Старый log603s.
   Последний чистый test -8 без GDB и без forced snapshots:660.3s,
   fresh telemetry без read errors,0presents;66 registered threads почти0CPU,
   server-pipe/futex ожидания. Это ещё не точный deadlock owner.
2. **Native Starting game:** после GDB attach/detach на Home main ждал
   завершения SDL audio thread в ui_quit/launcher_audio_close; аудио — audrenWaitFrame.
   FEX не стартовал. Physical HOME работала. На clean reboot без preliminary GDB
   переход меню прошёл: этот случай не повторился. Причинность GDB не доказана.

Не объяснять чёрный экран автоматически дополнительным launcher/MonoGC/шейдерами/OC.
Active0/stage0 и active1/stage4 — разные этапы.
В clean guest log forced snapshot off подтверждён: эта диагностика не единственная причина.
Ни общая синхронизация The Forest, ни SDL interruptible shutdown пока не исправлены.

## Assassin’s Creed

`AssassinsCreed_Game.exe`438272B — launcher/wrapper; actual Dx9/Dx10 EXEs и
bink/eax/DirectX dependencies присутствуют на SD.
Перед `Application load error 2:0000065558` Steam.dll lookup →c0000135.
Текущий lightweight game API SDK не заменяет legacy Steam client/runtime/IPC.
SteamCM login/entitled depots ≠ запущенный клиент в игре.
Настоящий поддерживаемый client path надо оценивать отдельно, не переименовывать SDK
в Steam.dll и не обходить wrapper/лицензирование. Global fix пока не реализован.

## Ninja, Santa и другие классы

- Ninja policy3 и0 воспроизводили crash до первого кадра; дополнительные cores
  не были необходимы для этого сбоя. Missing Screenshots и callback ABI затем исправлены
  в -2/-3 с probes. -2 после Steam init дал callback crash; latest coherent hardware
  acceptance не объявлять пройденным без нового сохранённого game result.
  Сохранённый `backend-callbacks-20261007/ninja-input.jpg` показывает чёрное поле
  с DXVK HUD, а не подтверждённый playable hub; наличие HUD не заменяет acceptance.
- Long Live Santa дошла до stage4 без кадра за237s. Пользователь ждал≈3min,
  окончательного10min теста не было. Это compatibility candidate, не установленная причина.
- Balatro / ранее рабочие игры сохраняются как regression control.
  Не повторять Ninja/full suite после каждой небольшей правки; после major shared fix
  или перед release нужен meaningful контроль. Steam init не равно playable.

## Ближайшие цели по приоритету

| Приоритет | Следующая работа | Критерий результата |
| --- | --- | --- |
|P0|Сохранить working control/state, текущий пакет и matching binaries|не потерять coherent NRO/manifest/SDK, возможность HOME/DBI/recovery|
|P1|The Forest/Dishonored waits: request/reply/handle/wake correlation|конкретная операция, источник ожидаемого wake и воспроизводимый stalled transition|
|P1|Изолировать native SDL teardown при необходимости|repeat without preliminary GDB; driver loop observes shutdown, join/resources lifetime корректны|
|P2|Контролируемое Dishonored A/B/A cumulative components|одна сцена/save/cache, actual clocks, frame-time distribution и correctness|
|P2|RAM cache/index и pipeline stage/queue costs|repeat-key/hit/miss/deserialize/compile/link/lock/IO раздельно, проверенный hot-path overhead|
|P2|SDK/Ninja regression и legacy client feasibility|настоящая ABI/runtime ошибка, generic fix с target result; unsupported честно|
|P3|FEX persistent cache/Unix bridge feasibility prototype|24B ABI, FD/lifetime/unmap/version/integrity/SMC, measured benefit без RAM/startup regression|
|Параллельный прежний приоритет|Download/install throughput|разделённые receive/decode/verify/write/network costs, integrity-preserving gain|
|Далее|Runner/matrix/UI/profile automation|versioned reversible profiles, safe resume, meaningful stage status|

P0 не означает ещё одну установку «на всякий случай»: последнее управление аппаратно проверено.
Не начинать со повторного Steam login, всех downloads или «unlock core4 firmware».

### P1: раскрыть ожидания, а не добавить sleep

Существующие passive waits адресуют futex/pipe, но не связывают owner/request/wake.
Следующий scoped analyzer может хранить bounded request opcode/serial/client TID,
ответ/объект/event producer и начало/конец ожидания без per-call stdout.
Нужны lifetime/generation для переиспользуемых threads/objects, dropped counter и disable switch.
Проверять forgotten reply/lost wake/request queue/APC/context paths на synthetic fixture,
после этого на реальном stalled game. Не возвращать fake success и не менять wait timeout
ради исчезновения чёрного экрана. Диагностика не должна сама держать lock во время stop.

Guest stack через GDB — альтернативный более intrusive путь. Не attach к system services,
не использовать stale PID/base. Native stack+TEB/JIT mapping пока требуют адаптации.
Если GDB меняет наблюдаемое состояние, его результат не считать исходным FPS test.

SDL source выявляет endless event loops без shutdown checks. Исправление должно быть
в самом wait loop с bounded wait/корректным return перед join; одна wrapper audrenWaitFrame
без loop condition недостаточна. Audout branch тоже имеет UINT64_MAX wait и не гарантирует fix.
Не убивать поток и не освобождать service/pool, пока audio driver ими пользуется.

### P2: честное сравнение скорости

Сохранить current profile как baseline, затем отключать **одну** составляющую за restart.
Кандидаты: GPLAuto↔True, workers2↔3, RAM on/off/index, L2 on/off, policy4↔legacy.
Параметр сцены/cache/clocks не меняется одновременно с флагом.
Собирать p50/p95/p99/max frame times и долгие gaps, heap/IO/cache counters,
actual clocks и compiler current-thread CPU/wall. Не удалять cache пользователя ради cold test
без отдельного scoped clone/backup. Log on/off overhead измерить тем же сценарием.
OC stability контроль отдельно CPU или EMC, не одновременно весь разгон.

Система с четырьмя доступными cores не получает автоматически33%FPS.
Готовая к исполнению работа, OS конкуренция, последовательные участки и memory ordering остаются.
Свободная RAM не означает лишний bandwidth: горячие полезные записи важнее allocations.

### P3: FEX MapFile и persistent JIT

UnixLib slot6 сейчас unsupported, структура24B FD/MapSize/Result.
Нужны genuine descriptor translation, stable read-only mapping или обоснованный RAM snapshot,
unmap/close/lifetime, per-build code identity/schema, bounded storage/invalid file fallback,
SMC/page protection совместимость. PermissionSMC меняет поведение при DISKCACHE/WIP.
Whole-file RAM copy может замедлить startup; не объявлять её fastest заранее.
Улучшения newer upstream/FEX forks проверять по Windows/Horizon ABI и Cortex-A57,
не копировать Linux/Bionic binaries. TSO оставлять1, full ARM tests требуются на ARM target.

### Прежние цели download/UI/matrix не потеряны

Уже существуют multi-CURL reuse/bounded requests/writer/batching,
decode/chunk integrity/resume/final SHA/registration/verification heartbeat.
10–20MB/s — пожелание исследования; исторические network means≈1.93/2.54MiB/s,
не достигнутый новый result этого чата. Сначала matched small/large/many-small depot
и breakdown DNS/TLS/TTFB/body/decrypt/decompress/SHA/queue/SD open-sync/readback.
Installed bytes ≠ compressed network bytes, parallel cumulative ms ≠ wall percentages.
Concurrency2/4 сравнить before выше; preserve TLS/entitlement/chunk+file checks/durability/cancel/Resume.
Большой SHA buffer/metadata batching — только после измерения конкретного bottleneck.

UI: общие existing components, Nintendo/XInput labels, coherent settings/saves/manual overrides;
ясные receive/verify/install/prepare/launch/error/resume стадии, technical logs отдельно.
1280×720 display policy/inverse input mapping — inherited, regression check по scoped changes.

Matrix/runner: AppID+EXE hash+runtime/SDK/profile/cache/clocks/runtime instance/test ID,
detector engine/API/architecture с Unknown для packed/dynamic,
profile precedence/versioned apply/undo, install/init/rendered/playable различать.
Unique game identity и полный runner ещё не реализованы.

## Источники и правило reuse-first

Приоритет primary source текущей pinned версии, готовые открытые Switch решения;
исследования сообщества полезны как гипотезы, не как подтверждённый target gain.
Записать commit/license/ABI/dependencies/performance cost/rollback до переноса.
Не переносить закрытый GameNative bootstrap/Bionic/Steam binaries как open-source backend.

- FEX: https://github.com/FEX-Emu/FEX и https://fex-emu.com/Scourge-of-emulation/
- DXVK3.1.1: https://github.com/doitsujin/dxvk/blob/v3.1.1/dxvk.conf
- Mesa/NVK: https://docs.mesa3d.org/drivers/nvk.html и https://github.com/danfromtico/mesa-switch
- Atmosphere1.11.2 scheduler: https://github.com/Atmosphere-NX/Atmosphere/tree/1.11.2/libraries/libmesosphere
- HOC2.5.1: https://github.com/Horizon-OC/Horizon-OC/tree/2.5.1
- Steam API/client requirements: https://partner.steamgames.com/doc/sdk/api
- Unity5.6 arguments: https://docs.unity3d.com/560/Documentation/Manual/CommandLineArguments.html

Эти ссылки сохранены из исследования чата; миграция документов не выполняла новый web survey.

## Полный итоговый отчёт диагностики

Ниже включается сохранённый `DEBUG_AND_COMPATIBILITY_20261007.md` с его точными
evidence/ограничениями; это часть данного roadmap. Исторические отдельные reports
остаются в корне repo для detail и provenance. Пути artifacts в копии приведены к workspace.


<!-- included-final-diagnostic-report -->

### Диагностика 7 октября 2026: GDB, управление и совместимость

#### Состояние и ограничения

Native Autorun: `2026.10.07-8`, NRO SHA256
`ad3e2eff3cc0c83939f6187951e5523d4bbf9c176817b6b353e0f11599e75368`.
Параметры производительности накопительного профиля не изменены в этой диагностической сборке.
Файл `stall-snapshots.txt=0` отключает принудительную приостановку потоков встроенным анализатором зависаний; пассивные записи ожиданий остаются включены.

Новый sys-botbase установлен через DBI FTP и проверен чтением:
SHA256 `5485be29787001270af2b4e2491c48d124d04f4d49a8a9a8a0bfb4010830d62b`,
130709 байт. Пользователь перезагрузил консоль; аппаратный маршрут проверен. Резервная копия:
`/atmosphere/contents/430000000000000B/exefs.nsp.before-fs-isolation-20261007`.
Порты и разрешения не расширены.

#### GDB

С согласия пользователя Atmosphere GDB был включён на TCP 22225.
На ненагруженном Autorun PID 138 успешно выполнены attach, перечисление 9 потоков и detach.
Последующие подключения к остановившемуся переходу также завершились с подтверждённым detach.
Это подтверждает доступность отладки native процесса; полноценная раскрутка гостевого FEX/Wine стека ещё не подтверждена.

Важные параметры клиента: `set auto-load off`, `set auto-solib-add off`,
`set sysroot /debug`, локальный ELF именно этой сборки, `remotetimeout 3`,
отсечение запуска отладчика через 25 секунд и обязательное отсоединение.
В SDK GDB нет Python. Чтение SD и управление не выполнялись параллельно со снятием стека.

Для этого процесса ELF был загружен с базой `0x574ebfc000`.
Соответствие подтверждено несколькими SVC адресами и осмысленной цепочкой кадров.
Первый пробный вывод с неверной базой был отвергнут и заменён корректным;
сообщения о якобы повреждённых стеках из него не являются доказательством повреждения памяти.
В корректном выводе обычный unwind всё ещё обрывается в оптимизированном main-потоке;
сохранённая цепочка frame pointers позволила восстановить вызовы отдельно.
Адресная база применима только к данному запуску.

Исходный `system_settings.ini` восстановлен и проверен чтением после снятия стеков.
Пользователь затем перезагрузил консоль с восстановленным файлом.
TCP подключение к 22225 после этого не установилось (timeout); это подтверждает
недоступность отладчика с ПК, но не отдельно состояние listener.

#### Новая остановка The Forest: до гостевой игры

После предварительного attach/detach на Home и одного Play меню показывало
`Starting game…`. Полное окно наблюдения 660 секунд не обнаружило запуска гостя:
active=0, stage=0, зарегистрированных Wine потоков 0, presents=0.
Дальнейший сохранённый runtime log продолжал показывать 0 кадров более 20 минут.

Корректно символизированные native стеки и frame-pointer цепочка:

```
main -> wine_nx_launcher_run -> ui_quit -> launcher_audio_close
     -> SDL close_audio_device -> SDL_WaitThread -> pthread_join
     -> threadWaitForExit -> svcWaitSynchronization

SDL audio thread -> SDL_RunAudio -> SWITCHAUDIO_PlayDevice
                 -> audrenWaitFrame -> eventWait -> svcWaitSynchronization
```

Главный поток ждёт аудиопоток меню, который ждёт событие AudioRenderer.
В libnx `audrenWaitFrame` использует бесконечное ожидание события:
https://github.com/switchbrew/libnx/blob/master/nx/source/services/audren.c

Это локализует остановку перехода, но не объясняет, почему перестало приходить событие.
Возможное влияние предварительной остановки через GDB требует чистого запуска.
Нельзя приписывать эту остановку Mono, компиляции шейдеров The Forest или разгону.
Пользователь подтвердил физическую HOME, закрыв Autorun и открыв DBI FTP.
Касание и изменение яркости сами по себе не проверяют HOME.

Предыдущий сохранённый TheForest.log описывает **другую** остановку:
Mono и DXVK уже загружены, GfxDeviceWorker работал, stage=4, первый present не наступил
за 603 секунды. Main ждёт server pipe, часть работников — futex.
Нет подтверждённого OOM или конкретной причины этого гостевого ожидания.
Новая остановка до гостя не проверяет, исправлен ли предыдущий случай.

#### Восстановление управления

Команда закрытия не прошла проверку активного title: getTitleID не ответил за 5 секунд,
HOME ещё не отправлялась. После ручного закрытия probe снова ответил за 0,183 секунды
и определил DBI `03DB117EE17B6000`.
Точное место блокировки sys-botbase в этом эпизоде не было снято.

Обнаружены и исправлены общие проблемы канала:

* getTitleID теперь обращается к PM, не использует debug attach для обычной навигации.
* Ошибка attach обрабатывается до чтения метаданных; закрытый handle обнуляется.
* Файловые операции вынесены из главного цикла команд и freeze mutex в единственный FS worker.
  Допускается одна операция; новые файловые запросы получают `ERR busy`, очередь не растёт.
* Аргументы копируются до освобождения parser buffer.
  Worker владеет дубликатом сокета; поздний ответ не попадёт в другое соединение или stdout.
* Ответ ограничен 8192 байтами, отправка сокета имеет тайм-аут 2 секунды.
  При зависшем FS worker не отменяется посреди service IPC; команды управления продолжают работать.

Тест с реальным pthread и намеренно заблокированным run_files, использующий production
enqueue/worker код, прошёл ASan/UBSan: контроль отвечает, busy отказ работает,
освобождение аргументов не ломает job, поздний ответ доставляется исходному клиенту.
Сборка Switch прошла. Прежние тесты PM-only title и debug handle также прошли ASan/UBSan.
После перезагрузки probe на HOME успешно завершился за 0,295 секунды.
Маршрут Right/A запустил Autorun; проверка title и чтение закрытого SD snapshot
одновременно успешно завершились (4101 символ ответа snapshot).
Один Play запустил The Forest. После окна 660 секунд удалённо выполнены
HOME/X/A, проверено отсутствие активного title, Right/A запустили DBI,
Down x12/A/Down x2/A открыли FTP Browse SD. Соединение с FTP и полные
логи подтверждают маршрут без ручного вмешательства.
Защита от блокировки FS не доказывает, что именно FS был причиной сегодняшнего тайм-аута.

#### Dishonored: что показывают сохранённые логи

Данные из `D:/autorun fork/artifacts/global-debug-fix-20261007/comparison.json`.
Интервалы считаются по приросту presents между progress записями; это не запись frame times.

| Сеанс | Медиана положительных интервалов, кадров/с | Интервалы без новых presents | Самая долгая завершённая компиляция pipeline |
| --- | ---: | ---: | ---: |
| Разгон до четырёхъядерного изменения | 9,6 | 6 / суммарно 60 с | 842 мс |
| Четырёхъядерная политика | 15,4 | 5 / суммарно 72 с | 274 мс |
| Накопительный профиль | 19,05 | 2 / суммарно 21 с | 520 мс |

Сцены, время в меню, состояние кэша и текущие частоты различаются.
Поэтому таблица показывает обнадёживающую тенденцию, а не причинный процент ускорения.
Интервалы без presents включают возможные меню/загрузки; они не равны числу фризов.
Самая долгая компиляция — только среди завершённых вызовов, не p95/p99.

Последний сеанс: 7478 presents при runtime progress 606 с;
последняя игровая PERF запись 517,051 с. Эти два таймера нельзя смешивать.
Явной fatal ошибки не найдено. Отсутствие fatal не исключает зависание или ошибку разгона.
Субъективные наблюдения пользователя о меньшем числе фризов согласуются с частью данных,
но прогрев persistent shader cache также мог помочь.

Горячий RAM shader cache: около 5,2 MiB, 418 hits / 14721 misses,
14721 puts / 14209 evictions. Hit rate около 2,76%.
При лимите 32 MiB сейчас раньше срабатывает лимит 512 slots.
Простое увеличение мегабайт не решает этот предел; перед расширением slots нужна
более дешёвая индексация, чтобы не увеличить линейные поиски на горячем пути.
Для объективного A/B нужны одинаковая сцена, прогретый кэш, одинаковые частоты
и распределение frame times. Существующие логи такого сравнения не обеспечивают.

#### Assassin’s Creed

Выбран `AssassinsCreed_Game.exe`, небольшой 438272-байтный launcher/wrapper.
В логе динамическая загрузка Steam.dll заканчивается `c0000135` (DLL not found),
после чего выводится `Application load error 2:0000065558`.
На SD присутствуют Dx9/Dx10 executables, bink, eax и DirectX DLLs.
Этот экземпляр ошибки указывает на зависимость старого Steam launcher от runtime клиента,
а не на нехватку CPU или доказанный сбой рендеринга.

Текущая Steam авторизация/загрузка и lightweight Steamworks SDK не обеспечивают
совместимость с legacy Steam.dll и полноценным клиентом. Нужен поддерживаемый
настоящий runtime клиента и его IPC; одной подменой имени DLL это не исправить.
Исполняемые файлы игры не менялись, обход wrapper или лицензирования не применялся.
Официальные исходные требования:
https://partner.steamgames.com/doc/sdk/api
https://partner.steamgames.com/doc/features/drm

#### Чистое аппаратное измерение завершено

Наблюдение `tools/remote_switch/runs/20261007T191619Z-observe-85ee443b`:
полное окно 660 секунд, без нажатий/скриншотов и предварительного attach GDB.
Переход меню прошёл, но stage4 не дал первого present за полное окно.
Снимки обновлялись, ошибок чтения не было. Затем выполнен описанный выше
удалённый маршрут закрытия/DBI/FTP.

Новые полные логи: `D:/autorun fork/artifacts/global-debug-fix-20261007/clean-TheForest.log`
(150528 исходных байт), `clean-autorun_runtime.log` (157054 байта).
Recovery journal: `clean-forest-recovery.json`.
Лог подтверждает `[STALL] paused snapshots disabled`, 66 зарегистрированных потоков
с практически нулевой загрузкой после начальной активности. Долгие ожидания
server pipe и futex сохраняются. Это исключает приостановку анализатором как
единственную причину воспроизведённого гостевого зависания.
Записи ожиданий не содержат владельца/wake источника и точного гостевого caller,
поэтому не устанавливают конкретную взаимоблокировку. Общий fix The Forest пока не найден.

Дальнейшая диагностика: связать запрос/ответ сервера, объект ожидания и wake источник;
или снять гостевой стек после старта с контролируемым риском GDB, сохранив пассивный
канал управления. Изолированное изменение DXVK/Unity graphics API может проверить
графический путь, но не должно объявляться общим fix по одному успешному запуску.
Если native остановка звука повторится без GDB, исправлять отмену ожидания
точного SDL backend. Не закрывать service/память, пока аудиопоток их использует.

#### Дополнительные исходники и проверяемые гипотезы

На чистом запуске после перезагрузки без GDB переход меню прошёл, active=1/stage=4
появились уже к первому snapshot. Предыдущая остановка audio teardown не повторилась.
Это не доказывает причинность GDB и не исправляет прежний гостевой случай.

Официальные SDL ветки найдены через git ls-remote:
`switch-sdl-2.28` @ `0738d3c9f6993875e2f3dd0e8cc0bb4ae4440b4e`,
`switch-sdl-2.28-audout` @ `371ccb8dad5a274e01eabd863cd42d75ce89db54`.
В первой PlayDevice ждёт состояния буфера через audrenWaitFrame в цикле,
не проверяя shutdown. Во второй используется audoutWaitPlayFinish с UINT64_MAX.
Поэтому простая замена renderer на audout не гарантирует устранения зависания при закрытии.
Проверяемый будущий fix: ограниченное ожидание события и проверка shutdown **внутри**
цикла драйвера, с корректным выходом перед join и освобождением ресурсов.
Обёртка одного audrenWaitFrame без изменения цикла недостаточна.
Исходники сохранены в artifacts/global-debug-fix-20261007, изменения SDL не установлены.

В FEX issue #2548 описано сходное ожидание DX11/futex на Android, но там старые
FEX 2303/Wine 7/DXVK 1.10.3; предложенный multiblock уже включён у нас.
https://github.com/FEX-Emu/FEX/issues/2548
Совпадение симптомов не доказывает общую причину.
Новый Mono report #5991 касается native Linux/Godot, не данного ARM64EC Unity 5.6 runtime:
https://github.com/FEX-Emu/FEX/issues/5991
Не переносить флаги из Linux отчётов без проверки Windows/Horizon пути.

Лог The Forest определяет Unity 5.6.5p4. Официальная документация Unity 5.6
подтверждает диагностический выбор D3D9/D3D11 для Windows Standalone:
https://docs.unity3d.com/560/Documentation/Manual/CommandLineArguments.html
`-force-gfx-direct` на этой странице указан в секции Windows Store;
его применимость к нашему Standalone ещё не подтверждена.
Изменение graphics API может локализовать проблему, но не является общим исправлением
синхронизации. Никакие такие аргументы пока не установлены.
