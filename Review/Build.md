# CMake: сборка base с текущими драйверами

Исходный обзор: 12.10.2026. Обновлено: **14.10.2026**.

## Проверка сборки базы после GPIO — 14.10.2026

**Итог:** 26 актуальных сборок Debug/Release: **14 собраны, 12 остановились на
ошибках компиляции**. Все 26 конфигураций CMake прошли. GPIO не вызвал ошибок
в проверенной матрице. Успешные сборки дали ELF/BIN/HEX/MAP без предупреждений
компилятора. Две причины отказов: имя функции CMSIS в Flash/F7 и отсутствующий
API зависимостей I2C. Изменения исходников для обхода ошибок не вносились.

### Какие рабочие копии использованы

| Компонент | Папка | HEAD |
| --- | --- | --- |
| База | `E:\projects\STM\VS\VSM-STM\stm32_cmake_base` | `08c4b0f7f49ce539518a95b008a16355697f51f0` |
| Драйверы | `E:\projects\STM\VS\VSM-STM\STM32_Drivers_CPP` | `f474d292271ce6cdf90bb72c441309d2ed060c9e` |
| Базовые файлы | `E:\projects\STM\VS\VSM-STM\STM32-base_files` | `9e7af11361e5326badf69367fd4c2e0cded3b4a7` |

Драйверы проверены вместе с незакоммиченными файлами рабочей копии. Источники
и состояния Git зафиксированы в [metadata.json](build_logs/2026-10-14_base_gpio/metadata.json);
записанные SHA256 GPIO/Flash и базовых CMake-файлов не изменились за время прогона.
`Drivers/` внутри исходной базы и release tag v2.1 не использовались.

Проверки выполнены в изолированных копиях текущей базы в системном TEMP.
В них скопированы CMake, startup/newlib stubs, CMSIS Core и пример приложения;
параметры project.json и проверочный main менялись только в этих копиях.
Драйверы подключены прямо из текущей папки через
`FETCHCONTENT_SOURCE_DIR_DRIVERS`, базовые файлы — через `STM32_BASE_FILES_DIR`.
Интернет, TEST и пользовательские проекты не использовались. Код базы,
базовых файлов и драйверов не изменялся.

Инструменты: **cmake version 3.28.1**, **Ninja 1.11.1**,
**arm-none-eabi-g++ (GNU Tools for STM32 13.3.rel1.20240926-1715) 13.3.1 20240614**. Использованы штатные пресеты базы:
Debug `-Og -g3`, Release `-Os -g0 -DNDEBUG`, C++20 и штатные CPU/FPU/linker flags.

### Матрица полного configure/build/link

Ядро: System, RCC, GPIO, Flash, IRQ Registry. Во временном
[core_probe.cpp](build_logs/2026-10-14_base_gpio/core_probe.cpp) вызываются System::Init,
ClockSystem::CalcPLL/Init (64 МГц от HSI), GPIO SetUp/levels, PinArray,
flash_data SetUp/read/write и IRQ Register/Dispatch/Unregister.
Проверены оба конструктора PIN, копирование проверенного описания, начальный
уровень OUTPUT и явное переопределение AF. Символы реально присутствуют в
успешных ELF; выборка nm сохранена в логах *_symbols.log.

UART/SPI проверены с constexpr-таблицами пинов и реальным вызовом SetUp;
DMA-передачи и все остальные пути этих драйверов не проверялись.

