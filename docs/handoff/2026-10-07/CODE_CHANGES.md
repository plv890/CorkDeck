# Код, поведение и границы изменений

Workspace `D:/autorun fork`, repository `D:/autorun fork/autorun`.
Ветка `feature/remote-switch-broker`, HEAD `737a2505b4f91b272f0d951d51bf3dab80fef96d`.
Перед миграцией tracked diff: 61 файл, 1655 additions / 174 deletions; есть untracked source/docs.
Это весь незакоммиченный проект, **не автоматически diff данного чата**.
Точной исходной ветки на начало чата для разделения каждого старого hunk нет.
Ниже происхождение отмечено по датированным отчётам и install journals.
Нельзя reset/checkout поверх изменений или считать только git diff --stat полным составом работы.

## 1. Планирование потоков: добавлено в данном чате

Код: `horizon-wine/source/thread_profile.c/.h`,
`dlls/ntdll/unix/horizon.c`, `horizon-wine/source/runtime.c`.
Переключатель `/switch/wine/cpu-worker-policy.txt`, применяется после полного restart.

| Policy | Смысл и результат |
| --- | --- |
| 0 / отсутствие | Прежние application cores0–2/custom balancing; legacy graphics helper core3 |
| 1 | Named DXVK shader/cache на mask8/core3/priority63; аппаратно подтверждено |
| 2 | Те же workers: maskF/preferred3/priority63; отдельного аппаратного сравнения нет |
| 3 | Автоматические Wine/native pthread maskF, обычные priorities, custom balancer выключен; подтверждены queried маски |
| 4, сейчас | Широкие Wine masks и распределённые preferred hints; named DXVK shader/cache low priority; native Mesa callback |

Режимы требуют включённого четырёхъядерного option и соответствующих process capabilities.
Явная affinity игры сохраняется. Fallback при недостаточных правах — прежняя политика.
Меньший HOS priority number означает больший приоритет;63 — фон, а не boost.

Policy4:

- Wine automatic threads допускаются на cores0–3; preferred распределяется по разрешённым cores.
- Custom balancer отключён, чтобы не сужать широкую affinity до одного ядра.
- `dxvk-shader-l`, `dxvk-shader-h`, `dxvk-cache`: preferred3/maskF/priority63.
- Graphics CS не переводится в background allowlist: обычный priority/широкая mask.
- Native Mesa util_queue получает weak callback из runtime. USE_MINIMUM_PRIORITY queues:
  preferred3/priority63; остальные — ordinary priority, preferred по index.
- Ошибка установки маски после понижения priority вызывает попытку вернуть прежний priority.
- Native workers имеют отдельную registry: не попадают в Wine suspend/wake/sampler.
- Если callback, policy4 или права отсутствуют, старая Mesa mask6 сохраняется.

Изменённый Mesa исходник — `D:/autorun fork/references/mesa-switch/src/util/u_queue.c`.
Связанные callbacks, registry, counters в thread_profile. Полная пересборка Mesa не завершилась:
в существующих SDK archives заменены **точные C objects**, см. ENVIRONMENT_AND_BUILD.
Это важно: правка reference файла сама по себе не изменяет linked SDK.

Удалено/отключено только ограничение автоматической политики при выбранном режиме;
ядро Horizon и правила остальных приложений не переписаны.
Трансляция и Windows API часто выполняются в том же игровом потоке, их нельзя целиком
«перенести на отдельное ядро» настройкой affinity. Независимые workers — можно.

Аппаратное доказательство: queried maskF/priority/Result0 и native jobs на нескольких cores.
Preferred не равен фактической residency; core3 разделяется с ОС.
Процент FPS от самой policy не доказан.

## 2. Горячий shader RAM cache: добавлено в -7

Файлы `horizon-wine/source/shader_ram_cache.c/.h`, подключение в CMake/runtime;
Mesa `references/mesa-switch/src/util/disk_cache.c`, rebuilt object `disk_cache.c.o`.
Переключатель `shader-ram-cache-mb.txt=32`;0/отсутствие выключает.

- Общий лимит32MiB,512 slots, отдельная запись≤1MiB.
- Namespace по instance cache + весь key и его длина (до32 байт), не сокращённый hash.
- Mutex/LRU stamp; get возвращает отдельную malloc-копию, принадлежащую caller.
- put и успешное SD read прогревают RAM. При alloc failure безопасный miss/fallback.
- destroy очищает namespace; повторный адрес instance не даёт старые данные.
- Существующая async persistent SD запись **сохранена**, а не заменена RAM.
- `[SHADER-RAM]` budget/bytes/hits/misses/puts/evictions.

