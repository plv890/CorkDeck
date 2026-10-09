/*
 * The settings model (app_settings.h): the table of settings and the
 * resolution of the value each one runs with.
 */
#include "app_settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "../config_json.h"
#include "../launcher_settings.h"

#define EN_RU(en, ru) { en, ru }
#define OFF_ON { { "0", "off", EN_RU( "Off", "Выкл" ) }, { "1", "on", EN_RU( "On", "Вкл" ) } }

static const char *const group_names[APP_GROUP_COUNT][APP_LANG_COUNT] =
{
    [APP_GROUP_GRAPHICS]     = EN_RU( "Graphics", "Графика" ),
    [APP_GROUP_TRANSLATION]  = EN_RU( "Translation and processor", "Трансляция и процессор" ),
    [APP_GROUP_INPUT]        = EN_RU( "Controls", "Управление" ),
    [APP_GROUP_DIAGNOSTICS]  = EN_RU( "Diagnostics", "Диагностика" ),
    [APP_GROUP_EXPERIMENTAL] = EN_RU( "Experimental", "Экспериментальное" ),
    [APP_GROUP_SHELL]        = EN_RU( "Interface", "Интерфейс" ),
};

static const char *const layer_names[APP_LAYER_COUNT][APP_LANG_COUNT] =
{
    [APP_LAYER_DEFAULT]    = EN_RU( "Default", "По умолчанию" ),
    [APP_LAYER_GLOBAL]     = EN_RU( "General settings", "Общие настройки" ),
    [APP_LAYER_PROFILE]    = EN_RU( "This application's profile", "Профиль приложения" ),
    [APP_LAYER_LIMIT]      = EN_RU( "Compatibility limit", "Ограничение совместимости" ),
    [APP_LAYER_EXPERIMENT] = EN_RU( "Test file on the card", "Тестовый файл на карте" ),
};

static const char *const reason_texts[APP_REASON_COUNT][APP_LANG_COUNT] =
{
    [APP_REASON_NONE] = EN_RU( "", "" ),
    [APP_REASON_VKD3D_NEEDS_OFFICIAL] = EN_RU(
        "DXVK + VKD3D needs an Official or GPLAsync DXVK of version 2.1 or newer, so the newest Official one is used.",
        "DXVK + VKD3D требует DXVK Official или GPLAsync версии 2.1 и новее, поэтому взята последняя Official." ),
    [APP_REASON_NO_VULKAN] = EN_RU(
        "This build has no Vulkan, so Wine's own renderer is used.",
        "В этой сборке нет Vulkan, поэтому используется собственный рендер Wine." ),
    [APP_REASON_NOT_X86] = EN_RU(
        "A native ARM64 program runs without a translator.",
        "Родная программа ARM64 выполняется без транслятора." ),
    [APP_REASON_LSFG_MISSING] = EN_RU(
        "Lossless.dll is not on the card (switch/wine/lsfg), so frame generation is off.",
        "На карте нет Lossless.dll (switch/wine/lsfg), поэтому генерация кадров выключена." ),
    [APP_REASON_FORWARDER_OLD] = EN_RU(
        "The installed forwarder does not give the fourth core; reinstall it in System maintenance.",
        "Установленный forwarder не даёт четвёртое ядро; переустановите его в «Обслуживании системы»." ),
    [APP_REASON_FEX_MISSING] = EN_RU(
        "The FEX module for this program's architecture is not installed.",
        "Модуль FEX для архитектуры этой программы не установлен." ),
    [APP_REASON_DIAGNOSTICS_OFF] = EN_RU(
        "Diagnostics are switched off as a whole.",
        "Диагностика выключена общим выключателем." ),
    [APP_REASON_NEEDS_EXTENDED] = EN_RU(
        "This affects how the program runs, so it works only in Extended diagnostics.",
        "Это влияет на выполнение программы, поэтому работает только в режиме «Расширенная диагностика»." ),
    [APP_REASON_EXPERIMENT_FILE] = EN_RU(
        "A test file in the runtime folder on the card overrides this setting.",
        "Тестовый файл в папке приложения на карте переопределяет эту настройку." ),
};

