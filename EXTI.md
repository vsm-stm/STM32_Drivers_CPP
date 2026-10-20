# External GPIO EXTI / Внешние GPIO EXTI

## English

Enable the optional module in project.json:

```json
"drivers": ["EXTI"]
```

Include `exti.hpp`. System/RCC, GPIO and IRQ_Registry are core dependencies;
this module needs no DMA or timer. It supports external lines 0..15 on F4/F7/G0.

### Binding and initialization

```cpp
#include "exti.hpp"

class Button final : public EXTI_Line {
public:
    Button() : EXTI_Line(PIN(GPIOC, 13), Edge::Falling) {}
    volatile bool pressed = false;
protected:
    void OnInterrupt() override { pressed = true; }
};

Button button;
// In main, after System/RCC initialization:
// if (button.SetUp(PIN::TYPE::INPUT_PullUp) != SysInitStatus::InitOK) {
//     ... handle setup failure ...
// }
```

The constructor fixes PIN and Edge::Rising/Falling/Both without register access.
An absent pin, a number outside 0..15 or an invalid edge traps. CMSIS pointer
construction follows PIN's valid-port contract; PIN(GPIOx_BASE, number) also
checks port/number during compilation. Check package pin availability yourself.
GPIO's new GetNumber() only reads the fixed number; it does not allow rebinding.

SetUp accepts only INPUT_NO_Pull (default), INPUT_PullUp or INPUT_PullDown.
It configures GPIO, port routing and edge detection, clears old flags, registers
this object and enables its source/NVIC. It returns InitError for repeated setup,
an occupied line, an invalid input mode or registry failure; these failures do
not modify GPIO/EXTI. Failed registry insertion restores the previous NVIC mask.

One owner per number: PA0 and PB0 cannot simultaneously own EXTI0. No copy/move
or runtime pin/edge changes. Reconfiguration requires explicit main-only DeInit.

### IRQ and capacity

Each object registers separately in IRQ_Registry. Each handler reads only its
line's mask/pending flags, acknowledges that line and calls protected virtual
OnInterrupt() (default empty). No GPIO reads, waits, allocation or registry
changes occur in the handler. On G0, rising/falling pending registers are read
once each; if both flags are present, one notification is delivered. This API
does not identify the edge or guarantee a callback for every physical transition.

| Family | Hardware vectors for GPIO lines |
|---|---|
| F4/F7 | EXTI0..EXTI4 separately, EXTI9_5 for 5..9, EXTI15_10 for 10..15 |
| G0 | EXTI0_1 for 0..1, EXTI2_3 for 2..3, EXTI4_15 for 4..15 |

**Current IRQ_MAX_SHARED default: 3 registrations per vector.** Thus G0 cannot
register more than three EXTI4_15 objects with the default setting. A fourth
SetUp returns InitError. The same limit applies to shared F4/F7 vectors and
counts any other registered handlers too. The driver does not increase it or
introduce a second dispatch table. If required, change irq_registry_settings.hpp
and rebuild the registry and all its users; no CMake setting is involved.

Serialize main-side configuration/registry/shared-register changes with affected
EXTI IRQs. SetUp/DeInit must not run in callbacks; do not change GPIO configuration
or EXTI routing behind an active owner. Keep objects alive while registered.

### Masking and teardown

```cpp
button.IRQ_en(DISABLE); // Own source only, no NVIC changes.
button.IRQ_en(ENABLE);  // Existing pending events are preserved.
button.DeInit();        // Main only; no callback.
```

IRQ_en does nothing before setup. It changes only the interrupt mask, leaving
the GPIO, edge detection, registry, pending bits and shared NVIC untouched.

DeInit masks the source, disables its edge detection, clears its pending flags,
unregisters only this object and releases its line. Other handlers survive;
their prior NVIC enable state is restored. With no handlers left, the vector is
kept disabled and its NVIC pending bit is cleared. GPIO input/pull, port routing
and shared clocks are retained. The fixed binding survives for another SetUp.
The destructor does not perform teardown; call DeInit before destruction.

EXTI_USE_ATOMIC defaults to 0 in exti.hpp. Value 1 uses ST ATOMIC_* for control
RMW; pending flags always use direct W1C writes. This does not serialize whole
operations or replace the caller's registry/configuration contract.

## Русский

Подключение в project.json:

```json
"drivers": ["EXTI"]
```

