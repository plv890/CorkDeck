# Передача Autorun — 7 октября 2026

Пакет фиксирует завершившийся чат и унаследованный контекст. Дата пользователя —
7 октября, Asia/Tbilisi; имена runs используют UTC. Это локальная документация,
а не новый набор разрешений. Новые сообщения пользователя имеют приоритет.

## С чего начать

1. Прочитать основной [NEXT_CHAT_REMOTE_DEBUG.md](../../../../NEXT_CHAT_REMOTE_DEBUG.md).
2. Для конкретной задачи открыть один тематический файл из таблицы.
3. Проверить соответствующий source и installation journal перед изменением.
   Не повторять всё исследование и все аппаратные тесты сначала.

| Документ | Содержание |
| --- | --- |
| [CODE_CHANGES.md](CODE_CHANGES.md) | Код и поведение Autorun/Wine/Steamworks/Mesa/sys-botbase, границы реализации |
| [ENVIRONMENT_AND_BUILD.md](ENVIRONMENT_AND_BUILD.md) | Частоты, Hekate/HOC, язык, toolchains, SDK, версии, хеши, установка и откаты |
| [REMOTE_CONTROL_AND_TELEMETRY.md](REMOTE_CONTROL_AND_TELEMETRY.md) | Известные маршруты кнопок, порты, восстановление, логирование и GDB |
| [RESULTS_AND_ROADMAP.md](RESULTS_AND_ROADMAP.md) | Игровые результаты, приоритеты, критерии проверки; включён полный итоговый отчёт диагностики |
| [ARTIFACTS.md](ARTIFACTS.md) | Где лежат исходники, бинарные артефакты, журналы, тесты и точные логи |
| [CURRENT_STATE.json](CURRENT_STATE.json) | Компактный актуальный checkpoint с происхождением данных |
| [ARTIFACT_HASHES.json](ARTIFACT_HASHES.json) | Проверенные локальные хеши ключевых файлов; это не новое чтение SD |
| [SOURCE_SNAPSHOT.json](SOURCE_SNAPSHOT.json) | Git branch/HEAD/status до миграции документации |
| [HISTORICAL_HANDOFF.md](HISTORICAL_HANDOFF.md) | Полный прежний handoff; исторические версии не заменяют текущий checkpoint |

## Актуальный результат

- Установлен native **2026.10.07-8** с накопительным профилем и пассивной диагностикой ожиданий.
- Устранены два подтверждённых ограничения разгона: App PD overrides и второй stock Loader
  в Hekate Autorun entry. CPU2499/EMC3000 подтверждены при Dishonored; governor остаётся активен.
- Язык консоли передаётся в Wine NLS, Steam UI/download default и local SDK;
  явные предпочтения и уже установленные языковые данные сохраняются.
- Приложение уже имело права на core0–3. Policy4 расширяет реальные маски Wine/Mesa,
  распределяет preferred hints и отключает собственный singleton balancer.
- Работает ограниченный shader RAM cache поверх persistent SD, включён FEX L2 lookup,
  эксперимент DXVK GPL=True/compiler workers2. Это не доказанная лучшая конфигурация.
- sys-botbase исправлен: PM-only title, корректный debug handle, один отдельный FS worker.
  Управление прошло аппаратный маршрут закрытия зависшего гостя и запуска DBI FTP.
- GDB native attach/stack/detach удалось выполнить. Временные настройки затем восстановлены,
  пользователь перезагрузил Switch. Полноценные гостевые стеки FEX/Wine ещё не доказаны.
- Dishonored стал субъективно плавнее; логовая тенденция положительная, контролируемого FPS A/B нет.
- The Forest не выдаёт первый кадр; две разные остановки локализованы, общий fix пока не найден.
- Assassin’s Creed выявил зависимость legacy wrapper от Steam.dll/client runtime.
  Этот путь текущий лёгкий SDK не обеспечивает.

## Последнее подтверждённое устройство

Switch `192.168.70.59`: **DBI → FTP → Browse SD**, TCP5000 доступен на завершённом тесте.
Autorun закрыт, его временный HTTP key истёк. sys-botbase6000 работает.
На момент этой миграции новых команд консоли не отправлялось: состояние взято из
последнего успешного `clean-forest-recovery.json`, а не из свежего скриншота.
В новом чате сначала дешёво проверить title/reachability; не считать этот экран вечным.

## Что не сделано

Нет persistent FEX disk JIT cache/MapFile, нового kernel scheduler, глобальной прошивочной
разблокировки core3, полной CPU/process timeline, wait-owner графа, гостевого debugger,
контролируемого p95/p99 FPS benchmark, доказанного общего fix зависаний или полной
Steam client совместимости. Новые SDL source изучены, но патч audio teardown не установлен.
Download10–20MB/s — прежняя цель исследования, не достигнутый результат.

Не скрывать эти ограничения обновлением headline в историческом отчёте.