static const struct app_setting_def defs[APP_SET_COUNT] =
{
    [APP_SET_RENDERER] = { APP_SET_RENDERER, APP_GROUP_GRAPHICS, APP_TYPE_CHOICE, "d3d", "d3d9", NULL, 0, NULL, 1, 3,
        { { "wine", NULL, EN_RU( "Wine (OpenGL)", "Wine (OpenGL)" ) },
          { NULL, NULL, EN_RU( "DXVK", "DXVK" ) },
          { "dxvk+vkd3d", "dxvk", EN_RU( "DXVK + VKD3D", "DXVK + VKD3D" ) } },
        EN_RU( "Direct3D renderer", "Рендер Direct3D" ),
        EN_RU( "What turns the program's Direct3D calls into the console's graphics. Wine uses its built-in "
               "OpenGL renderer; DXVK uses Vulkan for Direct3D 8 to 11; DXVK + VKD3D adds Direct3D 12. "
               "Graphics DLLs lying beside the program are loaded first whatever is chosen here.",
               "Чем вызовы Direct3D программы превращаются в графику консоли. Wine — встроенный рендер через "
               "OpenGL; DXVK — Vulkan для Direct3D 8–11; DXVK + VKD3D добавляет Direct3D 12. "
               "Графические DLL, лежащие рядом с программой, загружаются первыми при любом выборе." ),
        EN_RU( "Decides speed and compatibility more than any other setting, and which one is better depends "
               "on the program. DXVK compiles shaders as they are first met, which shows as short stutters.",
               "Сильнее других настроек влияет на скорость и совместимость; что лучше — зависит от программы. "
               "DXVK компилирует шейдеры при первой встрече, это заметно как короткие подтормаживания." ) },

    [APP_SET_DXVK_SOURCE] = { APP_SET_DXVK_SOURCE, APP_GROUP_GRAPHICS, APP_TYPE_CHOICE, "dxvk-source", NULL, NULL, 0, NULL, 0, 3,
        { { NULL, NULL, EN_RU( "Official", "Official" ) },
          { "sarek", NULL, EN_RU( "Sarek", "Sarek" ) },
          { "gplasync", NULL, EN_RU( "GPLAsync", "GPLAsync" ) } },
        EN_RU( "DXVK variant", "Вариант DXVK" ),
        EN_RU( "Which build of DXVK is used. Official is the upstream release. Sarek is a branch for older "
               "graphics hardware and APIs. GPLAsync compiles pipelines in the background.",
               "Какая сборка DXVK используется. Official — оригинальный выпуск. Sarek — ветка для старого "
               "графического оборудования и API. GPLAsync компилирует конвейеры в фоне." ),
        EN_RU( "GPLAsync can reduce stutter at the price of objects appearing late; Sarek cannot be combined "
               "with VKD3D. The effect differs from program to program.",
               "GPLAsync может уменьшить подтормаживания ценой позднего появления объектов; Sarek несовместим "
               "с VKD3D. Эффект различается от программы к программе." ) },

    [APP_SET_DXVK_VERSION] = { APP_SET_DXVK_VERSION, APP_GROUP_GRAPHICS, APP_TYPE_TEXT, "dxvk-version", NULL, NULL, 0, NULL, 0, 0, {{0}},
        EN_RU( "DXVK version", "Версия DXVK" ),
        EN_RU( "A particular DXVK release. Left empty, the newest installed one is used.",
               "Конкретный выпуск DXVK. Если не задано — используется самый новый из установленных." ),
        EN_RU( "An older release sometimes runs a program a newer one does not, and the other way round.",
               "Старый выпуск иногда запускает программу, с которой не справляется новый, и наоборот." ) },

    [APP_SET_VKD3D_VERSION] = { APP_SET_VKD3D_VERSION, APP_GROUP_GRAPHICS, APP_TYPE_TEXT, "vkd3d-version", NULL, NULL, 0, NULL, 0, 0, {{0}},
        EN_RU( "VKD3D version", "Версия VKD3D" ),
        EN_RU( "A particular VKD3D-Proton release for Direct3D 12. Left empty, the newest installed one is used.",
               "Конкретный выпуск VKD3D-Proton для Direct3D 12. Если не задано — самый новый из установленных." ),
        { NULL, NULL } },

    [APP_SET_DXVK_HUD] = { APP_SET_DXVK_HUD, APP_GROUP_GRAPHICS, APP_TYPE_CHOICE, "dxvk-hud", NULL, "dxvk-hud", 0, NULL, 0, 4,
        { { "0", "off", EN_RU( "Off", "Выкл" ) },
          { "fps", NULL, EN_RU( "Frame rate", "Частота кадров" ) },
          { "api,fps,frametimes", NULL, EN_RU( "Compact", "Краткий" ) },
          { "version,api,devinfo,fps,memory,frametimes,compiler", NULL, EN_RU( "Full", "Полный" ) } },
        EN_RU( "Show FPS and frame times", "Показ FPS и времени кадра" ),
        EN_RU( "DXVK's own figures drawn over the program: frames per second, frame times, the Direct3D "
               "version, video memory and shader compilation. It does not cover VKD3D's Direct3D 12.",
               "Собственные показатели DXVK поверх программы: кадры в секунду, время кадра, версия Direct3D, "
               "видеопамять и компиляция шейдеров. Не охватывает Direct3D 12 через VKD3D." ),
        EN_RU( "Costs a little drawing time, and switches off DXVK's descriptor buffers while it is shown.",
               "Немного расходует время отрисовки и отключает descriptor buffers в DXVK, пока показан." ) },

    [APP_SET_FRAME_LIMIT] = { APP_SET_FRAME_LIMIT, APP_GROUP_GRAPHICS, APP_TYPE_CHOICE, "frame-limit", NULL, NULL, 0, NULL, 0, 8,
        { { NULL, "Off", EN_RU( "Off", "Выкл" ) }, { "30", NULL, EN_RU( "30", "30" ) },
          { "40", NULL, EN_RU( "40", "40" ) }, { "45", NULL, EN_RU( "45", "45" ) },
          { "60", NULL, EN_RU( "60", "60" ) }, { "75", NULL, EN_RU( "75", "75" ) },
          { "90", NULL, EN_RU( "90", "90" ) }, { "120", NULL, EN_RU( "120", "120" ) } },
        EN_RU( "Frame rate limit", "Ограничение частоты кадров" ),
        EN_RU( "The most frames per second the program may draw through Vulkan, DXVK and VKD3D. VSync and "
               "the program's own limit still apply.",
               "Наибольшее число кадров в секунду, которое программа может рисовать через Vulkan, DXVK и "
               "VKD3D. VSync и собственное ограничение программы продолжают действовать." ),
        EN_RU( "A limit below what the console manages gives an even frame time and less heat.",
               "Ограничение ниже возможностей консоли даёт ровное время кадра и меньший нагрев." ) },

    [APP_SET_VSYNC] = { APP_SET_VSYNC, APP_GROUP_GRAPHICS, APP_TYPE_SWITCH, "vsync", NULL, NULL, 0, NULL, 1, 2, OFF_ON,
        EN_RU( "VSync", "Вертикальная синхронизация" ),
        EN_RU( "Shows each frame in step with the screen's refresh.",
               "Показывает каждый кадр в такт обновлению экрана." ),
        EN_RU( "Off removes the wait for the screen but can tear the picture. Frame generation always "
               "presents in step with the screen.",
               "Выключение убирает ожидание экрана, но может давать разрывы изображения. Генерация кадров "
               "всегда выводит в такт экрану." ) },

    [APP_SET_UPSCALING] = { APP_SET_UPSCALING, APP_GROUP_GRAPHICS, APP_TYPE_CHOICE, "upscaling", "upscale", NULL, 0, NULL, 0, 3,
        { { NULL, "off", EN_RU( "Off (bilinear)", "Выкл (билинейное)" ) },
          { "fsr", "1", EN_RU( "FSR 1.0", "FSR 1.0" ) },
          { "integer", "2", EN_RU( "Integer", "Целочисленное" ) } },
        EN_RU( "Upscaling", "Масштабирование" ),
        EN_RU( "How a picture drawn with fewer pixels than the screen is enlarged. FSR 1.0 keeps edges and "
               "sharpens; Integer enlarges by whole steps with square pixels, for pixel art.",
               "Как увеличивается изображение, нарисованное с меньшим числом пикселей, чем у экрана. "
               "FSR 1.0 сохраняет контуры и добавляет резкость; целочисленное увеличивает целыми шагами "
               "с квадратными пикселями — для пиксельной графики." ),
        EN_RU( "FSR takes some GPU time each frame. Integer leaves wider black bars.",
               "FSR расходует часть времени GPU на каждом кадре. Целочисленное оставляет более широкие чёрные поля." ) },

    [APP_SET_SHARPNESS] = { APP_SET_SHARPNESS, APP_GROUP_GRAPHICS, APP_TYPE_CHOICE, "upscaling-sharpness", "sharpness", NULL, 0, NULL, 2, 6,
        { { "0%", NULL, EN_RU( "0%", "0%" ) }, { "20%", NULL, EN_RU( "20%", "20%" ) },
          { "40%", NULL, EN_RU( "40%", "40%" ) }, { "60%", NULL, EN_RU( "60%", "60%" ) },
          { "80%", NULL, EN_RU( "80%", "80%" ) }, { "100%", NULL, EN_RU( "100%", "100%" ) } },
        EN_RU( "FSR sharpness", "Резкость FSR" ),
        EN_RU( "How strongly FSR's second pass sharpens the enlarged picture.",
               "Насколько сильно второй проход FSR повышает резкость увеличенного изображения." ),
        { NULL, NULL } },

    [APP_SET_LSFG] = { APP_SET_LSFG, APP_GROUP_GRAPHICS, APP_TYPE_SWITCH, "lsfg", NULL, NULL, 0, NULL, 0, 2, OFF_ON,
        EN_RU( "Frame generation (LSFG-VK, 2x)", "Генерация кадров (LSFG-VK, 2x)" ),
        EN_RU( "Draws one made-up frame between each two real ones, for Vulkan, DXVK and VKD3D. It needs "
               "your own Lossless.dll in switch/wine/lsfg.",
               "Рисует один вычисленный кадр между каждыми двумя настоящими — для Vulkan, DXVK и VKD3D. "
               "Нужна ваша собственная Lossless.dll в switch/wine/lsfg." ),
        EN_RU( "Motion looks smoother, but the program does not respond faster, input lags by about a "
               "frame, and GPU time and memory are spent on it. Not a remedy for a low frame rate.",
               "Движение выглядит плавнее, но отклик программы не ускоряется, ввод запаздывает примерно "
               "на кадр, расходуются время GPU и память. Не лекарство от низкой частоты кадров." ) },

    [APP_SET_LSFG_PERFORMANCE] = { APP_SET_LSFG_PERFORMANCE, APP_GROUP_GRAPHICS, APP_TYPE_SWITCH, "lsfg-performance", NULL, NULL, 0, NULL, 1, 2, OFF_ON,
        EN_RU( "Frame generation: performance mode", "Генерация кадров: режим производительности" ),
        EN_RU( "Uses the cheaper of LSFG's two paths.", "Использует более дешёвый из двух путей LSFG." ),
        EN_RU( "Off gives the quality path, which takes more GPU time.",
               "Выключение включает путь качества, он требует больше времени GPU." ) },

    [APP_SET_LSFG_FLOW] = { APP_SET_LSFG_FLOW, APP_GROUP_GRAPHICS, APP_TYPE_CHOICE, "lsfg-flow", NULL, NULL, 0, NULL, 1, 3,
        { { "0.125", NULL, EN_RU( "12.5%", "12,5%" ) }, { "0.25", NULL, EN_RU( "25%", "25%" ) },
          { "0.5", NULL, EN_RU( "50%", "50%" ) } },
        EN_RU( "Frame generation: motion resolution", "Генерация кадров: разрешение оценки движения" ),
        EN_RU( "The size of the picture motion is estimated on.",
               "Размер изображения, по которому оценивается движение." ),
        EN_RU( "Lower values take less GPU time and memory and estimate motion more roughly.",
               "Меньшие значения требуют меньше времени GPU и памяти, но оценивают движение грубее." ) },

    [APP_SET_TRANSLATOR] = { APP_SET_TRANSLATOR, APP_GROUP_TRANSLATION, APP_TYPE_CHOICE, "cpu", NULL, NULL, 0, NULL, 1, 2,
        { { "box64", NULL, EN_RU( "Box64", "Box64" ) }, { NULL, NULL, EN_RU( "FEX", "FEX" ) } },
        EN_RU( "Processor translator", "Транслятор процессора" ),
        EN_RU( "What turns the program's x86 instructions into the console's ARM64 ones. FEX is the main "
               "one; Box64 is the alternative where it is supported.",
               "Что превращает инструкции x86 программы в инструкции ARM64 консоли. FEX — основной; "
               "Box64 — альтернатива там, где он поддерживается." ),
        EN_RU( "Speed and compatibility differ from program to program; neither is faster everywhere.",
               "Скорость и совместимость различаются от программы к программе; ни один не быстрее везде." ) },

    [APP_SET_FOUR_CORES] = { APP_SET_FOUR_CORES, APP_GROUP_TRANSLATION, APP_TYPE_SWITCH, "four-cores", NULL, NULL, 0, NULL, 1, 2, OFF_ON,
        EN_RU( "Fourth core", "Четвёртое ядро" ),
        EN_RU( "Lets the program's helper threads use core 3, which the console otherwise keeps for the system.",
               "Разрешает вспомогательным потокам программы использовать ядро 3, которое консоль обычно "
               "оставляет системе." ),
        EN_RU( "Usually helps programs with several busy threads. The system also runs on that core, so "
               "the gain is not guaranteed.",
               "Обычно помогает программам с несколькими занятыми потоками. На этом ядре работает и система, "
               "поэтому выигрыш не гарантирован." ) },

    [APP_SET_SYNC] = { APP_SET_SYNC, APP_GROUP_TRANSLATION, APP_TYPE_CHOICE, "sync", NULL, NULL, 0, NULL, 0, 2,
        { { NULL, NULL, EN_RU( "Standard", "Стандартная" ) }, { "horizon", NULL, EN_RU( "Horizon", "Horizon" ) } },
        EN_RU( "Thread synchronisation", "Синхронизация потоков" ),
        EN_RU( "How the program's threads wait for each other. Standard asks the built-in Wine server; "
               "Horizon waits directly on the console's own objects.",
               "Как потоки программы ждут друг друга. Стандартная обращается к встроенному серверу Wine; "
               "Horizon ждёт напрямую на объектах самой консоли." ),
        EN_RU( "Horizon can be faster in programs that wait very often, and is less widely tested.",
               "Horizon может быть быстрее в программах с очень частыми ожиданиями, но проверена на меньшем "
               "числе программ." ) },

    [APP_SET_CORE_BALANCING] = { APP_SET_CORE_BALANCING, APP_GROUP_TRANSLATION, APP_TYPE_SWITCH, NULL, NULL, "core-balancing", 0, NULL, 1, 2, OFF_ON,
        EN_RU( "Spread threads over cores", "Распределять потоки по ядрам" ),
        EN_RU( "Moves the program's busy threads apart, so two of them do not share one core while another is idle.",
               "Разводит занятые потоки программы, чтобы два из них не делили одно ядро при простаивающем другом." ),
        EN_RU( "Normally on. Off leaves every thread where it started.",
               "Обычно включено. При выключении каждый поток остаётся там, где был запущен." ) },

    [APP_SET_WORKER_POLICY] = { APP_SET_WORKER_POLICY, APP_GROUP_TRANSLATION, APP_TYPE_CHOICE, "worker-policy", NULL, "cpu-worker-policy", 0, "cpu-worker-policy.txt", 4, 5,
        { { "0", NULL, EN_RU( "0 - Graphics workers on core 3", "0 — графические потоки на ядре 3" ) },
          { "1", NULL, EN_RU( "1 - Background workers on core 3", "1 — фоновые потоки на ядре 3" ) },
          { "2", NULL, EN_RU( "2 - Background workers share cores", "2 — фоновые потоки делят ядра" ) },
          { "3", NULL, EN_RU( "3 - The kernel places every thread", "3 — все потоки размещает ядро системы" ) },
          { "4", NULL, EN_RU( "4 - Shared cores, background prefers core 3", "4 — общие ядра, фоновые предпочитают ядро 3" ) } },
        EN_RU( "Thread placement policy", "Политика размещения потоков" ),
        EN_RU( "Which cores the program's own threads and the graphics library's helper threads may run on. "
               "It applies from the next start of the program.",
               "На каких ядрах могут работать потоки самой программы и вспомогательные потоки графической "
               "библиотеки. Применяется со следующего запуска программы." ),
        EN_RU( "An experimental tuning: the best policy depends on how the program divides its work, and "
               "has to be found by trying.",
               "Экспериментальная подстройка: лучшая политика зависит от того, как программа делит работу, "
               "и подбирается пробой." ) },

    [APP_SET_SHADER_RAM_MB] = { APP_SET_SHADER_RAM_MB, APP_GROUP_TRANSLATION, APP_TYPE_CHOICE, "shader-ram-cache-mb", NULL, "shader-ram-cache-mb", 0, "shader-ram-cache-mb.txt", 3, 6,
        { { "0", NULL, EN_RU( "Off", "Выкл" ) }, { "8", NULL, EN_RU( "8 MB", "8 МБ" ) },
          { "16", NULL, EN_RU( "16 MB", "16 МБ" ) }, { "32", NULL, EN_RU( "32 MB", "32 МБ" ) },
          { "48", NULL, EN_RU( "48 MB", "48 МБ" ) }, { "64", NULL, EN_RU( "64 MB", "64 МБ" ) } },
        EN_RU( "Shader cache in memory", "Кэш шейдеров в памяти" ),
        EN_RU( "Keeps compiled shaders in memory as well as on the card, so one met again is not read from the card.",
               "Держит скомпилированные шейдеры в памяти помимо карты, чтобы повторно встреченный не читался с карты." ),
        EN_RU( "Takes the chosen amount from the program's memory; it empties itself when memory runs low. "
               "Worth it only where the card is the delay.",
               "Занимает выбранный объём из памяти программы; сам очищается при нехватке памяти. "
               "Оправдан только там, где задержку даёт карта." ) },

    /* The clocks are in clock_profile.h, by the number of the choice. */
    [APP_SET_CLOCK_PROFILE] = { APP_SET_CLOCK_PROFILE, APP_GROUP_TRANSLATION, APP_TYPE_CHOICE, "clock-profile", NULL, "clock-profile", 0, NULL, 0, 5,
        { { "system", NULL, EN_RU( "As the system has it", "Как в системе" ) },
          { "economy", NULL, EN_RU( "Economy", "Экономный" ) },
          { "balanced", NULL, EN_RU( "Balanced", "Сбалансированный" ) },
          { "performance", NULL, EN_RU( "Performance", "Производительный" ) },
          { "extreme", NULL, EN_RU( "Extreme", "Экстремальный" ) } },
        EN_RU( "Overclocking profile", "Профиль разгона" ),
        EN_RU( "Processor, graphics and memory clocks while this program runs, set through Horizon OC as the "
               "application's own profile, which comes before the global one from the overlay. In the two "
               "upper profiles the processor keeps its clock instead of following the load: a translated "
               "game is one busy thread, and the governor gives it less than it was allowed. Processor / "
               "graphics / memory, MHz: Economy 1224 / 460 / 1600, Balanced 1785 / 768 / 3000, Performance "
               "2091 / 998 / 3000, Extreme 2397 / 1228 / 3000.",
               "Частоты процессора, видеочипа и памяти на время работы программы. Задаются через Horizon OC "
               "как профиль самого приложения — он главнее общего профиля из оверлея. В двух старших "
               "профилях процессор держит частоту, а не следует за нагрузкой: игра под трансляцией — это "
               "один занятый поток, и регулятор даёт ему меньше разрешённого. Процессор / видеочип / "
               "память, МГц: Экономный 1224 / 460 / 1600, Сбалансированный 1785 / 768 / 3000, "
               "Производительный 2091 / 998 / 3000, Экстремальный 2397 / 1228 / 3000." ),
        EN_RU( "Never above the global profile of the same power mode, and voltages are not changed. Higher "
               "profiles heat the console and drain the battery faster; a clock the console is not stable "
               "at shows as a freeze or a crash. Needs Horizon OC 2.5.1; without it nothing changes. "
               "Graphics clocks above 998 MHz exist only in Horizon OC's graphics undervolt modes; without "
               "them a profile gets 998.",
               "Не выше общего профиля для того же режима питания; напряжения не меняются. Старшие профили "
               "сильнее греют консоль и быстрее разряжают батарею; частота, на которой консоль нестабильна, "
               "проявится зависанием или вылетом. Нужен Horizon OC 2.5.1; без него ничего не меняется. "
               "Частоты видеочипа выше 998 МГц есть только в режимах пониженного напряжения видеочипа "
               "Horizon OC; без них профиль получит 998." ) },

    [APP_SET_OWN_CONTROLS] = { APP_SET_OWN_CONTROLS, APP_GROUP_INPUT, APP_TYPE_SWITCH, "own-controls", NULL, NULL, 0, NULL, 0, 2,
        { { "0", "off", EN_RU( "Shared", "Общие" ) }, { "1", "on", EN_RU( "Its own", "Свои" ) } },
        EN_RU( "Key mapping", "Назначение клавиш" ),
        EN_RU( "Whether this program's own key mapping applies over the shared one. Switching it off keeps "
               "the keys that were set.",
               "Действует ли собственное назначение клавиш этой программы поверх общего. При выключении "
               "заданные клавиши сохраняются." ),
        { NULL, NULL } },

    [APP_SET_PAD_AS_KEYBOARD] = { APP_SET_PAD_AS_KEYBOARD, APP_GROUP_INPUT, APP_TYPE_SWITCH, "pad-as-keyboard", NULL, "controller-as-keyboard-and-mouse", 0, NULL, 0, 2, OFF_ON,
        EN_RU( "Controller as keyboard and mouse", "Контроллер как клавиатура и мышь" ),
        EN_RU( "Until the program reads the controller as a gamepad, the sticks and buttons send mouse "
               "movement and keys. Off, the controller is only ever a gamepad (XInput).",
               "Пока программа не читает контроллер как геймпад, стики и кнопки посылают движение мыши "
               "и клавиши. Выключено — контроллер всегда только геймпад (XInput)." ),
        EN_RU( "Needed by programs without gamepad support. In a program that has it, it can make the "
               "pointer move when the stick is used.",
               "Нужно программам без поддержки геймпада. В программе с поддержкой может двигать курсор "
               "при работе стиком." ) },

    [APP_SET_KEYBOARD_ON_FOCUS] = { APP_SET_KEYBOARD_ON_FOCUS, APP_GROUP_INPUT, APP_TYPE_SWITCH, NULL, NULL, "keyboard-on-text-focus", 0, NULL, 1, 2, OFF_ON,
        EN_RU( "On-screen keyboard on text fields", "Экранная клавиатура в текстовых полях" ),
        EN_RU( "Opens the on-screen keyboard by itself when a text field takes focus. Minus with a click "
               "of the right stick opens it in any program either way.",
               "Сама открывает экранную клавиатуру, когда фокус получает текстовое поле. «−» вместе с нажатием "
               "правого стика открывает её в любой программе при любом значении." ),
        { NULL, NULL } },

    [APP_SET_DIAGNOSTICS_MODE] = { APP_SET_DIAGNOSTICS_MODE, APP_GROUP_DIAGNOSTICS, APP_TYPE_CHOICE, "diagnostics", NULL, "diagnostics", 0, NULL, 2, 3,
        { { "off", NULL, EN_RU( "Off", "Выключена" ) },
          { "observe", NULL, EN_RU( "Observation", "Наблюдение" ) },
          { "extended", NULL, EN_RU( "Extended diagnostics", "Расширенная диагностика" ) } },
        EN_RU( "Diagnostics", "Диагностика" ),
        EN_RU( "Off records nothing beyond errors. Observation collects figures that do not change how the "
               "program runs. Extended also allows the tools that do: traces, the sampling profiler, wait records.",
               "«Выключена» не записывает ничего, кроме ошибок. «Наблюдение» собирает показатели, не меняющие "
               "выполнение программы. «Расширенная» разрешает и инструменты, которые его меняют: трассы, "
               "сэмплирующий профайлер, записи ожиданий." ),
        EN_RU( "Observation is meant to cost nothing noticeable. Extended slows the program down and "
               "writes to the card; do not judge performance in it.",
               "«Наблюдение» рассчитано на незаметную стоимость. «Расширенная» замедляет программу и пишет "
               "на карту; оценивать производительность в ней нельзя." ) },

    [APP_SET_SESSION_LOGS] = { APP_SET_SESSION_LOGS, APP_GROUP_DIAGNOSTICS, APP_TYPE_SWITCH, "session-logs", NULL, "session-logs", 0, NULL, 0, 2, OFF_ON,
        EN_RU( "Keep this application's logs", "Сохранять логи приложения" ),
        EN_RU( "Keeps the log of each session in a folder of the application's own, one file per session, "
               "instead of overwriting the last one.",
               "Сохраняет лог каждого сеанса в отдельной папке приложения, по файлу на сеанс, вместо "
               "перезаписи последнего." ),
        EN_RU( "Takes space on the card; old sessions are not removed by themselves.",
               "Занимает место на карте; старые сеансы сами не удаляются." ) },

    [APP_SET_PERF_COUNTERS] = { APP_SET_PERF_COUNTERS, APP_GROUP_DIAGNOSTICS, APP_TYPE_SWITCH, "performance-counters", NULL, "performance-counters", 0, "performance.txt", 1, 2, OFF_ON,
        EN_RU( "Performance counters", "Счётчики производительности" ),
        EN_RU( "Samples a share of the program's timing, wait and graphics calls for the session report.",
               "Выборочно измеряет вызовы времени, ожиданий и графики программы для отчёта о сеансе." ),
        EN_RU( "Passive: a sample of calls is timed, the rest are untouched.",
               "Пассивно: измеряется часть вызовов, остальные не затрагиваются." ) },

    [APP_SET_STALL_SNAPSHOTS] = { APP_SET_STALL_SNAPSHOTS, APP_GROUP_DIAGNOSTICS, APP_TYPE_SWITCH, NULL, NULL, "stall-snapshots", 0, "stall-snapshots.txt", 0, 2, OFF_ON,
        EN_RU( "Snapshots when the program stalls", "Снимки при зависании программы" ),
        EN_RU( "When no frame has come for a while, writes down what every thread is waiting for.",
               "Когда кадров долго нет, записывает, чего ждёт каждый поток." ),
        EN_RU( "Each snapshot pauses the program's threads for a moment, only while it is already stalled.",
               "Каждый снимок на мгновение приостанавливает потоки программы — только когда она уже стоит." ) },

    [APP_SET_REMOTE_SNAPSHOT] = { APP_SET_REMOTE_SNAPSHOT, APP_GROUP_DIAGNOSTICS, APP_TYPE_SWITCH, NULL, NULL, "remote-snapshot", 0, "remote-snapshot.txt", 1, 2, OFF_ON,
        EN_RU( "Status file for a computer", "Файл состояния для компьютера" ),
        EN_RU( "Writes a short status file a computer on the same network can collect while the program runs.",
               "Пишет короткий файл состояния, который компьютер в той же сети может забирать во время работы программы." ),
        EN_RU( "One small write to the card every few seconds.",
               "Одна небольшая запись на карту раз в несколько секунд." ) },

    [APP_SET_VERBOSE] = { APP_SET_VERBOSE, APP_GROUP_DIAGNOSTICS, APP_TYPE_SWITCH, "verbose", NULL, "verbose-log", 0, NULL, 0, 2, OFF_ON,
        EN_RU( "Verbose traces", "Подробные трассы" ),
        EN_RU( "Writes Wine's warnings and traces to the log.", "Записывает в лог предупреждения и трассы Wine." ),
        EN_RU( "Slows the program down noticeably and writes a great deal to the card.",
               "Заметно замедляет программу и много пишет на карту." ) },

    [APP_SET_PROFILER] = { APP_SET_PROFILER, APP_GROUP_DIAGNOSTICS, APP_TYPE_SWITCH, "profile", NULL, "profiler", 0, NULL, 0, 2, OFF_ON,
        EN_RU( "Sampling profiler", "Сэмплирующий профайлер" ),
        EN_RU( "Stops every thread at intervals to record where it is, and writes where the time went.",
               "Периодически останавливает каждый поток, чтобы записать, где он находится, и пишет, куда ушло время." ),
        EN_RU( "Intrusive: the stops themselves cost time, so frame rates measured with it are not the program's own.",
               "Вмешивается в выполнение: остановки сами отнимают время, поэтому частота кадров с ним — "
               "не собственная частота программы." ) },

    [APP_SET_WAIT_DIAGNOSTICS] = { APP_SET_WAIT_DIAGNOSTICS, APP_GROUP_DIAGNOSTICS, APP_TYPE_SWITCH, "wait-diagnostics", NULL, "wait-diagnostics", 0, "wait-diagnostics.txt", 1, 2, OFF_ON,
        EN_RU( "Wait records", "Записи ожиданий" ),
        EN_RU( "Keeps a bounded record of what each thread asked the server and what it waits on, for "
               "finding why a program stands still.",
               "Ведёт ограниченную запись того, что каждый поток запросил у сервера и чего ждёт, — чтобы "
               "найти, почему программа стоит." ),
        EN_RU( "Adds work to every wait.", "Добавляет работу к каждому ожиданию." ) },

    [APP_SET_WINDOWS] = { APP_SET_WINDOWS, APP_GROUP_EXPERIMENTAL, APP_TYPE_CHOICE, "windows", NULL, "windows-through-opengl", 1, NULL, 0, 2,
        { { "compositor", NULL, EN_RU( "Compositor (OpenGL)", "Композитор (OpenGL)" ) },
          { "framebuffer", NULL, EN_RU( "Framebuffer", "Кадровый буфер" ) } },
        EN_RU( "Windows shown by", "Чем показываются окна" ),
        EN_RU( "The compositor draws every window of the program through OpenGL. The framebuffer copies "
               "window pixels straight to the screen, for when the compositor misbehaves.",
               "Композитор рисует каждое окно программы через OpenGL. Кадровый буфер копирует пиксели окон "
               "прямо на экран — на случай, если композитор работает неправильно." ),
        EN_RU( "The framebuffer is slower and is a fallback, not a tuning.",
               "Кадровый буфер медленнее; это запасной путь, а не подстройка." ) },

    [APP_SET_GL_PINNED_CACHED] = { APP_SET_GL_PINNED_CACHED, APP_GROUP_EXPERIMENTAL, APP_TYPE_SWITCH, NULL, NULL, "gl-pinned-buffers-cached", 0, NULL, 1, 2, OFF_ON,
        EN_RU( "OpenGL: cacheable pinned buffers", "OpenGL: кэшируемые закреплённые буферы" ),
        EN_RU( "Maps the OpenGL driver's pinned GPU buffers so the processor may cache them.",
               "Отображает закреплённые буферы GPU драйвера OpenGL так, чтобы процессор мог их кэшировать." ),
        EN_RU( "Off restores the older, slower mapping; it exists to compare the two.",
               "Выключение возвращает прежнее, более медленное отображение; существует для сравнения." ) },

    [APP_SET_GL_CLEAN_BEFORE_SUBMIT] = { APP_SET_GL_CLEAN_BEFORE_SUBMIT, APP_GROUP_EXPERIMENTAL, APP_TYPE_SWITCH, NULL, NULL, "gl-clean-before-submit", 0, NULL, 1, 2, OFF_ON,
        EN_RU( "OpenGL: flush caches before each submission", "OpenGL: сброс кэшей перед каждой отправкой" ),
        EN_RU( "Makes what the processor wrote into GPU buffers visible to the GPU before commands are sent.",
               "Делает записанное процессором в буферы GPU видимым для GPU перед отправкой команд." ),
        EN_RU( "Off is faster and can corrupt the picture.", "Выключение быстрее, но может портить изображение." ) },

    [APP_SET_DISPLAY_DEVICES] = { APP_SET_DISPLAY_DEVICES, APP_GROUP_EXPERIMENTAL, APP_TYPE_SWITCH, NULL, NULL, "display-devices", 0, NULL, 1, 2, OFF_ON,
        EN_RU( "Register display devices", "Регистрировать устройства отображения" ),
        EN_RU( "Tells Windows programs about the screen as a display adapter and monitor.",
               "Сообщает Windows-программам об экране как о видеоадаптере и мониторе." ),
        EN_RU( "Some programs do not start without it; off is for comparison.",
               "Некоторые программы без этого не запускаются; выключение — для сравнения." ) },

    [APP_SET_REOPEN_SHELL] = { APP_SET_REOPEN_SHELL, APP_GROUP_SHELL, APP_TYPE_SWITCH, NULL, NULL, "reopen-the-launcher-on-exit", 0, NULL, 1, 2, OFF_ON,
        EN_RU( "Come back here when a program ends", "Возвращаться сюда после завершения программы" ),
        EN_RU( "Starts the application again when a program closes, instead of going to the HOME menu.",
               "Заново открывает приложение после закрытия программы вместо выхода в меню HOME." ),
        { NULL, NULL } },

    [APP_SET_DXVK_FOR_NEW] = { APP_SET_DXVK_FOR_NEW, APP_GROUP_SHELL, APP_TYPE_SWITCH, NULL, NULL, "dxvk-for-new-games", 0, NULL, 1, 2, OFF_ON,
        EN_RU( "New applications start with DXVK", "Новым приложениям назначать DXVK" ),
        EN_RU( "The renderer an application gets when it is added to the library. Off gives it Wine's.",
               "Рендер, который получает приложение при добавлении в библиотеку. Выключено — рендер Wine." ),
        { NULL, NULL } },

    [APP_SET_LANGUAGE] = { APP_SET_LANGUAGE, APP_GROUP_SHELL, APP_TYPE_CHOICE, NULL, NULL, "language", 0, NULL, 0, 3,
        { { "system", NULL, EN_RU( "As the console", "Как в консоли" ) },
          { "en", NULL, EN_RU( "English", "English" ) },
          { "ru", NULL, EN_RU( "Русский", "Русский" ) } },
        EN_RU( "Language", "Язык" ),
        EN_RU( "The language of the interface.", "Язык интерфейса." ),
        { NULL, NULL } },

    [APP_SET_ANIMATIONS] = { APP_SET_ANIMATIONS, APP_GROUP_SHELL, APP_TYPE_SWITCH, NULL, NULL, "animations", 0, NULL, 1, 2, OFF_ON,
        EN_RU( "Animations", "Анимации" ),
        EN_RU( "Transitions between screens and the movement of the focus.",
               "Переходы между экранами и движение фокуса." ),
        { NULL, NULL } },

    [APP_SET_SOUNDS] = { APP_SET_SOUNDS, APP_GROUP_SHELL, APP_TYPE_SWITCH, NULL, NULL, "sounds", 0, NULL, 1, 2, OFF_ON,
        EN_RU( "Interface sounds", "Звуки интерфейса" ),
        EN_RU( "The sounds of moving, choosing and going back.", "Звуки перемещения, выбора и возврата." ),
        { NULL, NULL } },
};

