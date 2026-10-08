# ADC

*[English](#english) · [Русский](#русский)*

---

## English

## Contents

1. [Overview](#1-overview)
2. [Channels and pins](#2-channels-and-pins)
3. [SetUp() — clock, calibration, sequence](#3-setup--clock-calibration-sequence)
4. [Running: Start, StartDMA, StartTrig, StartCont, Stop](#4-running-start-startdma-starttrig-startcont-stop)
5. [Triggers: AttachTrig() and AttachExtTrig()](#5-triggers-attachtrig-and-attachexttrig)
6. [Interrupts and callbacks](#6-interrupts-and-callbacks)
7. [Usage examples](#7-usage-examples)
8. [Edge cases](#8-edge-cases)

---

## 1. Overview

`ADC_N` is one class per hardware ADC, with one API for all STM32G0, F4 and F7 parts. Like
[`USART`](UART.md)/[`SPI`](SPI.md) it inherits from `IIRQHandler`, looks the peripheral up in
`adc_table[]`, registers itself in [`IRQ_Registry`](IRQ_Registry.md) through `IRQ_en()`, and gets
DMA through `AttachDMA()`.

```cpp
ADC_N adc(ADC1);
adc.AddChannel(ADC_N::IN::PA0);      // build the sequence
adc.AddChannel(ADC_N::IN::PA1);
adc.SetUp();                         // clock, calibration, sequence, GPIO

adc.Start();                         // one sequence, poll / interrupt
adc.StartDMA(buf);                   // one sequence into buf by DMA
adc.StartTrig(buf, len);             // every trigger, circular DMA — until Stop()
adc.StartCont(buf, len);             // continuously, circular DMA — until Stop()
adc.Stop();
```

| | G0 | F4 / F7 |
|---|---|---|
| Channel selection | `CHSELR` as a sequence `SQ1..SQ8` (CHSELRMOD = 1, channels 0–14), or as a bitmask | `SQR1..3` sequence |
| Control | `CR` (ADVREGEN, ADCAL, ADEN, ADSTART, ADSTP, ADDIS) | `CR2` (ADON, SWSTART, EXTEN) |
| Status | `ISR`, cleared by writing 1 | `SR`, cleared by writing 0 |
| Interrupts | `IER` (EOC, EOS, OVR) | `CR1` (EOCIE, OVRIE) |
| Calibration | required, hardware (ADCAL) | none |
| Instances | ADC1 | ADC1 (+ ADC2, ADC3 where present) |

**Supported parts** (compiled against every CMSIS device header): all G0 (G030 … G0C1), all F4
(F401 … F479), all F7 (F722 … F779). Selecting the ADC module in CMake pulls in DMA and TIM.

---

## 2. Channels and pins

```cpp
explicit ADC_N(ADC_TypeDef* adcx);
void    AddChannel(ADC_PIN pin, SMP_SEL sel = SMP_SEL::SMP1);  // G0: appends, picks SMP1/SMP2
void    AddChannel(ADC_PIN pin, SMPL smpl = SMPL::DEFAULT);    // F4/F7: appends, own sampling time
void    ClearChannels();            // empties the sequence
uint8_t GetChannelCount() const;    // samples per sequence
```

The order of the `AddChannel()` calls is the conversion order and the order of the samples in the
DMA buffer. A channel may repeat (F4/F7, G0 sequencer mode). Everything takes effect at the next
`SetUp()`.

**Sampling time** follows the hardware, so it is set differently per family (`SMPL`: G0
1.5…160.5 cycles, F4/F7 3…480 cycles; `SMPL::DEFAULT` is 12.5 on G0 and 15 on F4/F7):

| | G0 | F4/F7 |
|---|---|---|
| Hardware | two values `SMP1`/`SMP2` for the whole ADC, a `SMPSELx` bit per channel picks one | one value per channel (`SMPR1/SMPR2`) |
| `SetUp()` | `SetUp(SMPL smp1, SMPL smp2)` — the two values | `SetUp()` — no sampling argument |
| `AddChannel()` | `AddChannel(pin, SMP_SEL::SMP1 / SMP2)` — which value | `AddChannel(pin, SMPL::CYC_…)` — the value |

The setting belongs to the channel number, not to the rank: a channel added twice with a different
setting makes `AddChannel()` call `System::DebugTrap()`. With the defaults (`SetUp()`,
`AddChannel(pin)`) the same code compiles on all families.

The sequence length is limited by the hardware: up to 16 ranks on F4/F7 (SQ1–SQ16); on G0 up to 8
in any order (SQ1–SQ8) or up to all 16 external channels in ascending channel order (§3). The
storage is sized by `ADC_N_MAX_CHANNELS` (default 16, 8 bytes per slot). `AddChannel()` calls
`System::DebugTrap()` if the sequence is full or a pin belongs to a different ADC
(`ADC_N::_2::PA0` added to `ADC1`).

**Internal inputs** are added like pins; `SetUp()` sets their enable bits in `ADC->CCR` (and clears
the bits of internal inputs not in the sequence — VBAT loads the battery while enabled):

| | VREFINT | TEMP (VSENSE) | VBAT | Enable bits |
|---|---|---|---|---|
| G0 — `ADC_N::IN::` | IN13 | IN12 | IN14 (VBAT/3) | VREFEN, TSEN, VBATEN |
| F401/F405/F407/F410/F415/F417 — `ADC_N::_1::` | IN17 | IN16 | IN18 (VBAT/2 on F405/F407/F415/F417, VBAT/4 on F401/F410) | TSVREFE (TEMP + VREFINT), VBATE |
| other F4, all F7 — `ADC_N::_1::` | IN17 | IN18 | IN18 (VBAT/4) | TSVREFE, VBATE |

On F4/F7 they exist on ADC1 only. Where TEMP and VBAT share IN18, the hardware reads VBAT while
VBATE is set, so a sequence with both makes `SetUp()` return `InitError`. TEMP and VREFINT need a
long sampling time (several µs, see the datasheet).

| Family | Table | Content |
|---|---|---|
| G0 | `ADC_N::IN::Pxx` | PA0–PA7 = IN0–7, PB0 = 8, PB1 = 9, PB2 = 10, PB10 = 11, PB11 = 15, PB12 = 16 |
| | G03x/G04x/G05x/G06x only | PB7 = 11, PA11 = 15, PA12 = 16 (replace PB10–PB12 in ≤ 32-pin packages), PA13 = 17, PA14 = 18 |
| | G07x/G08x/G0Bx/G0Cx only | PC4 = 17, PC5 = 18 |
| F4/F7 | `ADC_N::_1::Pxx`, `_2::` | PA0–PA7 = 0–7, PB0 = 8, PB1 = 9, PC0–PC5 = 10–15 |
| | `ADC_N::_3::Pxx` | PA0–PA3 = 0–3, PF6–PF10 = 4–8, PF3 = 9, PC0–PC3 = 10–13, PF4 = 14, PF5 = 15 |

Checked against the STM32CubeMX MCU database for every G0, F4 and F7 part number. `_2`/`_3` exist
only where the device has ADC2/ADC3. Whether a pin is bonded out in your package is not checked.
PA13/PA14 on G0 are SWDIO/SWCLK — using them as analog inputs disconnects the debugger.

---

## 3. SetUp() — clock, calibration, sequence

```cpp
SysInitStatus SetUp(SMPL smp1 = SMPL::DEFAULT, SMPL smp2 = SMPL::DEFAULT);   // G0
SysInitStatus SetUp();                                                       // F4/F7
```

`SetUp()` configures the ADC only — sequence, sampling times and internal inputs from the
`AddChannel()` calls; no trigger, no DMA, those are attached separately. It may be called again; anything running is
stopped first, an attached DMA/trigger stays attached.

**Clock** (below f_ADC max — G0: 35 MHz, F4/F7: 36 MHz):

- G0: synchronous PCLK/2 (`CFGR2.CKMODE = 01`); the reset default (asynchronous, SYSCLK) gives 64 MHz.
- F4/F7: PCLK2/4 (`ADC->CCR.ADCPRE = 01`); the reset default /2 gives 42 MHz at PCLK2 = 84 MHz.
  `CCR` is common to all ADCs.

**G0:** stop/disable (`ADSTP`, `ADDIS`) → clock → `ADVREGEN` + 1 ms → `ADCAL` → `SMPR`, `CFGR1` →
enable (ADEN is set again until `ADRDY`, as HAL does) → `CHSELR` + wait `CCRDY` → GPIO analog.

**G0 sequencer mode.** The fully configurable sequencer (`CHSELRMOD = 1`) converts in
`AddChannel()` order, like F4/F7, but has 8 ranks with 4-bit fields: at most 8 channels, all in
0–14. With any channel 15–18 or more than 8 channels the bitmask mode is used, which converts in
ascending channel order only; then the channels must be added in that order (no repeats),
otherwise `SetUp()` returns `InitError`.

**F4/F7:** stop → `CR2 = 0` → clock → `SMPR1/2` → `SQR1..3` → `CR1` (SCAN if > 1 channel) →
`CR2 = ADON` → 1 ms (t_STAB) → GPIO analog.

---

## 4. Running: Start, StartDMA, StartTrig, StartCont, Stop

```cpp
SysInitStatus AttachDMA(DMA_Sx* dma);                   // after SetUp()
void          Start();                                  // one sequence, no DMA
SysStatus     StartDMA(uint16_t* buf);                  // one sequence -> buf, then stops
SysStatus     StartTrig(uint16_t* buf, uint32_t len);   // every trigger, circular DMA
SysStatus     StartCont(uint16_t* buf, uint32_t len);   // continuous, circular DMA
SysStatus     StartCont();                              // continuous, one channel, no DMA
void          Stop();                                   // stops everything
bool          IsBusy() const;                           // StartDMA / StartTrig / StartCont running
```

| | `Start()` | `StartDMA(buf)` | `StartTrig(buf, len)` | `StartCont(buf, len)` | `StartCont()` |
|---|---|---|---|---|---|
| Needs | — | `AttachDMA()` | `AttachDMA()` + `AttachTrig()`/`AttachExtTrig()` | `AttachDMA()` | exactly one channel |
| Conversions | the sequence once | the sequence once | the sequence on every trigger edge | back to back (`CONT`) | back to back (`CONT`) |
| DMA | — | normal, `GetChannelCount()` samples | circular over `len` | circular over `len` | — |
| ADC DMA requests | — | one-shot (`DMACFG`/`DDS` = 0) | circular (`DMACFG`/`DDS` = 1) | circular | — |
| Result | `GetData()` | `buf` | `buf` | `buf` | `GetData()` — always the latest value |
| End | sequence done (EOC/EOS) | by itself: `IsBusy()` = false; DMA callback if `IRQ::DMA_TC` is on | `Stop()`; DMA callback after every `len` samples | `Stop()`; DMA callback after every `len` samples | `Stop()` |
| Timer | — | — | started last by `StartTrig()`, stopped by `Stop()` | — | — |

`AttachDMA()` configures the stream: peripheral → memory, half-word, MINC, source `&ADCx->DR`,
request from `adc_table` (G0: DMAMUX ID 5; F4/F7: DMA2 CHSEL 0/1/2). It returns `InitError` if
`SetUp()` was not called, `dma` is null, or the stream is unknown/already in use.

`StartTrig()`/`StartCont(buf, len)` return `Error` if no DMA (or, for `StartTrig()`, no trigger) is
attached, or `len` is not a non-zero multiple of `GetChannelCount()` ≤ 65535. `StartCont()` returns
`Error` unless the sequence is exactly one channel. All of them return `Busy` while something runs.

`StartCont()` without DMA overwrites unread results by design: G0 sets `OVRMOD = 1`; on F4/F7 an
overrun is not detected at all without DMA and with `EOCS = 0`. An OVR flag does not stop this mode.

`Stop()` stops the trigger timer, the conversions (G0: ADSTP; F4/F7: clears EXTEN and CONT — a
conversion in progress completes), clears the ADC DMA/CONT/OVRMOD bits and disables the DMA stream.

In the circular modes the DMA callback runs while the DMA continues writing from the start of
`buf`; copy the data out quickly (a half-transfer callback is not implemented yet).

---

## 5. Triggers: AttachTrig() and AttachExtTrig()

```cpp
SysInitStatus AttachTrig(TIM_TriggerGenerator* trg);   // timer trigger
SysInitStatus AttachExtTrig();                         // EXTI line 11
```

A trigger timer is created like a DMA stream — the timer plus a request that says what it
triggers — and set up with its period; then it is attached:

```cpp
TIM_TriggerGenerator trg(TIM3, TIM_TriggerGenerator::Req::Adc::TIM3_TRGO);  // like DMA_Sx(ch, Req)
trg.SetUp(10000);                  // TRGO / TRGO2 request: the frequency is enough

TIM_TriggerGenerator cc(TIM1, TIM_TriggerGenerator::Req::Adc::TIM1_CC1);
cc.SetUp(10000, 1000, 250);        // CCx request: frequency, period (ticks), compare point

adc.AttachTrig(&trg);
```

- The `TIM_TriggerGenerator` constructor checks that the request belongs to that timer
  (`System::DebugTrap()` otherwise); `AttachTrig()` checks that the request is meant for the ADC
  and takes the EXTSEL value from it. No lookup, nothing else is configured by the ADC.
- `StartTrig()` starts the timer, `Stop()` stops it.
- `AttachExtTrig()` selects EXTI line 11 (rising edge) instead; the EXTI line itself (pin, edge) is
  configured by the application.
- Attaching is refused (`InitError`) while acquisition runs.

Requests (`TIM_TriggerGenerator::Req::Adc::…`, from [tim_trig_defs.hpp](src/tim_trig_defs.hpp);
EXTSEL values from the ST LL headers; each one exists only where its timer does):

| G0 | F4 | F7 |
|---|---|---|
| TIM1_TRGO2, TIM1_CC4, TIM2_TRGO, TIM3_TRGO, TIM15_TRGO, TIM6_TRGO, TIM4_TRGO | TIM1_CC1/2/3, TIM2_CC2/3/4, TIM2_TRGO, TIM3_CC1, TIM3_TRGO, TIM4_CC4, TIM5_CC1/2/3, TIM8_CC1, TIM8_TRGO | TIM1_CC1/2/3, TIM2_CC2, TIM5_TRGO, TIM4_CC4, TIM3_CC4, TIM8_TRGO/TRGO2, TIM1_TRGO/TRGO2, TIM2_TRGO, TIM4_TRGO, TIM6_TRGO |

The tables differ per family: code moved from F4 to F7 must pick a request that exists there
(e.g. there is no `TIM3_TRGO` on F7) — a missing one is a compile error.

---

## 6. Interrupts and callbacks

```cpp
void IRQ_en(IRQ irq, FunctionalState en);   // IRQ::EOC, IRQ::EOS, IRQ::OVR, IRQ::DMA_TC
void SetEOCCallback(void (*cb)(void));
void SetEOSCallback(void (*cb)(void));
void SetOVRCallback(void (*cb)(void));
void SetDMACallback(void (*cb)(void));      // StartDMA: once; StartTrig: every pass
```

- **All interrupts are off until `IRQ_en()` enables them** — the driver itself enables none, not
  even the DMA one. `IRQ::DMA_TC` is the transfer-complete interrupt of the attached DMA stream
  (needs `AttachDMA()`); only with it does the DMA callback run. Without it a `StartDMA()` run is
  still finished: the hardware stops by itself and `IsBusy()` sees the DMA transfer-complete flag.
- **EOC/EOS on F4/F7.** There is no separate EOS interrupt; the driver keeps `EOCS = 0`, so EOC
  fires once per sequence. `IRQ::EOS` is an alias of `IRQ::EOC`, both callbacks are called.
- **OVR** (when enabled) stops a running DMA acquisition (`Stop()`), then calls the OVR callback; `StartCont()` without DMA keeps running.
- **`HandleIRQ()` only handles enabled sources.** The same object is also registered on the DMA
  line, so the flags are masked with `IER`/`CR1`.
- **Shared lines.** F4/F7: ADC1/2/3 share `ADC_IRQn`; G0 with COMP: `ADC1_COMP_IRQn`. `IRQ_en()`
  removes only itself and masks the NVIC only when nobody is left on the line.

---

## 7. Usage examples

### Single conversion, polling (G0)

```cpp
ADC_N adc(ADC1);
adc.AddChannel(ADC_N::IN::PA1);
adc.SetUp(ADC_N::SMPL::CYC_39_5);
adc.Start();
while (!adc.IsEOC()) {}
uint16_t v = adc.GetData();
```

### Continuous, one channel, latest value (F4)

```cpp
ADC_N adc(ADC1);
adc.AddChannel(ADC_N::_1::PA0, ADC_N::SMPL::CYC_84);
adc.SetUp();
adc.StartCont();                      // runs until Stop()
...
uint16_t v = adc.GetData();           // latest conversion
```

### One sequence by DMA (F4)

```cpp
static uint16_t res[3];
DMA_Sx dma(DMA_Sx::Req::Adc1::RX);
ADC_N  adc(ADC1);
adc.AddChannel(ADC_N::_1::PA0);
adc.AddChannel(ADC_N::_1::PA1);
adc.AddChannel(ADC_N::_1::PC2);
adc.SetUp();
adc.AttachDMA(&dma);
adc.StartDMA(res);                    // 3 samples, then stops
while (adc.IsBusy()) {}
```

### Pins plus internal inputs, different sampling times (G0)

```cpp
ADC_N adc(ADC1);
adc.AddChannel(ADC_N::IN::PA0);                                  // SMP1
adc.AddChannel(ADC_N::IN::TEMP,    ADC_N::SMP_SEL::SMP2);
adc.AddChannel(ADC_N::IN::VREFINT, ADC_N::SMP_SEL::SMP2);
adc.SetUp(ADC_N::SMPL::CYC_12_5, ADC_N::SMPL::CYC_160_5);       // SMP1, SMP2; TSEN + VREFEN on
```

### Timer-paced scan into a circular buffer (G0)

```cpp
static uint16_t buf[2 * 8];           // 8 sequences of 2 channels

TIM_TriggerGenerator trg(TIM3, TIM_TriggerGenerator::Req::Adc::TIM3_TRGO);
trg.SetUp(1000);                      // 1000 sequences/s
DMA_Sx dma(DMA1_Channel1, DMA_Sx::Req::Adc1::RX);

ADC_N adc(ADC1);
adc.AddChannel(ADC_N::IN::PA0);
adc.AddChannel(ADC_N::IN::PB0);
adc.SetUp(ADC_N::SMPL::CYC_39_5);
adc.AttachDMA(&dma);
adc.AttachTrig(&trg);
adc.SetDMACallback(on_buffer_full);   // every 8 ms
adc.IRQ_en(ADC_N::IRQ::DMA_TC, ENABLE);
adc.StartTrig(buf, 16);               // DMA circular + TIM3
...
adc.Stop();
```

### Compare-channel trigger at a point of the period (F4, TIM1 CC1)

```cpp
TIM_TriggerGenerator trg(TIM1, TIM_TriggerGenerator::Req::Adc::TIM1_CC1);
trg.SetUp(20000, 1000, 500);          // 20 kHz, trigger in the middle of the period
adc.AttachTrig(&trg);
adc.StartTrig(buf, 3 * 32);
```

---

## 8. Edge cases

- **G0 with channels 15–18 or more than 8 channels requires ascending channel order** — otherwise
  `SetUp()` returns `InitError` (§3).
- **F4/F7 `Stop()` cannot abort a conversion in progress** — there is no ADSTP; it completes.
- **`ADC->CCR.ADCPRE` is shared** by all ADCs on F4/F7 — `SetUp()` of any instance sets PCLK2/4 for
  all of them.
- **In `StartTrig()` the DMA callback is called while the DMA keeps writing** — the beginning of
  `buf` is already being overwritten.
- **On overrun, acquisition stops** and `IsBusy()` becomes false.
- **A `TIM_TriggerGenerator` is a trigger source only** — no pins, no interrupts; a timer that runs
  PWM cannot be attached as a trigger.
- **G0 calibration is redone on every `SetUp()`** — about 1 ms (`System::Delay_ms(1)`, needs SysTick).

---

## See also

- [TIM.md](TIM.md) — `TIM_TriggerGenerator` (§7.9), the trigger requests.
- [DMA.md](DMA.md) — `DMA_Sx`, request tables (`Req::Adc1..3`), the `AttachDMA()` pattern.
- [IRQ_Registry.md](IRQ_Registry.md) — registration and dispatch behind `IRQ_en()`/`HandleIRQ()`.
- [GPIO.md](GPIO.md) — `PIN`, the `ANALOG` mode used for ADC inputs.
- [System.md](System.md) — `SysStatus`/`SysInitStatus`, `Delay_ms()`, `DebugTrap()`.

---

## Русский

## Содержание

1. [Обзор](#1-обзор)
2. [Каналы и пины](#2-каналы-и-пины)
3. [SetUp() — тактирование, калибровка, последовательность](#3-setup--тактирование-калибровка-последовательность)
4. [Запуск: Start, StartDMA, StartTrig, StartCont, Stop](#4-запуск-start-startdma-starttrig-startcont-stop)
5. [Триггеры: AttachTrig() и AttachExtTrig()](#5-триггеры-attachtrig-и-attachexttrig)
6. [Прерывания и колбэки](#6-прерывания-и-колбэки)
7. [Примеры использования](#7-примеры-использования)
8. [Угловые случаи](#8-угловые-случаи)

---

## 1. Обзор

`ADC_N` — один класс на один аппаратный ADC с одинаковым API для всех STM32G0, F4 и F7. Как и
[`USART`](UART.md)/[`SPI`](SPI.md), наследует `IIRQHandler`, ищет периферию в `adc_table[]`,
регистрируется в [`IRQ_Registry`](IRQ_Registry.md) через `IRQ_en()`, DMA подключается через
`AttachDMA()`.

```cpp
ADC_N adc(ADC1);
adc.AddChannel(ADC_N::IN::PA0);      // собрать последовательность
adc.AddChannel(ADC_N::IN::PA1);
adc.SetUp();                         // такт, калибровка, последовательность, GPIO

adc.Start();                         // одна последовательность, опрос / прерывание
adc.StartDMA(buf);                   // одна последовательность в buf через DMA
adc.StartTrig(buf, len);             // по каждому триггеру, циклический DMA — до Stop()
adc.StartCont(buf, len);             // непрерывно, циклический DMA — до Stop()
adc.Stop();
```

| | G0 | F4 / F7 |
|---|---|---|
| Выбор каналов | `CHSELR` как последовательность `SQ1..SQ8` (CHSELRMOD = 1, каналы 0–14) или битовая маска | последовательность `SQR1..3` |
| Управление | `CR` (ADVREGEN, ADCAL, ADEN, ADSTART, ADSTP, ADDIS) | `CR2` (ADON, SWSTART, EXTEN) |
| Статус | `ISR`, сброс записью 1 | `SR`, сброс записью 0 |
| Прерывания | `IER` (EOC, EOS, OVR) | `CR1` (EOCIE, OVRIE) |
| Калибровка | обязательна, аппаратная (ADCAL) | нет |
| Экземпляры | ADC1 | ADC1 (+ ADC2, ADC3, где есть) |

**Поддерживаемые кристаллы** (проверена компиляция со всеми заголовками CMSIS): все G0
(G030 … G0C1), все F4 (F401 … F479), все F7 (F722 … F779). При выборе модуля ADC в CMake
подключаются DMA и TIM.

---

## 2. Каналы и пины

```cpp
explicit ADC_N(ADC_TypeDef* adcx);
void    AddChannel(ADC_PIN pin, SMP_SEL sel = SMP_SEL::SMP1);  // G0: добавляет, выбирает SMP1/SMP2
void    AddChannel(ADC_PIN pin, SMPL smpl = SMPL::DEFAULT);    // F4/F7: добавляет, своё время выборки
void    ClearChannels();            // очищает последовательность
uint8_t GetChannelCount() const;    // отсчётов на последовательность
```

Порядок вызовов `AddChannel()` — это порядок преобразований и отсчётов в DMA-буфере. Канал можно
повторить (F4/F7, режим последовательности G0). Всё применяется при следующем `SetUp()`.

**Время выборки** задаётся так, как устроено железо, поэтому на семействах по-разному (`SMPL`: G0
1.5…160.5 тактов, F4/F7 3…480 тактов; `SMPL::DEFAULT` — 12.5 на G0 и 15 на F4/F7):

| | G0 | F4/F7 |
|---|---|---|
| Железо | два значения `SMP1`/`SMP2` на весь ADC, бит `SMPSELx` у канала выбирает одно | своё значение у каждого канала (`SMPR1/SMPR2`) |
| `SetUp()` | `SetUp(SMPL smp1, SMPL smp2)` — два значения | `SetUp()` — без параметра выборки |
| `AddChannel()` | `AddChannel(pin, SMP_SEL::SMP1 / SMP2)` — какое значение | `AddChannel(pin, SMPL::CYC_…)` — само значение |

Настройка относится к номеру канала, а не к позиции: канал, добавленный повторно с другой
настройкой, приводит к `System::DebugTrap()` в `AddChannel()`. С параметрами по умолчанию
(`SetUp()`, `AddChannel(pin)`) один и тот же код собирается на всех семействах.

Длину ограничивает железо: до 16 позиций на F4/F7 (SQ1–SQ16); на G0 до 8 в любом порядке (SQ1–SQ8)
или до всех 16 внешних каналов по возрастанию номера (§3). Размер хранилища — `ADC_N_MAX_CHANNELS`
(по умолчанию 16, 8 байт на позицию). `AddChannel()` вызывает `System::DebugTrap()`, если
последовательность заполнена или пин принадлежит другому ADC (`ADC_N::_2::PA0`, добавленный в `ADC1`).

**Внутренние входы** добавляются как пины; `SetUp()` включает их биты в `ADC->CCR` (и выключает биты
внутренних входов, которых нет в последовательности — включённый VBAT нагружает батарею):

| | VREFINT | TEMP (VSENSE) | VBAT | Биты включения |
|---|---|---|---|---|
| G0 — `ADC_N::IN::` | IN13 | IN12 | IN14 (VBAT/3) | VREFEN, TSEN, VBATEN |
| F401/F405/F407/F410/F415/F417 — `ADC_N::_1::` | IN17 | IN16 | IN18 (VBAT/2 на F405/F407/F415/F417, VBAT/4 на F401/F410) | TSVREFE (TEMP + VREFINT), VBATE |
| остальные F4, все F7 — `ADC_N::_1::` | IN17 | IN18 | IN18 (VBAT/4) | TSVREFE, VBATE |

На F4/F7 они есть только у ADC1. Где TEMP и VBAT делят IN18, при включённом VBATE канал читает VBAT,
поэтому последовательность с обоими приводит к `InitError` из `SetUp()`. TEMP и VREFINT требуют
длинного времени выборки (несколько мкс, см. даташит).

| Семейство | Таблица | Содержимое |
|---|---|---|
| G0 | `ADC_N::IN::Pxx` | PA0–PA7 = IN0–7, PB0 = 8, PB1 = 9, PB2 = 10, PB10 = 11, PB11 = 15, PB12 = 16 |
| | только G03x/G04x/G05x/G06x | PB7 = 11, PA11 = 15, PA12 = 16 (замена PB10–PB12 в корпусах ≤ 32 выводов), PA13 = 17, PA14 = 18 |
| | только G07x/G08x/G0Bx/G0Cx | PC4 = 17, PC5 = 18 |
| F4/F7 | `ADC_N::_1::Pxx`, `_2::` | PA0–PA7 = 0–7, PB0 = 8, PB1 = 9, PC0–PC5 = 10–15 |
| | `ADC_N::_3::Pxx` | PA0–PA3 = 0–3, PF6–PF10 = 4–8, PF3 = 9, PC0–PC3 = 10–13, PF4 = 14, PF5 = 15 |

Сверено с базой STM32CubeMX по всем партномерам G0, F4 и F7. `_2`/`_3` есть только там, где у
кристалла есть ADC2/ADC3. Выведен ли пин в вашем корпусе, не проверяется. PA13/PA14 на G0 — это
SWDIO/SWCLK: использование их как аналоговых входов отключает отладчик.

---

## 3. SetUp() — тактирование, калибровка, последовательность

```cpp
SysInitStatus SetUp(SMPL smp1 = SMPL::DEFAULT, SMPL smp2 = SMPL::DEFAULT);   // G0
SysInitStatus SetUp();                                                       // F4/F7
```

`SetUp()` настраивает только ADC — последовательность, времена выборки и внутренние входы из вызовов
`AddChannel()`; без триггера и без DMA, они подключаются отдельно. Можно вызвать повторно: всё запущенное
останавливается, подключённые DMA/триггер остаются.

**Тактирование** (ниже предела f_ADC — G0: 35 МГц, F4/F7: 36 МГц):

- G0: синхронный PCLK/2 (`CFGR2.CKMODE = 01`); значение после сброса (асинхронный, SYSCLK) даёт 64 МГц.
- F4/F7: PCLK2/4 (`ADC->CCR.ADCPRE = 01`); по умолчанию /2 даёт 42 МГц при PCLK2 = 84 МГц. `CCR`
  общий для всех ADC.

**G0:** остановить/выключить (`ADSTP`, `ADDIS`) → такт → `ADVREGEN` + 1 мс → `ADCAL` → `SMPR`,
`CFGR1` → включение (ADEN выставляется повторно до `ADRDY`, как в HAL) → `CHSELR` + ожидание
`CCRDY` → GPIO в аналоговый режим.

**Режим секвенсора G0.** Полностью настраиваемый секвенсор (`CHSELRMOD = 1`) конвертирует в порядке
`AddChannel()`, как F4/F7, но у него 8 позиций с 4-битными полями: не больше 8 каналов и только
0–14. Если есть канал 15–18 или каналов больше 8, используется режим битовой маски — только по
возрастанию номера; тогда каналы нужно добавлять в этом порядке (без повторов), иначе `SetUp()`
вернёт `InitError`.

**F4/F7:** остановка → `CR2 = 0` → такт → `SMPR1/2` → `SQR1..3` → `CR1` (SCAN при > 1 канала) →
`CR2 = ADON` → 1 мс (t_STAB) → GPIO в аналоговый режим.

---

## 4. Запуск: Start, StartDMA, StartTrig, StartCont, Stop

```cpp
SysInitStatus AttachDMA(DMA_Sx* dma);                   // после SetUp()
void          Start();                                  // одна последовательность, без DMA
SysStatus     StartDMA(uint16_t* buf);                  // одна последовательность -> buf, затем стоп
SysStatus     StartTrig(uint16_t* buf, uint32_t len);   // по каждому триггеру, циклический DMA
SysStatus     StartCont(uint16_t* buf, uint32_t len);   // непрерывно, циклический DMA
SysStatus     StartCont();                              // непрерывно, один канал, без DMA
void          Stop();                                   // останавливает всё
bool          IsBusy() const;                           // идёт StartDMA / StartTrig / StartCont
```

| | `Start()` | `StartDMA(buf)` | `StartTrig(buf, len)` | `StartCont(buf, len)` | `StartCont()` |
|---|---|---|---|---|---|
| Требует | — | `AttachDMA()` | `AttachDMA()` + `AttachTrig()`/`AttachExtTrig()` | `AttachDMA()` | ровно один канал |
| Преобразования | последовательность один раз | последовательность один раз | последовательность на каждый фронт триггера | подряд (`CONT`) | подряд (`CONT`) |
| DMA | — | обычный, `GetChannelCount()` отсчётов | циклический по `len` | циклический по `len` | — |
| DMA-запросы ADC | — | однократные (`DMACFG`/`DDS` = 0) | циклические (`DMACFG`/`DDS` = 1) | циклические | — |
| Результат | `GetData()` | `buf` | `buf` | `buf` | `GetData()` — всегда последнее значение |
| Окончание | последовательность готова (EOC/EOS) | само: `IsBusy()` = false; DMA-колбэк, если включён `IRQ::DMA_TC` | `Stop()`; DMA-колбэк после каждых `len` отсчётов | `Stop()`; DMA-колбэк после каждых `len` отсчётов | `Stop()` |
| Таймер | — | — | запускается последним в `StartTrig()`, останавливается в `Stop()` | — | — |

`AttachDMA()` настраивает поток: периферия → память, полуслово, MINC, источник `&ADCx->DR`, запрос
из `adc_table` (G0: DMAMUX ID 5; F4/F7: DMA2 CHSEL 0/1/2). Возвращает `InitError`, если `SetUp()` не
вызывался, `dma` равен nullptr или поток неизвестен/уже занят.

`StartTrig()`/`StartCont(buf, len)` возвращают `Error`, если не подключён DMA (или, для
`StartTrig()`, триггер) либо `len` не кратен `GetChannelCount()`, равен нулю или больше 65535.
`StartCont()` возвращает `Error`, если в последовательности не ровно один канал. Все они возвращают
`Busy`, пока что-то идёт.

`StartCont()` без DMA намеренно перезаписывает непрочитанные результаты: на G0 включается
`OVRMOD = 1`; на F4/F7 без DMA и при `EOCS = 0` переполнение вообще не фиксируется. Флаг OVR этот
режим не останавливает.

`Stop()` останавливает таймер-триггер, преобразования (G0: ADSTP; F4/F7: сброс EXTEN и CONT —
текущее преобразование доходит до конца), снимает биты DMA/CONT/OVRMOD у ADC и выключает поток DMA.

В циклических режимах DMA-колбэк выполняется, пока DMA продолжает писать с начала `buf`; забирайте
данные быстро (колбэк на половину буфера пока не сделан).

---

## 5. Триггеры: AttachTrig() и AttachExtTrig()

```cpp
SysInitStatus AttachTrig(TIM_TriggerGenerator* trg);   // таймер
SysInitStatus AttachExtTrig();                         // линия EXTI 11
```

Таймер-триггер создаётся так же, как поток DMA — таймер плюс запрос, который говорит, что он
запускает, — и настраивается своим периодом; затем подключается:

```cpp
TIM_TriggerGenerator trg(TIM3, TIM_TriggerGenerator::Req::Adc::TIM3_TRGO);  // как DMA_Sx(ch, Req)
trg.SetUp(10000);                  // запрос TRGO / TRGO2: достаточно частоты

TIM_TriggerGenerator cc(TIM1, TIM_TriggerGenerator::Req::Adc::TIM1_CC1);
cc.SetUp(10000, 1000, 250);        // запрос CCx: частота, период (такты), точка сравнения

adc.AttachTrig(&trg);
```

- Конструктор `TIM_TriggerGenerator` проверяет, что запрос относится к этому таймеру (иначе
  `System::DebugTrap()`); `AttachTrig()` проверяет, что запрос предназначен для ADC, и берёт из него
  значение EXTSEL. Никакого поиска, ничего другого ADC не настраивает.
- `StartTrig()` запускает таймер, `Stop()` останавливает.
- `AttachExtTrig()` вместо этого выбирает линию EXTI 11 (нарастающий фронт); саму линию EXTI (пин,
  фронт) настраивает приложение.
- Пока идёт сбор, подключение отклоняется (`InitError`).

Запросы (`TIM_TriggerGenerator::Req::Adc::…`, файл [tim_trig_defs.hpp](src/tim_trig_defs.hpp);
значения EXTSEL из LL-заголовков ST; каждый есть только там, где есть его таймер):

| G0 | F4 | F7 |
|---|---|---|
| TIM1_TRGO2, TIM1_CC4, TIM2_TRGO, TIM3_TRGO, TIM15_TRGO, TIM6_TRGO, TIM4_TRGO | TIM1_CC1/2/3, TIM2_CC2/3/4, TIM2_TRGO, TIM3_CC1, TIM3_TRGO, TIM4_CC4, TIM5_CC1/2/3, TIM8_CC1, TIM8_TRGO | TIM1_CC1/2/3, TIM2_CC2, TIM5_TRGO, TIM4_CC4, TIM3_CC4, TIM8_TRGO/TRGO2, TIM1_TRGO/TRGO2, TIM2_TRGO, TIM4_TRGO, TIM6_TRGO |

Таблицы у семейств разные: при переносе кода с F4 на F7 нужно выбрать запрос, который там есть
(например, на F7 нет `TIM3_TRGO`) — отсутствующий даёт ошибку компиляции.

---

## 6. Прерывания и колбэки

```cpp
void IRQ_en(IRQ irq, FunctionalState en);   // IRQ::EOC, IRQ::EOS, IRQ::OVR, IRQ::DMA_TC
void SetEOCCallback(void (*cb)(void));
void SetEOSCallback(void (*cb)(void));
void SetOVRCallback(void (*cb)(void));
void SetDMACallback(void (*cb)(void));      // StartDMA: один раз; StartTrig: каждый проход
```

- **Все прерывания выключены, пока их не включит `IRQ_en()`** — сам драйвер не включает ни одного,
  даже DMA. `IRQ::DMA_TC` — прерывание окончания передачи подключённого потока DMA (нужен
  `AttachDMA()`); только с ним вызывается DMA-колбэк. Без него `StartDMA()` всё равно завершается:
  железо останавливается само, а `IsBusy()` видит флаг окончания передачи DMA.
- **EOC/EOS на F4/F7.** Отдельного прерывания EOS нет; драйвер держит `EOCS = 0`, поэтому EOC
  срабатывает раз на последовательность. `IRQ::EOS` — псевдоним `IRQ::EOC`, вызываются оба колбэка.
- **OVR** (если включено) останавливает идущий сбор с DMA (`Stop()`), затем вызывает OVR-колбэк; `StartCont()` без DMA продолжает работать.
- **`HandleIRQ()` обрабатывает только разрешённые источники.** Тот же объект зарегистрирован и на
  линии DMA, поэтому флаги маскируются по `IER`/`CR1`.
- **Общие линии.** F4/F7: ADC1/2/3 делят `ADC_IRQn`; G0 с компараторами: `ADC1_COMP_IRQn`.
  `IRQ_en()` снимает только себя и маскирует NVIC, только когда на линии никого не осталось.

---

## 7. Примеры использования

### Однократное преобразование с опросом (G0)

```cpp
ADC_N adc(ADC1);
adc.AddChannel(ADC_N::IN::PA1);
adc.SetUp(ADC_N::SMPL::CYC_39_5);
adc.Start();
while (!adc.IsEOC()) {}
uint16_t v = adc.GetData();
```

### Непрерывно, один канал, последнее значение (F4)

```cpp
ADC_N adc(ADC1);
adc.AddChannel(ADC_N::_1::PA0, ADC_N::SMPL::CYC_84);
adc.SetUp();
adc.StartCont();                      // работает до Stop()
...
uint16_t v = adc.GetData();           // последнее преобразование
```

### Одна последовательность через DMA (F4)

```cpp
static uint16_t res[3];
DMA_Sx dma(DMA_Sx::Req::Adc1::RX);
ADC_N  adc(ADC1);
adc.AddChannel(ADC_N::_1::PA0);
adc.AddChannel(ADC_N::_1::PA1);
adc.AddChannel(ADC_N::_1::PC2);
adc.SetUp();
adc.AttachDMA(&dma);
adc.StartDMA(res);                    // 3 отсчёта, затем стоп
while (adc.IsBusy()) {}
```

### Пины и внутренние входы с разным временем выборки (G0)

```cpp
ADC_N adc(ADC1);
adc.AddChannel(ADC_N::IN::PA0);                                  // SMP1
adc.AddChannel(ADC_N::IN::TEMP,    ADC_N::SMP_SEL::SMP2);
adc.AddChannel(ADC_N::IN::VREFINT, ADC_N::SMP_SEL::SMP2);
adc.SetUp(ADC_N::SMPL::CYC_12_5, ADC_N::SMPL::CYC_160_5);       // SMP1, SMP2; включаются TSEN и VREFEN
```

### Сканирование по таймеру в циклический буфер (G0)

```cpp
static uint16_t buf[2 * 8];           // 8 последовательностей по 2 канала

TIM_TriggerGenerator trg(TIM3, TIM_TriggerGenerator::Req::Adc::TIM3_TRGO);
trg.SetUp(1000);                      // 1000 последовательностей/с
DMA_Sx dma(DMA1_Channel1, DMA_Sx::Req::Adc1::RX);

ADC_N adc(ADC1);
adc.AddChannel(ADC_N::IN::PA0);
adc.AddChannel(ADC_N::IN::PB0);
adc.SetUp(ADC_N::SMPL::CYC_39_5);
adc.AttachDMA(&dma);
adc.AttachTrig(&trg);
adc.SetDMACallback(on_buffer_full);   // каждые 8 мс
adc.IRQ_en(ADC_N::IRQ::DMA_TC, ENABLE);
adc.StartTrig(buf, 16);               // циклический DMA + TIM3
...
adc.Stop();
```

### Триггер от канала сравнения в заданной точке периода (F4, TIM1 CC1)

```cpp
TIM_TriggerGenerator trg(TIM1, TIM_TriggerGenerator::Req::Adc::TIM1_CC1);
trg.SetUp(20000, 1000, 500);          // 20 кГц, триггер в середине периода
adc.AttachTrig(&trg);
adc.StartTrig(buf, 3 * 32);
```

---

## 8. Угловые случаи

- **G0 с каналами 15–18 или больше чем 8 каналами требует возрастающего порядка** — иначе `SetUp()`
  вернёт `InitError` (§3).
- **На F4/F7 `Stop()` не может прервать идущее преобразование** — ADSTP нет, оно доходит до конца.
- **`ADC->CCR.ADCPRE` общий** для всех ADC на F4/F7 — `SetUp()` любого экземпляра ставит PCLK2/4 для
  всех.
- **В `StartTrig()` DMA-колбэк вызывается, пока DMA продолжает писать** — начало `buf` уже
  перезаписывается.
- **При переполнении сбор останавливается** и `IsBusy()` становится false.
- **`TIM_TriggerGenerator` — только источник триггера**: без пинов и прерываний; таймер, который
  выдаёт ШИМ, подключить как триггер нельзя.
- **Калибровка G0 повторяется при каждом `SetUp()`** — около 1 мс (`System::Delay_ms(1)`, нужен SysTick).

---

## См. также

- [TIM.md](TIM.md) — `TIM_TriggerGenerator` (§7.9), запросы триггеров.
- [DMA.md](DMA.md) — `DMA_Sx`, таблицы запросов (`Req::Adc1..3`), паттерн `AttachDMA()`.
- [IRQ_Registry.md](IRQ_Registry.md) — регистрация и диспетчеризация за `IRQ_en()`/`HandleIRQ()`.
- [GPIO.md](GPIO.md) — `PIN`, режим `ANALOG` для входов ADC.
- [System.md](System.md) — `SysStatus`/`SysInitStatus`, `Delay_ms()`, `DebugTrap()`.
