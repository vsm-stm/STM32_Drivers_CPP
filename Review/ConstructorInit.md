# Constructor initialization migration

## Applied

- USART/SPI/I2C/ADC/TIM use selected-device `Instance` descriptions from their defs.
- Per-instance metadata replaces the runtime peripheral-info arrays and searches.
- Numeric DMA storage and constexpr constructors allow static `constinit` objects.
- Named requests are owned by the peripheral definitions, with no duplicate
  request constants in DMA_Sx::Req.
- Existing fixed pin/DMA checks move to constant initialization. TIM DMA route
  data/functions are constexpr and visible in the header.
- EXTI and Modbus fixed constructor checks follow the same initialization model;
  DAC's fixed instance/channel are also described and checked before hardware setup.
- Main/IRQ volatile state is retained.
- The second TIM binding is checked during Init, as agreed.
- The deprecated USART pointer constructor retains runtime lookup/check behavior.
- Comments, usage examples and Initialization.md describe the new API and its
  constinit requirement. No new-family support or speculative configuration is added.

## Verification limits

Source changes and whitespace are reviewed. Builds, tests, assembly-size comparison
and hardware verification were not run for this migration.

## Русский

Постоянные привязки перенесены в constexpr-конструкторы и instance-описания.
Использование нового API требует constinit. Runtime-проверки прежнего USART
сохранены только в deprecated-перегрузке; второй TIM проверяется при Init.
Состояние main/IRQ сохраняет volatile. Сборки, тесты, сравнение ассемблера и
проверка на железе в рамках этой миграции не выполнялись.