const struct app_setting_def *app_setting_def( enum app_setting id )
{
    return id >= 0 && id < APP_SET_COUNT ? &defs[id] : NULL;
}

const char *app_setting_group_name( enum app_setting_group group, enum app_language language )
{
    return group_names[group][language];
}

const char *app_setting_layer_name( enum app_setting_layer layer, enum app_language language )
{
    return layer_names[layer][language];
}

const char *app_setting_reason_text( enum app_setting_reason reason, enum app_language language )
{
    return reason_texts[reason][language];
}

/* The choice a word in a file names, or -1. */
static int choice_of( const struct app_setting_def *def, const char *word )
{
    int i;

    for (i = 0; i < def->choice_count; i++)
        if ((def->choices[i].value && !strcasecmp( def->choices[i].value, word )) ||
            (def->choices[i].alias && !strcasecmp( def->choices[i].alias, word ))) return i;
    return -1;
}

/* What the program's own file says: the key, or its older name when the key is
 * not there at all. */
static int profile_word( const struct launcher_kv *profile, const struct app_setting_def *def,
                         char *word, size_t size )
{
    if (!profile || !def->profile_key) return 0;
    if (launcher_kv_get( profile, def->profile_key, word, size )) return 1;
    return def->profile_alias && launcher_kv_get( profile, def->profile_alias, word, size );
}

