# Четыре ядра: тестовая реализация 2026.10.07-6

<!-- migration-checkpoint-20261007 -->

Актуальное состояние и итоговые результаты: [пакет передачи следующему чату](docs/handoff/2026-10-07/README.md).
Этот документ сохраняет последовательность этапа; его прежние current/HOME/policy/GDB-pending формулировки относятся к моменту записи и не переопределяют последний checkpoint.


Пользователь возобновил аппаратные тесты: агент запускает Dishonored и собирает
логи, затем пользователь играет вручную. Этот документ продолжает
FOUR_CORE_RESEARCH_20261007.md. Прирост FPS пока не установлен.

## Реализовано

Новая restart-only политика **4** в `/switch/wine/cpu-worker-policy.txt`:

- Автоматические Wine threads: разрешённые cores 0–3, preferred hints
  распределены по разрешённым ядрам; явно заданная игрой affinity сохраняется.
- Custom Wine balancer отключён, чтобы не заменять широкие маски singleton.
- DXVK shader/cache workers: preferred 3, mask 0xF, priority 63. Graphics CS
  сохраняет обычный приоритет и широкую маску. Это эксперимент с возможной
  конкуренцией/голоданием фоновых workers, не доказанный оптимальный режим.
- Mesa util_queue вызывает слабый native callback. Только в policy 4 и при
  подтверждённых правах 0–3 runtime задаёт mask 0xF. Очереди с флагом
  USE_MINIMUM_PRIORITY предпочитают core 3 и получают HOS priority 63;
  остальные сохраняют приоритет и распределяют preferred по worker index.
- При отсутствии callback, policy 4 или необходимых прав сохраняется старая
  Mesa mask 0x6. При ошибке изменения mask после понижения priority выполняется
  попытка восстановить прежний priority. Policies 0–3 сохраняют свои правила.
- Native workers регистрируются отдельно от Wine: shutdown/wake и sampler
  не начинают считать их гостевыми потоками и не приостанавливают их.
- `[MESA-SCHED]` и `[MESA-WORKER]`: имя, effective mask/priority, результаты SVC,
  CPU ticks и счётчики core в момент начала job. Это не полный residency trace,
  queue depth или измерение времени ожидания job; счётчики накопительные.

Число Mesa writers, shader cache, FEX/TSO, clocks и firmware не изменялись.
Для сравнения создан/дополнен соседний Dishonored `dxvk.conf` с
`dxvk.numCompilerThreads = 3`: прежний запуск использовал три workers, а
увеличение видимого CPU count могло бы изменить автоматический выбор DXVK.

## Сборка и установка

Host thread-profile tests прошли ASan/UBSan, включая классификацию новой
политики и сохранение обычных игровых/graphics threads вне background allowlist.
Native ELF/NRO собраны. ELF содержит три strong Mesa callback symbols.

Полная пересборка SDK остановилась из-за отсутствия ARM64 Rust std в образе.
Изменённый ARM64 `u_queue.c.o` уже скомпилирован штатным Meson-рецептом для
того же pinned Mesa revision и release options. Только этот объект заменён
в существующих libEGL.a, libvulkan.a и libmesa_util.a; каждый archive member
проверен байт-в-байт и переиндексирован devkitA64 ranlib. Другие SDK members
сохранены. Docker image: `autorun/switch-dev:four-core-20261007`.

Артефакты в `../artifacts/four-core-20261007`: исходники scripts, журналы,
`sdk-patch.json`, `build.json`, matching ELF/NRO. Две ранние попытки сборки
сохранены для диагностики; рабочая native сборка описана `build-native.py`.

Установленный NRO: **40 567 864 bytes**, SHA256
`debfb7baf287e79a4707fc2895827a3b7a0d3f0cb52f229d538e998ae1946ec9`.
Stage/final readbacks проверены для NRO, manifest, policy и DXVK config.
Предыдущий NRO -5 SHA256
`935a0129b95d616c2e4d44efa1d9cf78171b0e4d00d5f50d2fc655eb2fa8fc82`.
До установки policy была 0. Installation journal сохранён отдельно.

DBI FTP остановлен и DBI закрыт перед запуском Autorun. Текущий временный
ключ LAN чтения сохранён через DPAPI, не в открытом виде. Collector
`tools/remote_switch/runs/20261007T150056Z-observe-bd4fbae7` запущен на 900 s,
HTTP /live, interval 10 s, screenshots=false, sends_input=false.

## Откат

Для возврата прежнего размещения установить policy 0 и полностью перезапустить
Autorun. Mesa callback тогда не меняет прежнюю mask 0x6. Для полного возврата
восстановить NRO и manifest из `.before-four-core-20261007`, восстановить
policy backup; восстановить прежний dxvk.conf из такой же копии, если она
существует, иначе удалить созданный экспериментом файл. Не удалять чужой
изменённый конфиг без проверки. Installer одноразовый: не запускать повторно.

## Аппаратный результат

Логи первого запуска подтверждают `DXVK: Using 3 compiler threads`, shader
workers 80/84/88 с mask 0xF / priority 63, игровые потоки с разными preferred
cores и mask 0xF. Mesa disk$ worker получил preferred 3 / mask 0xF / priority 63;
gl workers получили широкую маску с обычным priority 59. SVC results = 0.
Счётчики начала заданий gl workers уже показывают исполнение на cores 0 и 1;
заданий disk$ на checkpoint ещё не было. Это подтверждение применения политики,
а не доказательство дополнительной работы на core 3 или выигрыша FPS.

К game second 155 зарегистрировано 710 успешных presents, fatal_kind=0.
Снимок `../artifacts/four-core-20261007/dishonored-ready.jpg` подтверждает
титульный экран «Нажмите любую клавишу». После восстановления TCP виртуальный
контроллер отдельно отсоединён; дальнейшее управление передано пользователю.
Collector продолжает читать логи без кнопок/снимков до конца своего окна.

Позднее presents остановились на **807** в трёх последовательных полученных
samples (observer elapsed 207.5 / 217.7 / 227.8 s). Затем HTTP перестал отвечать
(connection refused / timeout), а read-only TCP probe тоже завершился timeout.
Это не успешный FPS benchmark и не доказанная причина зависания. Пользователю
задан вопрос о реакции титульного экрана на физическую A; новых кнопок,
перезапусков или закрытий агент не отправлял. Ручная игровая проверка ещё
не подтверждена. fatal_kind=0 в последних доступных данных не исключает hang.

EMC service read показывает 3000 MHz, CPU в стартовых sample меняется
примерно между 1.4 и 2.1 GHz. Конфигурация HOC не менялась, однако одинаковая
частота CPU под игровой нагрузкой с прежним тестом ещё не подтверждена.
Сравнение FPS требует учитывать реальные clocks, а не только профиль.

Ожидается проверка поведения ручной игровой сессии и зависаний. Предыдущие logs относятся к другим
сценам и не дают контролируемого A/B FPS benchmark. Временная потеря TCP/HTTP
при старте не считается доказательством падения игры.