| Конфигурация | Debug | Release |
| --- | --- | --- |
| База без драйверов, штатный F446 | [OK](build_logs/2026-10-14_base_gpio/default_debug_build.log) | [OK](build_logs/2026-10-14_base_gpio/default_release_build.log) |
| F446, ядро драйверов | [OK](build_logs/2026-10-14_base_gpio/STM32F446RE_core_debug_build.log) | [OK](build_logs/2026-10-14_base_gpio/STM32F446RE_core_release_build.log) |
| F429ZI, ядро драйверов | [OK](build_logs/2026-10-14_base_gpio/STM32F429ZI_core_debug_build.log) | [OK](build_logs/2026-10-14_base_gpio/STM32F429ZI_core_release_build.log) |
| F767ZI, ядро драйверов | [Ошибка](build_logs/2026-10-14_base_gpio/STM32F767ZI_core_debug_build.log) | [Ошибка](build_logs/2026-10-14_base_gpio/STM32F767ZI_core_release_build.log) |
| G031, ядро драйверов | [OK](build_logs/2026-10-14_base_gpio/STM32G031K8_core_debug_corrected_build.log) | [OK](build_logs/2026-10-14_base_gpio/STM32G031K8_core_release_corrected_build.log) |
| G071, ядро драйверов | [OK](build_logs/2026-10-14_base_gpio/STM32G071RB_core_debug_corrected_build.log) | [OK](build_logs/2026-10-14_base_gpio/STM32G071RB_core_release_corrected_build.log) |
| F446, UART + SPI | [OK](build_logs/2026-10-14_base_gpio/STM32F446RE_uart_spi_debug_build.log) | [OK](build_logs/2026-10-14_base_gpio/STM32F446RE_uart_spi_release_build.log) |
| F767, UART + SPI | [Ошибка](build_logs/2026-10-14_base_gpio/STM32F767ZI_uart_spi_debug_build.log) | [Ошибка](build_logs/2026-10-14_base_gpio/STM32F767ZI_uart_spi_release_build.log) |
| G031, UART + SPI | [OK](build_logs/2026-10-14_base_gpio/STM32G031K8_uart_spi_debug_build.log) | [OK](build_logs/2026-10-14_base_gpio/STM32G031K8_uart_spi_release_build.log) |
| F446, UART + SPI + I2C | [Ошибка](build_logs/2026-10-14_base_gpio/STM32F446RE_uart_spi_i2c_debug_build.log) | [Ошибка](build_logs/2026-10-14_base_gpio/STM32F446RE_uart_spi_i2c_release_build.log) |
| F767, UART + SPI + I2C | [Ошибка](build_logs/2026-10-14_base_gpio/STM32F767ZI_uart_spi_i2c_debug_build.log) | [Ошибка](build_logs/2026-10-14_base_gpio/STM32F767ZI_uart_spi_i2c_release_build.log) |
| G031, UART + SPI + I2C | [Ошибка](build_logs/2026-10-14_base_gpio/STM32G031K8_uart_spi_i2c_debug_build.log) | [Ошибка](build_logs/2026-10-14_base_gpio/STM32G031K8_uart_spi_i2c_release_build.log) |
| F767, ядро + DMA-область SRAM2 | [Ошибка](build_logs/2026-10-14_base_gpio/STM32F767ZI_dma_region_debug_build.log) | [Ошибка](build_logs/2026-10-14_base_gpio/STM32F767ZI_dma_region_release_build.log) |

Для F767 DMA-область задана как
`{"name":"adc","size":"0x1000","memory":"SRAM2","mpu_region":7}`.
CMake сформировал область `DMA_adc` по адресу `0x2007F000`, размер 4096,
NOLOAD-секцию `.dma.adc` и запись MPU с номером 7; обычная SRAM2 уменьшена
до 12288 байт. System.cpp с настройкой региона компилируется. Финальная
линковка этого варианта не достигнута из-за ошибки Flash ниже; работа MPU/cache
на железе не проверялась.

### Подтверждённые препятствия сборке

**BASE-BUILD-1 — P1 — Flash/F7 не согласован с текущим CMSIS Core.**

`src/flash.cpp:117` вызывает `SCB_InvalidateDCache_byAddr`, которого нет
в CMSIS Core, поставляемом текущей базой. В
`stm32_cmake_base/cmsis-core/Include/cachel1_armv7.h:358` объявлена
`SCB_InvalidateDCache_by_Addr(volatile void*, int32_t)`.
Ошибка подтверждена на F767 в Debug/Release с ядром, с UART/SPI, с I2C и
с DMA-областью. Для исправления согласовать имя вызова с используемым CMSIS;
не добавлять обход в CMake и не объявлять F7 собранным до повторного прогона.
Лог: [STM32F767ZI_core_debug](build_logs/2026-10-14_base_gpio/STM32F767ZI_core_debug_build.log).

**BASE-BUILD-2 — P1 — I2C требует отсутствующий API DMA и IrqLock.**

