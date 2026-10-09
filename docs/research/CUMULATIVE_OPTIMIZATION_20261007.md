# Общий эксперимент и диагностика — 2026.10.07-7

<!-- migration-checkpoint-20261007 -->

Актуальное состояние и итоговые результаты: [пакет передачи следующему чату](docs/handoff/2026-10-07/README.md).
Этот документ сохраняет последовательность этапа; его прежние current/HOME/policy/GDB-pending формулировки относятся к моменту записи и не переопределяют последний checkpoint.


Пользователь разрешил совмещённый профиль с последующим отключением отдельных
составляющих. Отдельно разрешил включить GDB Atmosphere на время диагностики.
Наблюдение пользователя: примерно +5 FPS после четырёхъядерной политики;
это субъективная оценка, не подтверждённый контролируемый benchmark.

## Сохранённые логи и остановки

`tools/remote_switch/runs/20261007T152001Z-9bf6ca67` содержит полные последние
Dishonored.log, Long-Live-Santa.log и autorun_runtime.log. Ничего не запускалось
повторно ради получения этих логов. ELF -6 использован для расшифровки,
ASLR base вычислен по svcWaitForAddress с проверкой page alignment.
Полные символы/строки 52 потоков в `../artifacts/cumulative-20261007`.

Последний Dishonored остановился на frames=2758; счётчики ресурсов перестали
расти, а logger ещё работал. Main thread: server_wait → server select/call →
horizon_pipe_read_r → condvarWaitTimeout. Graphics/compiler/render-related
threads: NtWaitForAlertByThreadId → horizon_futex_wait → svcWaitForAddress.
Это места ожиданий, а не доказанный владелец взаимной блокировки или причина.
Подобный класс ожиданий был и до новой политики. Thread-entry frames
server_init_thread/signal_start_thread сами по себе не означают «не стартовал».

Long Live Santa дошла до guest stage 4, ни одного кадра за 237 s; сохранённые
стэки также показывают server/futex waits. Три минуты не объявлены окончательным
startup timeout: пользователь допускает 5–10 минут для медленного запуска.

Положительные интервалы frames в Dishonored достигают 29.9/s, медиана 15.4/s,
но включают загрузку/разные сцены. Это aggregate counter throughput, не
сопоставимый gameplay FPS. Частоты CPU в последних логах менялись и затем
опустились до 1428 MHz, EMC service read = 3000 MHz. Настройки HOC не менялись.
Проверки только одного нестабильного запуска не разделяют ошибку ПО и разгон.
Для этого нужен одинаковый сценарий с зафиксированными частотами, отдельно
снижение CPU и EMC, а не одновременное изменение всех параметров.

## Установленный профиль

| Составляющая | Реализация / переключатель |
|---|---|
| Четыре ядра | Прежняя policy 4 сохранена |
| Hot shader RAM cache | shader-ram-cache-mb.txt = 32; 0/удаление отключает |
| FEX L2 lookup | fex-l2-cache.txt = 1; 0 отключает; удаление сохраняет per-game/default выбор |
| DXVK | dxvk-experiment.conf: numCompilerThreads=2, enableGraphicsPipelineLibrary=True |
| Незавершённые ожидания | wait-diagnostics.txt=1; 0/удаление отключает |
| Общая занятость CPU | SYSTEM-CPU по kernel idle ticks для cores 0–3, все процессы |

RAM-кэш хранит до 512 записей, максимум 1 MB на запись, общий payload 32 MB;
ключ включает instance Mesa cache и полный cache key. LRU под mutex, caller
получает отдельную malloc-копию. destroy cache удаляет его namespace, чтобы
повторное использование адреса не читало старые записи. put и успешное SD-read
прогревают hot cache. Существующая async SD запись остаётся; RAM не заменяет
постоянный backend. При malloc failure промах/пропуск записи с fallback на SD.
Новые `[SHADER-RAM]` показывают budget/bytes/hits/misses/puts/evictions.

FEX L2 не является постоянным JIT-кэшем. Dynamic L1 и TSO не менялись, новые
FEX DLL не устанавливались. Применённая настройка видна в `[FEX-CACHE]`.
Часть памяти lookup может выделяться на поток; heap usage нужно измерить.
GPL=True использует feature при наличии поддержки, без background optimized
pipeline compilation; возможна потеря GPU эффективности. Это эксперимент,
а не рекомендованный default.

`[WAIT-PENDING]` пишет адрес и возраст futex/server-pipe ожиданий старше секунды,
не меняя исходные таймауты и не прерывая их. Это не граф владельцев lock и не
доказательство deadlock. `[SCHED-THREAD]` включает kernel handle для корреляции.
Системные idle ticks показывают общую занятость, не распределение по процессам.

