# Окружение, установленные версии, сборка и откаты

Checkpoint7 октября2026, Asia/Tbilisi. Хеши — из install/readback journals;
нового подключения к Switch при оформлении документации не было.
Основные локальные файлы дополнительно сверены в ARTIFACT_HASHES.json.
Последнее устройство: DBI FTP Browse SD5000, Autorun закрыт.

## Рабочие каталоги и stack

| Что | Где / версия |
| --- | --- |
| Workspace / shell | `D:/autorun fork`, PowerShell |
| Git repo / артефакты / references | `autorun` / `artifacts` / `references` под workspace |
| WSL | `AvatarLab`, Linux x86_64 |
| Linux mirror | `/opt/autorun-build` |
| Workspace из WSL | `/mnt/d/autorun fork` |
| Базовый SDK image | `autorun/switch-dev:2026.09.28.4-fixed` |
| Native build dir | `/opt/autorun-build/horizon-wine/build-switch-amd64` |
| MinGW | `/opt/autorun-build/horizon-wine/toolchains/llvm-mingw-20260505-ucrt-ubuntu-22.04-x86_64/bin` |
| GDB/binutils в SDK | `/opt/devkitpro/devkitA64/bin/aarch64-none-elf-*` |
| Windows Python | `python` → `F:/anaconda3/python.exe`; warning о real location не равен failure |

| Компонент | Проверенная привязка |
| --- | --- |
| Wine manifest | `8770f0a8d241a06b11397cff04d6c159f39eb349` |
| FEX | FEX-2609; payload tool pin `395b132f346b1a45def246d10c52245edba1ef02` |
| Box64 | v0.4.4; `2f130fab1d6e1a4ee8a71dc60cfdfcc839ad192a` |
| Mesa Switch |26.2.3; `d4a00ea0ab3f59afb967cc5d779e4263d237bd77` |
| Dishonored / The Forest | ordinary DXVK3.1.1 |
| Ninja historical log | GPLAsync3.1.1-1, не ordinary DXVK |
| Atmosphere / HOC |1.11.2 / Horizon OC2.5.1 |
| GBE ABI reference | `7103add7ca6ef3ef8353279219880068f2494fc8` |
| sys-botbase base |v2.5, `45b6a6419624ed1a1f34aabc9f37336d5f130e61` |

`nx-amd64-fex-2640` — runtime log label, не diagnostic revision -8/полный FEX commit.
Generic FEX DLL short pin `ad60d97183a761c4` — другое поле provenance.
Cortex-A57 не получает новые ARM ISA extensions от software flags. TSO=1 сохраняется.
Full FEX ARM64 execution checks нельзя считать пройденными на x86_64 WSL.
Собственные synthetic PE probes запускались, игровые EXE на ПК — только анализ файлов.
Не обновлять весь stack из-за нового fork. FEX — CPU translation, DXVK/VKD3D — graphics API,
Mesa/NVK — native driver/compiler. Dishonored D3D9 не использует VKD3D.

## Исправление разгона и boot entry

Global PD профиль запрашивал CPU2499/GPU1228/EMC3000, реальные readings были
примерно1020/307/1600. Пользователь описывал свою конфигурацию2400/1200/3000
как стабильную; это не отдельный stress test Wine/FEX workloads.

Подтверждённые причины:

1. `/config/horizon-oc/config.ini`: Autorun App PD1785/921/1600 перекрывал Global.
   Удалены только эти три override для title0548EABB35576000.
   Другие global/app profiles, timings и voltages сохранены.
2. Hekate Autorun entry: одновременно hoc.kip и autorun-loader.kip с одним Loader ID.
   Второй binary совпал со stock project Loader SHA256
   `996922063f4a45b963f7bf61fff342fd9e1a6f0fc323868ab1938afec773d6dc`.
   Убран двойной Loader; один HOC с сохранённым CUST block revision6/version251,
   существующий custom Mesosphere сохранён.

Frequency picker получает таблицу HOC/clkrst/pcv: Autorun не содержит собственного
жёсткого потолка UI2400/3000. Исправлен recognizer Quick setup и выполнен
HOC / Preserve existing на Switch. После этого CPU2499 во время Dishonored и
EMC3000 в overlay/clkrst подтверждены. Governor всё ещё меняет CPU/GPU по нагрузке.
Idle1428/76.8 не означает возвращение stock потолка.