Host ASan/UBSan проверили ownership, namespaces, LRU, byte cap, destroy,
concurrent get/put и выключенный режим.
Последний Dishonored:≈5,2MiB,418hits/14721misses,14209evictions — ограничение slots
раньше memory budget. Увеличивать только мегабайты бессмысленно;
расширение512 slots требует оценки линейного поиска/блокировок или индекса.
Не внедрены zero-copy возвращаемые pointers и RAM copy всего игрового prefix.

## 3. FEX и DXVK: настройка, а не новый транслятор

`runtime.c`: `fex-l2-cache.txt=1` выбирает `FEX_DISABLEL2CACHE=0`.
При0 отключает L2; отсутствие оставляет per-game/default. Presence check исправлен,
поскольку старый read_bool_file возвращал0 также при отсутствии файла.
Это runtime lookup L2, **не disk JIT cache**. Dynamic L1/TSO не менялись.
FEX DLL не обновлялись в накопительном эксперименте.

`dxvk-experiment.conf` дописывается после launcher/generated и per-game config:

```ini
dxvk.numCompilerThreads = 2
dxvk.enableGraphicsPipelineLibrary = True
```

Это общий эксперимент, применяемый всем подходящим запускам DXVK, поэтому последним
может переопределить пользовательские/per-game значения. Удаление experiment возвращает
прежний выбор. В Dishonored сохранён per-game контроль3 workers из policy comparison.
True оставляет GPL при поддержке driver и подавляет background optimized-pipeline jobs;
Auto/False отличаются. Link fallback может всё ещё компилировать optimized pipeline.
Снижение compiler work может ухудшить GPU эффективность/удержание памяти; это не final default.
`DXVK_ASYNC` не добавляет async fork в ordinary upstream DLL.

Изучены UnixLib MapFile slot6/24-byte ABI, FD lifetime/unmap/cache identity/SMC.
Bridge там остаётся STATUS_NOT_SUPPORTED. `FEX_DISKCACHE=1` не включён;
реальная mapping abstraction/persistent code cache не написаны.

## 4. Язык: добавлено в -4/-5

`horizon-wine/source/system_language.h`: bounded mapping18 console languages,
включая Chinese script aliases; English при неизвестном code/ошибке query.
Источник libnx `setGetSystemLanguage`, без предположения о Newlib locale.

- `dlls/ntdll/unix/env.c`, runtime: системная locale передаётся Wine до NLS init;
  system/user locale и UI используют существующие Wine NLS данные.
- `steam/steam_settings.c/.h`: default download language наследуется от консоли,
  если нет валидного explicit preference. System language — отдельный вариант UI.
- `steam/steam_ui.c`, `steam/steam_local.c/.h`: console UI язык,
  сохранение языка установки; language/ui_language разделены.
- `tools/steamworks/local.cpp`: SDK больше не возвращает English безусловно;
  validated adjacent config language/UI language.
- Изменение download concurrency сохраняет inheritance, а не превращает его в explicit English.

Assets старых установок не переписаны/не скачаны заново. Игра сможет выбрать только язык,
который поддерживается её данными и собственными правилами.
Wine LCID0x0419 подтверждён на Switch. Synthetic SDK x86/x64 Russian/German
и preparer/settings ASan/UBSan прошли; поведение каждой игры не проверено.

## 5. Quick setup и разгон: общий source fix

`horizon-wine/source/setup_boot.c` теперь распознаёт `autorun-loader.kip`
при удалении прежних Loader entries, вместе с другими известными Loader filenames.
Fixture `horizon-wine/tests/setup_boot.c` содержит конфликт из трёх названий.
Setup tests прошли; обновлённый Quick setup HOC / Preserve existing выполнен на Switch.
Это исключает повторное создание двух Loader программ при следующих setup.
Профили HOC/boot edits и сохранение CUST — отдельно в ENVIRONMENT_AND_BUILD.
Частоты/вольтаж/тайминги в код Autorun не зашиты.

## 6. Local Steamworks: совместимость SDK в данном чате

Унаследован limited local Steamworks API; добавлены exact Screenshots001/002/003
из pinned GBE ABI headers. Первое исследование ошибочно не нашло legacy headers,
последующая реализация их использовала; перенос всего GBE runtime не делался.

`tools/steamworks/generate.py`, `local.cpp`, `probe.cpp`;
`steam/steam_local.c/.h` inferred versions и migration allowlist.

- Exact versioned factories/client accessors/legacy flat exports.
- Shared hook state с mutex/reset on Shutdown.
- Unsupported capture/write/tag/library возвращают unavailable/invalid handle.
- Trigger не включает overlay, не делает capture, не создаёт фиктивный request callback.

Следующий аппаратный запуск Ninja в -2 дал c0000005 в SteamAPI_RunCallbacks.
Обнаружена отдельная ABI ошибка: Windows/MSVC order overloaded Run отличается от MinGW.
Fixed explicit vtable dispatch: slot1 ordinary callback, slot0 three-argument call-result,
x86 thiscall. Registered flags/id и unregister/shutdown cleanup согласованы;
callback registrations и call-result entries не удаляются вместе произвольно.
Foreign/MSVC-layout probes x86/x64 проходят; прежний same-compiler probe этот класс не покрывал.
SDK update сделан в -3, language — в -4. Окончательное аппаратное acceptance Ninja
на последнем coherent payload не зафиксировано: прошлый удачный старый build его не заменяет.