На F446/F767/G031 ошибки начинаются с `DMA_Sx::RequestSet` в
`src/i2c.hpp:193–194`. В cpp отсутствуют требуемые `Release`, `CacheRxOk`,
`CacheClean`, `CacheInvalidate`, `GetTE_Flag`, `ClearTC_Flag`, используемая
перегрузка SetUp и `IrqLock`. В F4/F7 таблицы дополнительно ссылаются на
отсутствующие `I2c1::TX_alt`, `I2c3::RX_alt`, на F767 — `I2c4`.
Ошибки `dma_tx/dma_rx` и лишних инициализаторов являются следствием
необъявленных типов. Это рассогласование исходников, а не проблема GPIO
или путей include. API выровнять при разборе соответствующих модулей.
IRQ HasHandlers и адресный Unregister сейчас объявлены — прежняя часть
I2C-1 о недостающем IRQ API больше не актуальна.
Логи: [STM32F446RE_uart_spi_i2c_debug](build_logs/2026-10-14_base_gpio/STM32F446RE_uart_spi_i2c_debug_build.log),
[STM32G031K8_uart_spi_i2c_debug](build_logs/2026-10-14_base_gpio/STM32G031K8_uart_spi_i2c_debug_build.log).

### Проверки компиляционной модели GPIO

Дополнительно с фактическими флагами CMake выполнены 7 проверок на каждом из
F446/F767/G031 — **21 ожидаемый результат из 21**:

- Корректные PIN(GPIOA, 5), constexpr PIN(GPIOA_BASE, 5, 0) и копирование приняты.
- В consteval-конструкторе отклонены номер 16, номер 256, AF 16 и неизвестный адрес порта.
- Отклонены присваивание другого PIN и прямое изменение private const поля pin.

Результаты и диагностика: [gpio_checks.json](build_logs/2026-10-14_base_gpio/gpio_checks.json).
Для F767 это независимая проверка заголовка GPIO; она не подменяет полную сборку,
которая остановилась в Flash. Конструктор с GPIO_TypeDef* по принятому решению
не выполняет проверки диапазонов во время выполнения.

### Границы проверки и сохранённые материалы

Это проверки компиляции, линковки и генерации образов. Прошивки на MCU не
загружались, регистры, тайминги, Flash/ECC, IRQ и DMA на железе не проверялись.
Bootloader оставлен null. ADC/TIM/DAC/RTC/DMA как отдельные драйверы и остальные
optional modules в эту матрицу не включались; их последовательный разбор сохраняется.

Первая версия временного main ошибочно вызывала Delay_us на G0. Этот API
доступен только F4/F7. Поправлен только пример; четыре исходных попытки отмечены
`probe_error` в JSON и исключены из 26 итоговых сборок. Повторные G0-сборки успешны.

Сохранены [все результаты](build_logs/2026-10-14_base_gpio/results.json), конфигурационные/сборочные
логи, размеры ELF, выборки символов, временные примеры и скрипт прогона.
Расположение TEMP записано для трассировки; временные копии после проверки удаляются.

## Исходные пункты интеграции и текущий статус

### BUILD-1 — P2 — Список optional modules не описывает транзитивные source dependencies

**Место:** [CMakeLists.txt](../CMakeLists.txt), строка 168.

**Проблема:** CMake добавляет только явно запрошенные modules. MODBUS использует out-of-line USART и DMA; ADC включает TIM API, I2C/DMA transfer paths требуют DMA implementations. PUBLIC src include позволяет include headers невыбранных modules.

**Последствие / условие:** Например использование MODBUS без UART/DMA либо DMA методов SPI/TIM без DMA заканчивается undefined reference. Успешный core/обычный режим с --gc-sections не доказывает наличие unused DMA API.

**Возможное решение:** Согласовать простой выбор: добавлять необходимые dependencies автоматически либо явно требовать их с ясной configure diagnostic. Не включать все драйверы без необходимости.

**Статус:** решение не принято, код в рамках этого обзора не изменён.

### BUILD-2 — P1 — Генерируемая IRQ конфигурация не едина для всех sources

**Место:** [CMakeLists.txt](../CMakeLists.txt), строка 57.

**Проблема:** Размер shared строки задаётся только в generated config, обычные headers используют другой fallback. Дополнительные ISR requirements новых ADC/I2C не реализованы в IRQ Registry.