Заголовок — `exti.hpp`. Модуль использует ядро System/RCC/GPIO/IRQ_Registry;
DMA и таймер не требуются. Поддерживаются внешние линии 0–15 на F4/F7/G0.

### Привязка и настройка

В примере Button выше PIN и фронт закреплены конструктором. Доступны
Edge::Rising/Falling/Both. Отсутствующий пин, номер вне 0–15 и неверный enum
дают DebugTrap без доступа к регистрам. Для PIN(GPIOx, number) действует контракт
GPIO о корректном порте; PIN(GPIOx_BASE, number) также проверяет порт/номер при
сборке. Наличие вывода в корпусе проверяет приложение. Новый PIN::GetNumber()
только читает номер, изменять привязку через него нельзя.

SetUp принимает INPUT_NO_Pull (по умолчанию), INPUT_PullUp или INPUT_PullDown.
Драйвер сам настраивает GPIO, маршрутизацию порта и фронты, очищает старые флаги,
регистрирует объект и разрешает источник/NVIC. При повторной настройке, занятой
линии, неверном типе входа или отказе реестра возвращает InitError, не меняя
GPIO/EXTI. При отказе регистрации прежняя маска NVIC восстанавливается.

Один владелец на номер линии: PA0 и PB0 одновременно использовать EXTI0
не могут. Копирование/перемещение запрещены. Для смены роли нужен DeInit;
пин и фронт остаются привязаны к объекту.

### Прерывания и ограничение реестра

Каждый объект отдельно зарегистрирован в IRQ_Registry. Обработчик проверяет
только маску/флаги собственной линии, подтверждает событие и вызывает
protected virtual OnInterrupt() (по умолчанию пустой). В IRQ нет чтения GPIO,
ожиданий, выделения памяти и изменения реестра. G0 читает rising/falling pending
по одному разу; если стоят оба флага, выполняется одно уведомление. Callback
не сообщает, какой фронт произошёл, и не является счётчиком всех переходов.

Группировка векторов указана в таблице выше. **IRQ_MAX_SHARED сейчас равен 3.**
Поэтому на G0 при настройке по умолчанию помещаются только три объекта группы
EXTI4_15; четвёртый SetUp вернёт InitError. Такое же ограничение действует для
общих EXTI9_5/EXTI15_10 на F4/F7. Учитываются и другие обработчики того же вектора.
Драйвер лимит не увеличивает и дополнительную таблицу диспетчеризации не создаёт.
Если понадобится, ёмкость меняют в irq_registry_settings.hpp и пересобирают
реестр вместе с потребителями. Настроек через CMake нет.

Изменения конфигурации/реестра/общих регистров из main согласует вызывающий код
с затронутыми EXTI IRQ. SetUp/DeInit из callback запрещены. Во время владения
линией не меняют её GPIO/маршрутизацию напрямую. Объект сохраняют до снятия регистрации.

### Маскирование и освобождение

IRQ_en(DISABLE/ENABLE) меняет только маску своей линии, не затрагивая GPIO,
фронты, флаги, регистрацию и общий NVIC. До SetUp вызов ничего не делает.
После ENABLE ранее ожидавшее событие может вызвать callback.

DeInit из main отключает источник/фронты своей линии, очищает её флаги,
адресно удаляет регистрацию и освобождает номер. При оставшихся обработчиках
восстанавливается прежнее разрешение NVIC; при пустом реестре вектор остаётся
выключенным и очищается его NVIC pending. Конфигурация GPIO/подтяжка,
маршрутизация и общие такты сохраняются. Привязки конструктора остаются для
нового SetUp. Автоматического освобождения в деструкторе нет.

EXTI_USE_ATOMIC в exti.hpp по умолчанию 0; при 1 используются ATOMIC_* ST
для управляющих RMW. Pending подтверждается прямой W1C-записью независимо
от define. Это не заменяет согласование составных операций вызывающим кодом.

## Register references / Регистры

- [ST G0 LL EXTI](https://github.com/STMicroelectronics/stm32g0xx-hal-driver/blob/master/Inc/stm32g0xx_ll_exti.h): EXTICR layout, port selection, RPR1/FPR1 acknowledgement.
- [ST F4 LL EXTI](https://github.com/STMicroelectronics/stm32f4xx-hal-driver/blob/master/Inc/stm32f4xx_ll_exti.h): masks, edge selection and PR acknowledgement.

**Status:** source/diff inspection only. No build, tests or hardware execution.

**Статус:** просмотр исходников/diff. Сборки, тесты и запуск на железе не выполнялись.