Host RAM-cache tests прошли ASan/UBSan: ownership, namespace, LRU, byte cap,
destroy, parallel get/put, disabled mode. Native ELF/NRO слинкован. Исправлена
обработка отсутствующего FEX toggle до установки: read_bool_file возвращает
0 при отсутствии, поэтому для override используется отдельный presence check.

NRO -7 SHA256:
`91e363838144e3cccedaec26a334dddd336189d5752cfe27db49065506bbc807`,
40 576 056 bytes. SDK image cumulative-20261007 сохраняет прежние objects,
кроме изменённого disk_cache.c.o; u_queue.c.o из четырёхъядерного эксперимента.
Readbacks NRO/manifest/переключателей проверены. Журнал установки сохраняет
наличие оригинальных файлов; `.before-cumulative-20261007` — backups.

## GDB и постоянный FEX JIT cache

Первый GDB inventory не подключился. В system_settings.ini GDB флаги
отсутствовали; Atmosphere 1.11.2 boot2 code default отключён. По отдельному
явному согласию пользователя установлены enable_standalone_gdbstub=u8!0x1 и
enable_htc=u8!0x0. Backup `.before-gdb-20261007`, readback проверен.
Активация требует перезагрузки; открытие порта/attach ещё не проверено.
Автоматическая проверка первоначально отклонила этот системный параметр;
повторная установка выполнена после явного согласия, не обходом отказа.

Штатный GDB позволяет inventory и attach к поддерживаемым процессам. Attach
останавливает процесс, поэтому stack collection не является пассивным FPS
измерением. Нельзя остановить все системные процессы сразу: сетевые зависимости
самого stub могут заблокировать связь. Для Autorun нужны matching ELF + ASLR
relocation, thread apply all bt/registers и гарантированный detach. GDB видит
native ARM64; guest x86/JIT стэки требуют дополнительной корреляции с TEB и
таблицей трансляций. После диагностики вернуть GDB backup и перезагрузить.

В FEX UnixLib слот 6 MapFile сейчас STATUS_NOT_SUPPORTED. Upstream ABI содержит
FD, MapSize и Result (24 bytes), native mapper обычно отображает файл целиком.
Варианты Horizon: настоящая файловая mapping-абстракция либо read-only RAM
snapshot с зарегистрированным lifetime/освобождением. Для высокой скорости
целевой путь — быстрый in-memory lookup и повторное использование отображений,
SD для сохранения между запусками. Whole-file RAM copy не доказана быстрее:
может загрузить ненужные записи и увеличить startup/RAM.

Кроме mapper нужны корректная FD принадлежность bridge, закрытие/unmap,
идентичность файла/версии/cache schema, обработка повреждений и bounded storage.
Наша PermissionSMC отключается при DISKCACHE/ENABLECODECACHINGWIP: до включения
постоянного кэша нужна проверка SMC инвалидизации. Поэтому FEX_DISKCACHE=1
не включён. Не объявлять эту адаптацию реализованной или стабильной.

Источники:
- [Atmosphere boot2 1.11.2](https://github.com/Atmosphere-NX/Atmosphere/blob/1.11.2/libraries/libstratosphere/source/boot2/boot2_api.board.nintendo_nx.cpp)
- [Atmosphere GDB описание](https://github.com/Atmosphere-NX/Atmosphere/blob/master/docs/changelog.md)
- [FEX UnixLib ABI 2609](https://github.com/FEX-Emu/FEX/blob/FEX-2609/Source/Windows/UnixLib/FEXUnixLib.h)
- [FEX 2609 release](https://fex-emu.com/FEX-2609/)
- [DXVK 3.1.1 config](https://github.com/doitsujin/dxvk/blob/v3.1.1/dxvk.conf)

## Откат

Можно отключать по одному RAM/FEX/DXVK/wait переключателю и полностью
перезапускать Autorun. DXVK experiment применяется после per-game config,
поэтому его удаление возвращает прежние настройки игры (в Dishonored остаётся
контроль трёх workers из предыдущего эксперимента). Полный rollback — восстановить
парные NRO/manifest backups и старые settings по installation journal; созданные
файлы удалять только после проверки, что это именно наши experiment values.
GDB имеет отдельный rollback и начинает/прекращает действие после reboot.

Финальное состояние этого этапа: **HOME, active title None, DBI закрыт,
FTP остановлен**. В проверенном меню DBI не найден штатный reboot, доступный
нашему контроллеру; Power-кнопка через имеющийся протокол не управляется.
Для запуска GDB требуется физическая перезагрузка пользователем в тот же
правильный Hekate/HOC профиль. Новая -7 игра на этом этапе не запускалась;
native callback/heap/system-idle/wait observations -7 ещё не проверены на игре.