static int global_choice( const struct wine_nx_config *global, const struct app_setting_def *def )
{
    char word[64];
    int i;

    if (!global || !def->global_key || (i = wine_nx_config_find( global, def->global_key )) < 0) return -1;
    if (def->type == APP_TYPE_SWITCH && global->entries[i].value[0] != '"')
    {
        int on = wine_nx_config_bool( global, def->global_key, -1 );

        if (on < 0) return -1;
        return def->global_inverted ? !on : on;
    }
    if (def->type == APP_TYPE_CHOICE && def->global_inverted && global->entries[i].value[0] != '"')
    {
        int on = wine_nx_config_bool( global, def->global_key, -1 );

        return on < 0 ? -1 : !on;
    }
    if (global->entries[i].value[0] == '"')
    {
        if (!wine_nx_config_string_value( global, def->global_key, word, sizeof(word) )) return -1;
    }
    else snprintf( word, sizeof(word), "%s", global->entries[i].value );
    return choice_of( def, word );
}

/* A loose test file. Most hold a truth value; stall-snapshots.txt switches off
 * only with "0", and the two that hold a number keep it as it was written. */
static int experiment_choice( const struct app_setting_def *def, app_settings_experiment experiment,
                              void *context, char *raw, size_t raw_size )
{
    char line[64];

    raw[0] = 0;
    if (!experiment || !def->experiment_file || !experiment( context, def->experiment_file, line, sizeof(line) ))
        return -1;
    if (def->id == APP_SET_STALL_SNAPSHOTS) return strcmp( line, "0" ) != 0;
    if (def->type == APP_TYPE_SWITCH)
        return !strcmp( line, "1" ) || !strcasecmp( line, "true" ) || !strcasecmp( line, "yes" ) ||
               !strcasecmp( line, "run" );
    snprintf( raw, raw_size, "%.*s", (int)raw_size - 1, line );
    return choice_of( def, line );
}