```ini
[Autorun]
pkg3=atmosphere/package3
cal0blank=1
secmon=atmosphere/exosphere.bin
logopath=bootloader/res/bootscreen.bmp
icon=bootloader/res/atmo_cfw.bmp
kip1=atmosphere/kips/hoc.kip
kernel=atmosphere/mesosphere.bin
```

Autoboot временно3 для теста, исходный **1 восстановлен**. Нужный профиль затем
пользователь выбирал сам. После ремонта clocks/voltages/timings больше не менялись.
Влияние разгона на поздние hangs не доказано и не исключено; нужен контроль
по одному CPU/EMC при неизменных cache/backend/сцене.

Evidence `artifacts/clock-language-20261007`: hoc.before/after.ini,
hekate_ipl.before/final.ini, clock-change/boot-change/autoboot-restored.json,
matching KIPs/images. Старые fix-boot/install/restore scripts не rerun автоматически.

## Текущие установленные payloads

| SD файл | Bytes | SHA256 |
| --- | ---: | --- |
|`/switch/wine/wine-nx-runtime.nro`, -8|40576056|`ad3e2eff3cc0c83939f6187951e5523d4bbf9c176817b6b353e0f11599e75368`|
|`/switch/wine/steamworks-local/steam_api.dll`, x86|467968|`e5aa414897739634065da8f88595cb32543b95eafe62b508fa42c646ba0b8abe`|
|`/switch/wine/steamworks-local/steam_api64.dll`, x64|443904|`0503fc69e9ea089a59544b2279927215d5e15f0f9cdbb5988b41d76b7a323623`|
|`/atmosphere/contents/430000000000000B/exefs.nsp`|130709|`5485be29787001270af2b4e2491c48d124d04f4d49a8a9a8a0bfb4010830d62b`|

SDK readbacks: clock-language installation.json; native: global-debug-fix native-installation.json;
sysbot: fs-installation.json и последующий reboot/clean-forest-recovery.json.
Adjacent SDK игр обновляется guarded preparer при следующем запуске,
а не немедленно во всех папках при global component install.

## Native история этого чата

| Revision | Изменение / artifact directory | NRO hash |
| --- | --- | --- |
|inherited2026.10.06-11|continuous perf / performance-continuous|`cfcfb85852363ba44799d77d01fdc23d3d40eee9f51d7f7af71dc802ac581dd7`|
|2026.10.07-1|policies0–3 / cpu-scheduling-20261007|`f4c345fafed86ea04435c11bcacfd029deb7b7b476ba6a576e566fa538877dcf`|
|2026.10.07-2|Screenshots ABI / backend-implementation-20261007|`d8118471f73b384d6129ecf3ca857ca00028f5c9200b0a84dea7825c83aa61e4`, failed Ninja acceptance|
|2026.10.07-3|callback ABI / backend-callbacks-20261007|`0718598fa8b0ae529c2508cf95c611bdce471b8b71d4f1637ca880a6373ea0af`|
|2026.10.07-4|setup/language/pipeline CPU / clock-language-20261007|`561a33cb527c533c0902f40cb0369b7d01d0b3e6a2b0c7b24f2c3434f29e64a9`|
|2026.10.07-5|System language option / same directory|`935a0129b95d616c2e4d44efa1d9cf78171b0e4d00d5f50d2fc655eb2fa8fc82`|
|2026.10.07-6|policy4/Mesa / four-core-20261007|`debfb7baf287e79a4707fc2895827a3b7a0d3f0cb52f229d538e998ae1946ec9`|
|2026.10.07-7|cumulative RAM/L2/DXVK/waits / cumulative-20261007|`91e363838144e3cccedaec26a334dddd336189d5752cfe27db49065506bbc807`|
|2026.10.07-8|passive-only stall / global-debug-fix-20261007|`ad3e2eff3cc0c83939f6187951e5523d4bbf9c176817b6b353e0f11599e75368`|

Sizes: -1/-2/-3=40563768; -4/-5/-6=40567864; -7/-8=40576056.
Exact -8 ELF retained. -4 runtime.elf не автоматически matching runtime-system-language.nro -5.
Installer journals фиксируют момент до reboot; reboot_required в старом JSON
не означает, что последующий подтверждённый reboot ещё не произошёл.

## Текущие переключатели под `/switch/wine`