Настоящие auth tickets/legacy Steam.dll/client IPC/DRM backend не реализованы.
Не подменяли EXE, не маскировали fatal, не имитировали universal ownership.

## 7. Телеметрия и безопасная диагностика

`dlls/winevulkan/make_vulkan`, `vulkan_thunks.c`, `nx_performance.h`,
`horizon-wine/source/performance.c/.h`, `performance_wine.h`:
optional pipeline current-thread CPU time в дополнение к wall time.
Это позволяет отделять исполнение текущего потока от ожидания/descheduling,
но не измеряет CPU других compiler threads/GPU duration и не является full transition cost.

`thread_profile.c/.h`, `dlls/ntdll/unix/horizon.c`: bounded pending waits
по futex/server-pipe address/expected/handle/age; hooks begin/end не меняют таймауты.
Состояние не даёт владельца lock/источник wake. Репорт ограничен16 rows, не полным графом.
`SCHED-THREAD` получил kernel handle; `MESA-SCHED/MESA-WORKER` отдельные native counters.
`SYSTEM-CPU` — экспериментальные kernel idle tick queries; полный надёжный
four-core/process coverage не доказан, смотреть Result и доступность каждой выборки.

-8: `wine_nx_stall_snapshots_enabled`, file `stall-snapshots.txt=0`:
stall reporter не выполняет svcSetThreadActivity для snapshots,
а выводит passive waits. Legacy default1 сохранён при отсутствии/другом содержимом.
Это отдельно от intrusive `profile.txt` sampler и отдельно от passive `performance.txt`.

## 8. Канал управления sys-botbase

Рабочий checkout **вне repo Autorun**:
`D:/autorun fork/references/sys-botbase/sys-botbase/source`.
Overlay FS source: `autorun/tools/remote_switch/sysbot_extension/remote_files.c`.

- Новый `application_title.h`: PM application PID/program ID, без debug attach.
- `main.c getTitleID` использует helper, blank при отсутствии приложения.
- `commands.c/.h`: attach возвращает Result, сначала detach, failure handle=0;
  detach idempotent/reset0; getMetaData не читает при failed attach.
- FS команды вынесены в единственный рабочий поток. State CAS0→1→2,
  только одна job, busy отказ вместо накопления очереди.
- Job copies arguments до parser free, общий bounded storage8192/argc≤6.
- Worker формирует bounded reply8192, пишет только в свой `dup(STDOUT_FILENO)`;
  поздний ответ не уходит новому клиенту при reuse descriptor/глобальном stdout.
- FS service/mount открываются/закрываются для job. При зависшем IPC worker не убивается
  посреди владения session; основной controller loop не ждёт FS/freeze mutex.
- На accepted sockets SO_RCVTIMEO2s и добавлен SO_SNDTIMEO2s.
- Нет нового listener/port; protocol arVersion остаётся1, bot getVersion2.5.

Host production-code fixtures ASan/UBSan подтвердили PM failures, handle ownership,
blocked FS isolation, copy args/busy/correct socket. Реальный Switch прошёл concurrent
title/snapshot и HOME/close/launch DBI/FTP. Точное место прежнего sysbot timeout не снято;
исправленный FS failure mode не объявляется доказанным виновником того эпизода.

Остальные upstream memory/freeze callers не все переработаны в transactional debugger.
Не пользоваться ими как гарантированно совместимыми с активным GDB.
`sysbot_extension/prepare.py` старый: при запуске заново перепишет main.c из upstream
и **не воспроизведёт PM/title/send-timeout fixes**. Сейчас сборка использует сохранённый
исправленный reference/source целиком. Обновление воспроизводимого overlay — технический долг.

## 9. Что унаследовано и продолжает работать

До этого чата уже существовали persistent encrypted Steam session, SteamCM/Guard/QR,
library/Families/entitled Windows depots, download/resume/integrity/registration,
limited local SDK/storage/preparer, display/input/profile API fixes, diagnostic
HTTP/ring/snapshots, passive performance collector -11 и предыдущие Wine context/wait fixes.
Их полный контекст сохранён в HISTORICAL_HANDOFF; этот чат не переписал их заново.

Старые source hunks `horizon_registry_server.h`, `sync.c`, `server.c`,
`signal_arm64.c`, `thread.c`, installer/provider/UI не приписывать автоматически новым
policy/cache fixes. Проверять текущий diff и предыдущие fixtures по конкретному механизму.
Vault, saves, licenses, installation markers, explicit profiles и original DLL backups сохранены.