static void apply_limit( struct app_setting_value *value, int choice, enum app_setting_reason reason )
{
    if (value->effective == choice) return;
    value->layer[APP_LAYER_LIMIT] = choice;
    value->effective = choice;
    value->source = APP_LAYER_LIMIT;
    value->reason = reason;
}

void app_settings_resolve( struct app_settings *out, const struct wine_nx_config *global,
                           const struct launcher_kv *profile, const struct app_settings_limits *limits,
                           app_settings_experiment experiment, void *context )
{
    static const struct app_settings_limits no_limits = { 0, 1, 1, 1, 1, 0 };
    static const enum app_setting intrusive[] = { APP_SET_VERBOSE, APP_SET_PROFILER, APP_SET_WAIT_DIAGNOSTICS };
    static const enum app_setting passive[] = { APP_SET_SESSION_LOGS, APP_SET_PERF_COUNTERS,
                                                APP_SET_STALL_SNAPSHOTS, APP_SET_REMOTE_SNAPSHOT };
    struct app_setting_value *v;
    char word[64], raw[APP_SETTING_TEXT];
    unsigned int n;
    int id, layer;

    if (!limits) limits = &no_limits;
    memset( out, 0, sizeof(*out) );
    for (id = 0; id < APP_SET_COUNT; id++)
    {
        const struct app_setting_def *def = &defs[id];

        v = &out->values[id];
        for (layer = 0; layer < APP_LAYER_COUNT; layer++) v->layer[layer] = -1;
        v->layer[APP_LAYER_DEFAULT] = def->default_choice;
        if (id == APP_SET_OWN_CONTROLS) v->layer[APP_LAYER_DEFAULT] = !!limits->has_own_keys;
        v->layer[APP_LAYER_GLOBAL] = global_choice( global, def );
        if (profile_word( profile, def, word, sizeof(word) ))
        {
            if (def->type == APP_TYPE_TEXT)
            {
                if (launcher_dxvk_version_valid( word ))
                {
                    snprintf( v->chosen_text, sizeof(v->chosen_text), "%.*s", (int)sizeof(v->chosen_text) - 1, word );
                    v->layer[APP_LAYER_PROFILE] = 0;
                }
            }
            else v->layer[APP_LAYER_PROFILE] = choice_of( def, word );
        }
        for (layer = APP_LAYER_PROFILE; layer >= APP_LAYER_DEFAULT; layer--)
            if (v->layer[layer] >= 0) break;
        v->effective = v->layer[layer];
        v->source = layer;
        if (def->type == APP_TYPE_TEXT) memcpy( v->text, v->chosen_text, sizeof(v->text) );
    }

    /* What a combination of choices rules out, in the order the runtime has
     * always applied it: the renderer the program asked for decides whether
     * its DXVK variant and version can stand. */
    if (out->values[APP_SET_RENDERER].effective == 2)
    {
        struct app_setting_value *source = &out->values[APP_SET_DXVK_SOURCE];
        struct app_setting_value *version = &out->values[APP_SET_DXVK_VERSION];

        if (source->effective == 1 /* Sarek */ ||
            (version->text[0] && !launcher_dxvk_vkd3d_compatible( (enum dxvk_source)source->effective, version->text )))
        {
            apply_limit( source, 0, APP_REASON_VKD3D_NEEDS_OFFICIAL );
            if (version->text[0])
            {
                version->text[0] = 0;
                version->layer[APP_LAYER_LIMIT] = 0;
                version->source = APP_LAYER_LIMIT;
                version->reason = APP_REASON_VKD3D_NEEDS_OFFICIAL;
            }
        }
    }
    if (!limits->vulkan) apply_limit( &out->values[APP_SET_RENDERER], 0, APP_REASON_NO_VULKAN );
    if (!limits->lsfg_installed) apply_limit( &out->values[APP_SET_LSFG], 0, APP_REASON_LSFG_MISSING );

    /* Extended diagnostics alone may change how the program runs. */
    if (out->values[APP_SET_DIAGNOSTICS_MODE].effective < 2)
        for (n = 0; n < sizeof(intrusive) / sizeof(intrusive[0]); n++)
            apply_limit( &out->values[intrusive[n]], 0, APP_REASON_NEEDS_EXTENDED );

    /* A test file put on the card from a computer goes over all of that. */
    for (id = 0; id < APP_SET_COUNT; id++)
    {
        int choice = experiment_choice( &defs[id], experiment, context, raw, sizeof(raw) );

        v = &out->values[id];
        if (choice < 0 && !raw[0]) continue;
        v->layer[APP_LAYER_EXPERIMENT] = choice;
        v->effective = choice;
        v->source = APP_LAYER_EXPERIMENT;
        v->reason = APP_REASON_EXPERIMENT_FILE;
        memcpy( v->text, raw, sizeof(v->text) );
    }

    /* Switched off as a whole means off: nothing above brings a part of it back. */
    if (out->values[APP_SET_DIAGNOSTICS_MODE].effective == 0)
    {
        for (n = 0; n < sizeof(intrusive) / sizeof(intrusive[0]); n++)
            apply_limit( &out->values[intrusive[n]], 0, APP_REASON_DIAGNOSTICS_OFF );
        for (n = 0; n < sizeof(passive) / sizeof(passive[0]); n++)
            apply_limit( &out->values[passive[n]], 0, APP_REASON_DIAGNOSTICS_OFF );
    }
}