| Файл | Сейчас | Отмена |
| --- | --- | --- |
|cpu-worker-policy.txt|4|0/удаление → legacy after restart|
|shader-ram-cache-mb.txt|32|0/удаление → off|
|fex-l2-cache.txt|1|0 → off; удаление → per-game/default|
|dxvk-experiment.conf|workers2/GPLTrue|удаление → прежний config; Dishonored control3 остаётся|
|wait-diagnostics.txt|1|0/удаление → off|
|stall-snapshots.txt|0|удаление/не0 → intrusive snapshots **включатся**|
|performance.txt|passive1 по current log|0 → off next launch|
|profile.txt|intrusive sampler off по current log|не включать для обычного FPS теста|
|remote-snapshot.txt|closed snapshots enabled по hardware|0/нет → direct live off|

Effective game log важнее предположения по старому header/config.
HTTP key истёк после exit. Current flags не превращать автоматически в release defaults.

## GDB и background services

Изначально GDB flags отсутствовали. С явного согласия пользователя временно выставлялись:

```ini
[atmosphere]
enable_standalone_gdbstub=u8!0x1
enable_htc=u8!0x0
```

Первый auto-review отклонил открытие debug port; после прямого разрешения пользователя
установка прошла. Не превращать этот исторический эпизод в новую повторную просьбу.
После диагностики исходный system_settings.ini восстановлен byte-for-byte:
SHA256 `a5b6c10f3d3e241b735f7768c619d2c410a0dde3ebf5e8873c18fa8988ec088c`.
Пользователь уже перезагрузил console. TCP22225 затем не подключился (timeout);
это недоступность с ПК, а не отдельное доказательство состояния listener.
Background sys-ftpd5001 остаётся отключён после старого resource/WLAN fatal;
не включать его снова для удобства и не путать с GDB pause нового запуска.

## Как собрана Mesa/native

Full Mesa SDK rebuild остановился: нет ARM64 Rust std. Изменённые C objects
собраны Meson с тем же pin/release flags; остальные objects не трогались:
u_queue.c.o в image `autorun/switch-dev:four-core-20261007`, disk_cache.c.o в
`autorun/switch-dev:cumulative-20261007`. Members заменены во всех соответствующих
archives (включая libEGL.a/libvulkan.a/libmesa_util.a), byte-verified и ranlib reindexed.
Exact archive list — sdk-patch.json, не предполагать только эти три.
Код Mesa statically linked в NRO; FEX/Mesa всей пачкой DLL на SD не обновлялись.
Базовый SDK image без patches при rebuild потеряет Mesa hook/cache.

`global-debug-fix-20261007/build-native.py` копирует runtime/thread_profile/headers
в Linux mirror и вызывает `wine-nx-runtime-nro` с cumulative image.
Это incremental script: новые изменённые файлы тоже надо явно синхронизировать.
Сохранять новый matching ELF/NRO и полный log при каждой новой revision.

`global-debug-fix-20261007/build.sh` собирает весь исправленный sysbot source
из reference в отдельном /tmp каталоге контейнера. Старый prepare.py переписывает main
из upstream и не включает весь нынешний fix; его модернизация — технический долг.
Не вкладывать сложные shell vars в PowerShell/bash/Docker quoting; хранить .py/.sh.

## Установка и rollback

Scoped transaction: закрыть Autorun → DBI FTP → сверить current hash+manifest →
unique stage → readback/hash → rename current в unique backup → activate → final readback/journal.
Не писать active NRO/DLL/manifest; сохранить vault, games, prefix, caches, saves/manual options.

Исторические installers одноразовые: oldhash/unusedbackup уже не соответствуют state.
Suffixes: before-scheduling, before-screenshots, before-language,
before-system-language, before-four-core, before-cumulative, before-safe-debug (все20261007).
Callback-ABI suffix `.before-callbacks-20261007`; exact path читать из installation.json.

sysbot SD backup before-safe-title сохраняет `de124371...` extended original;
before-fs-isolation сохраняет `cbff8090...` PM-only intermediate.
Current active NSP —5485be..., уже перезагружен. Original official NSP отдельно
`exefs.official-v2.5.nsp`. Локальные previous.nsp / previous-safe-title.nsp сохранены.

Откат NRO с matching manifest; переключатели отдельно по existence originals.
Созданный файл удалять только если contents всё ещё наши, не затирать manual edit.
GDB rollback отдельный, уже выполнен. Native -10 содержит известный NULL-TLS collector defect;
для раннего отката -9 предпочтительнее -10 по этому дефекту, не как гарантия всей матрицы.
Текущая миграция документов никаких установок/откатов не запускает.