**Последствие / условие:** IRQ-1..IRQ-4 блокируют согласованное подключение base+drivers и регистрацию shared peripheral IRQ.

**Возможное решение:** Передавать общие constants одним способом; реализацию tables/stubs оставить в одном cpp. Сначала согласовать IRQ API, затем generator — без локальных aliases в периферийных headers.

**Статус на 14.10.2026:** исходное рассогласование закрыто: ёмкость берётся
из общего irq_registry_settings.hpp, generated config использует MAX_PER_IRQ
этого класса; HasHandlers и адресное Unregister реализованы. Сборки ядра и
UART/SPI на F4/G0 подтверждены. В сообщении CMake ещё печатается прежнее
IRQ_MAX_SHARED=2 для F4/F7, хотя фактический default заголовка равен 3;
это устаревший текст, не настройка таблицы.

### BUILD-3 — P2 — CMake принимает unsupported module/device сочетания без ясной диагностики

**Место:** [CMakeLists.txt](../CMakeLists.txt), строка 172.

**Проблема:** RTC только G0; DMA G071 имеет неверный IRQ name; TIM F7 без tables; DAC использует старый status API. CMake validation проверяет лишь spelling имени модуля.

**Последствие / условие:** Пользователь получает compile/link failure после успешного configure. Это подтверждено чтением sources, а не новым build matrix.

**Возможное решение:** После исправления modules закрепить поддерживаемую матрицу; для сознательно неподдерживаемых сочетаний выдавать configure/#error сообщение. Причины находятся в отдельных отчётах, не лечить их обходами CMake.

**Статус:** решение не принято, код в рамках этого обзора не изменён.

### BUILD-4 — P2 — Нужно явно указывать, какую рабочую копию проверяет base

**Место:** [../stm32_cmake_base/CMakeLists.txt](../../stm32_cmake_base/CMakeLists.txt), строка 81.

**Проблема:** База использует локальный Drivers/ при наличии, иначе FetchContent tag v2.1. Текущие файлы STM32_Drivers_CPP рядом с base не автоматически становятся этим Drivers/. STM32_BASE_FILES_DIR тоже является отдельным override.

**Последствие / условие:** Сборка может использовать release tag или закешированные источники вместо файлов, которые сейчас обсуждаются; результаты не относятся к правкам рабочей копии.

**Возможное решение:** Для будущих проверок явно подключать текущую папку драйверов через FETCHCONTENT_SOURCE_DIR_DRIVERS/локальный Drivers и локальный base_files override, записывать точные source paths в отчёт. Не копировать/перезаписывать пользовательские каталоги автоматически.

**Статус:** решение не принято, код в рамках этого обзора не изменён.

### BUILD-5 — P3 — README matrix отражает прежние проверки и не описывает текущий snapshot

**Место:** [ReadMe.md](../ReadMe.md), строка 34.

**Проблема:** README содержит старые результаты и backlog. Рабочая копия ADC/I2C/System менялась, текущий проход новых сборок не выполняет.

**Последствие / условие:** Старое «ok» рядом с именем модуля может быть воспринято как проверка именно текущих файлов.

**Возможное решение:** После отдельного согласованного build matrix обновить дату, MCU, комбинации и фактические источники. Сейчас считать Review статическим списком задач, не новым протоколом сборки.

**Статус:** решение не принято, код в рамках этого обзора не изменён.

## Как проверять после согласования очередного модуля

1. Создать отдельный экземпляр base вне пользовательского проекта и явно подключить текущие Drivers и STM32-base_files.
2. Выбрать F446, F767, G071 и другие реально заявленные для модуля MCU.
3. Собрать минимум core + проверяемый модуль + требуемые source dependencies; в main вызвать методы, которые нужно проверить на link (одного include недостаточно).
4. Для header-only модулей реально инстанцировать templates и методы.
5. Проверить Debug и Release; записать точные paths, defines, compiler и результаты.
6. Аппаратные handshake/timing/DMA/cache сценарии проверить на плате отдельно.

Этот порядок теперь применён в прогоне выше. Пункты BUILD-1/3 о прочих
модулях остаются для их очереди. BUILD-4 учтён явными локальными override;
актуальная матрица размещена в Review, README драйверов в этом прогоне не менялся.