const char *app_setting_value_label( const struct app_settings *settings, enum app_setting id,
                                     enum app_language language )
{
    const struct app_setting_def *def = &defs[id];
    const struct app_setting_value *value = &settings->values[id];

    if (def->type == APP_TYPE_TEXT || value->effective < 0 || value->effective >= def->choice_count)
        return value->text;
    return def->choices[value->effective].label[language];
}

int app_settings_set_profile( struct launcher_kv *profile, enum app_setting id, int choice, const char *text )
{
    const struct app_setting_def *def = app_setting_def( id );
    const char *word = NULL;

    if (!def || !def->profile_key) return 0;
    if (def->profile_alias && !launcher_kv_set( profile, def->profile_alias, NULL )) return 0;
    if (def->type == APP_TYPE_TEXT)
    {
        if (choice >= 0 && text && text[0])
        {
            if (!launcher_dxvk_version_valid( text )) return 0;
            word = text;
        }
    }
    else if (choice >= def->choice_count) return 0;
    /* A setting no other layer can give is left out when it is at its default,
     * which is how these files have always been written. */
    else if (choice >= 0 && (def->global_key || choice != def->default_choice || id == APP_SET_OWN_CONTROLS))
        word = def->choices[choice].value ? def->choices[choice].value : def->choices[choice].alias;
    return launcher_kv_set( profile, def->profile_key, word );
}

int app_settings_set_global( struct wine_nx_config *global, enum app_setting id, int choice )
{
    const struct app_setting_def *def = app_setting_def( id );
    char json[80];

    if (!def || !def->global_key || choice < 0 || choice >= def->choice_count) return 0;
    if (def->type == APP_TYPE_SWITCH || def->global_inverted)
        return wine_nx_config_set_bool( global, def->global_key, def->global_inverted ? !choice : choice );
    snprintf( json, sizeof(json), "\"%s\"", def->choices[choice].value );
    return wine_nx_config_set( global, def->global_key, json );
}
