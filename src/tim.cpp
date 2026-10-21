#include "tim.hpp"

#define TIM_DEFS_CPP
#include "tim_defs.hpp"
#undef TIM_DEFS_CPP

SysInitStatus TIM::Init()
{
	if (TIMx) return SysInitStatus::InitError;
	// Object-to-object binding cannot be read through TIM* during constinit.
	if (_counter && (_counter == this || _counter->_instance.periph == _instance.periph
		|| FindITR(GetTIMx(), _counter->GetTIMx()) < 0))
		return SysInitStatus::InitError;
	TIMx = reinterpret_cast<TIM_TypeDef*>(_instance.periph);
	TIM_SET_BIT(*reinterpret_cast<volatile uint32_t*>(_instance.clk_reg), _instance.clk_bit);
	(void)*reinterpret_cast<volatile uint32_t*>(_instance.clk_reg);
	TIM_SET_BIT(*reinterpret_cast<volatile uint32_t*>(_instance.rst_reg), _instance.rst_bit);
	TIM_CLEAR_BIT(*reinterpret_cast<volatile uint32_t*>(_instance.rst_reg), _instance.rst_bit);

	const bool up_enabled = NVIC_GetEnableIRQ(_instance.irq_up) != 0;
	const bool separate_cc = _instance.irq_cc != _instance.irq_up;
	const bool cc_enabled = separate_cc && NVIC_GetEnableIRQ(_instance.irq_cc) != 0;
	NVIC_DisableIRQ(_instance.irq_up);
	if (separate_cc) NVIC_DisableIRQ(_instance.irq_cc);
	bool registered = IRQ_Registry::Register(_instance.irq_up, this) == SysInitStatus::InitOK;
	if (registered && separate_cc
		&& IRQ_Registry::Register(_instance.irq_cc, this) != SysInitStatus::InitOK) {
		IRQ_Registry::Unregister(_instance.irq_up, this);
		registered = false;
	}
	if (!registered) {
		TIM_CLEAR_BIT(*reinterpret_cast<volatile uint32_t*>(_instance.clk_reg), _instance.clk_bit);
		TIMx = nullptr;
		if (up_enabled) NVIC_EnableIRQ(_instance.irq_up);
		if (cc_enabled) NVIC_EnableIRQ(_instance.irq_cc);
		return SysInitStatus::InitError;
	}
	NVIC_EnableIRQ(_instance.irq_up);
	if (separate_cc) NVIC_EnableIRQ(_instance.irq_cc);
	return SysInitStatus::InitOK;
}

SysInitStatus TIM::SetFrequency(uint32_t freq, uint32_t arr)
{
	if (!TIMx || !freq) return SysInitStatus::InitError;
	const uint32_t clock = *_instance.bus_clk;
	if (freq > clock || (arr && arr - 1u > _instance.arr_max))
		return SysInitStatus::InitError;
	const uint32_t ratio = clock / freq;
	uint32_t psc;
	if (!arr) {
		// Minimum divider that fits a 16-bit period; 32-bit timers need no divider.
		constexpr uint32_t period_16bit = 0x10000u;
		psc = _instance.arr_max == UINT32_MAX ? 1u : (ratio - 1u) / period_16bit + 1u;
		arr = ratio / psc;
	} else {
		// floor(floor(clock/freq)/arr) == floor(clock/(freq*arr)); no wide multiply.
		psc = ratio / arr;
	}
	// arr is the period in ticks, not the register value. 65536 fits a 16-bit ARR.
	if (!psc || psc > 0x10000u || !arr || arr - 1u > _instance.arr_max)
		return SysInitStatus::InitError;
	TIMx->PSC = psc - 1u;
	TIMx->ARR = arr - 1u;
	const uint32_t cr1 = TIMx->CR1;
	if (!(cr1 & TIM_CR1_CEN)) {
		// Load PSC before the first period. URS suppresses a synthetic IRQ/DMA request.
		TIM_MODIFY_REG(TIMx->CR1, TIM_CR1_URS | TIM_CR1_UDIS, TIM_CR1_URS);
		TIMx->EGR = TIM_EGR_UG;
		TIMx->SR = ~TIM_SR_UIF;
		TIM_MODIFY_REG(TIMx->CR1, TIM_CR1_URS | TIM_CR1_UDIS, cr1 & (TIM_CR1_URS | TIM_CR1_UDIS));
	}
	return SysInitStatus::InitOK;
}

SysInitStatus TIM::SetUp(uint32_t freq, void (*cb)(void), uint32_t arr)
{
    SysInitStatus s = Init();
    if (s != SysInitStatus::InitOK) return s;
    s = SetFrequency(freq, arr);
    if (s != SysInitStatus::InitOK) { DeInit(); return s; }
    _period_explicit = arr != 0;
    _update_cb = cb;
    TIMx->SR = 0;
    if (cb) TIM_SET_BIT(TIMx->DIER, TIM_DIER_UIE);
    return SysInitStatus::InitOK;
}

SysInitStatus TIM::SetUpPWM(Channel ch, uint32_t value, void (*dma_callback)(void))
{
    const uint32_t i = static_cast<uint32_t>(ch);
    if (!CanSetUpChannel(i) || !_channels[i].IsValid()
        || GetPeriod() > _instance.arr_max || value > GetPeriod())
        return SysInitStatus::InitError;
    if (_channels[i].dma.HasSelection() && SetUpDMA(i) != SysInitStatus::InitOK)
        return SysInitStatus::InitError;

    _channels[i].ToPin().SetUp(PIN::TYPE::AF_PushPull, PIN::OUTPUT_SPEED::High);
    // Each CCMR holds two channel fields: low byte for CH1/CH3, high for CH2/CH4.
    const uint32_t shift = (i & 1U) * 8U;
    volatile uint32_t& ccmr = i < 2 ? TIMx->CCMR1 : TIMx->CCMR2;
    const uint32_t mask = (TIM_CCMR1_CC1S | TIM_CCMR1_OC1M | TIM_CCMR1_OC1PE) << shift;
    // Load this channel's initial CCR directly, then enable its preload.
    // No global UG: configuring one channel must not trigger other consumers.
    constexpr uint32_t pwm1 = TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2;
    TIM_MODIFY_REG(ccmr, mask, pwm1 << shift);
    *ccr(i) = value;
    TIM_SET_BIT(ccmr, TIM_CCMR1_OC1PE << shift);
    TIM_MODIFY_REG(TIMx->CCER, (TIM_CCER_CC1E | TIM_CCER_CC1P | TIM_CCER_CC1NP) << (i * 4U),
        TIM_CCER_CC1E << (i * 4U));
    if (_instance.has_bdtr) TIM_SET_BIT(TIMx->BDTR, TIM_BDTR_MOE);
    TIMx->SR = ~(TIM_SR_CC1IF << i);
    _dma_cb[i] = dma_callback;
    _pwm_channels |= static_cast<uint8_t>(1U << i);
    _channels_used |= static_cast<uint8_t>(1U << i);
    return SysInitStatus::InitOK;
}

SysInitStatus TIM::SetUpEncoder(uint32_t period, EncoderMode mode)
{
    const uint32_t sms = static_cast<uint32_t>(mode);
    if (TIMx || !period || !IS_TIM_ENCODER_INTERFACE_INSTANCE(GetTIMx())
        || sms < 1U || sms > 3U
        || !_channels[0].IsValid() || !_channels[1].IsValid()
        || _channels[0].dma.HasSelection() || _channels[1].dma.HasSelection())
        return SysInitStatus::InitError;
    if (Init() != SysInitStatus::InitOK) return SysInitStatus::InitError;
    if (period - 1U > _instance.arr_max) { DeInit(); return SysInitStatus::InitError; }
    _channels[0].ToPin().SetUp(PIN::TYPE::AF_PushPull);
    _channels[1].ToPin().SetUp(PIN::TYPE::AF_PushPull);
    TIMx->ARR = period - 1U;
    TIMx->CCMR1 = TIM_CCMR1_CC1S_0 | TIM_CCMR1_CC2S_0; // Direct TI1 and TI2.
    TIMx->CCER = TIM_CCER_CC1E | TIM_CCER_CC2E;
    TIM_MODIFY_REG(TIMx->SMCR, TIM_SMCR_SMS, sms);
    TIMx->CNT = 0;
    _channels_used = 3U;
    _mode = Mode::Encoder;
    return SysInitStatus::InitOK;
}

SysInitStatus TIM::SetUpQuadrature(Channel a, Channel b, uint32_t freq,
    uint32_t period, void (*cb)(void))
{
    const uint32_t ai = static_cast<uint32_t>(a), bi = static_cast<uint32_t>(b);
    if (TIMx || !_counter || _counter->TIMx || ai >= 4U || bi >= 4U || ai == bi
        || !freq || freq > UINT32_MAX / 2U || period < 4U || (period & 1U)
        || !_channels[ai].IsValid() || !_channels[bi].IsValid()
        || _channels[ai].dma.HasSelection() || _channels[bi].dma.HasSelection())
        return SysInitStatus::InitError;
    if (SetUp(freq * 2U, nullptr, period) != SysInitStatus::InitOK)
        return SysInitStatus::InitError;
    if (ai >= _instance.channel_count || bi >= _instance.channel_count
        || SetUpTrigger(Trigger::Update) != SysInitStatus::InitOK) {
        DeInit();
        return SysInitStatus::InitError;
    }
    // Use the counter's hardware range; no software extension or per-cycle IRQ.
    const uint32_t counter_period = IS_TIM_32B_COUNTER_INSTANCE(_counter->GetTIMx())
        ? UINT32_MAX : 0x10000U;
    if (_counter->SetUpCounter(this, counter_period, cb) != SysInitStatus::InitOK) {
        DeInit(); // Counter setup rolls back only its own failed acquisition.
        return SysInitStatus::InitError;
    }
    _counter->TIMx->PSC = 1U; // Two generator Updates = one full A/B cycle.
    TIM_SET_BIT(_counter->TIMx->CR1, TIM_CR1_URS);
    _counter->TIMx->EGR = TIM_EGR_UG;
    _counter->TIMx->SR = 0;

    for (const uint32_t i : {ai, bi}) {
        volatile uint32_t& ccmr = i < 2U ? TIMx->CCMR1 : TIMx->CCMR2;
        const uint32_t shift = (i & 1U) * 8U;
        // Inactive references before exposing the pins; GenPulse selects toggle.
        TIM_MODIFY_REG(ccmr, (TIM_CCMR1_CC1S | TIM_CCMR1_OC1M | TIM_CCMR1_OC1PE) << shift,
            TIM_CCMR1_OC1M_2 << shift);
        const uint32_t phase = period / 4U;
        *ccr(i) = i == ai ? phase : phase + period / 2U;
        _channels[i].ToPin().SetUp(PIN::TYPE::AF_PushPull, PIN::OUTPUT_SPEED::High);
        TIM_SET_BIT(TIMx->CCER, TIM_CCER_CC1E << (i * 4U));
    }
    if (_instance.has_bdtr) TIM_SET_BIT(TIMx->BDTR, TIM_BDTR_MOE);
    _channels_used = static_cast<uint8_t>((1U << ai) | (1U << bi));
    _quadrature_a = static_cast<uint8_t>(ai);
    _quadrature_b = static_cast<uint8_t>(bi);
    _mode = Mode::Quadrature;
    return SysInitStatus::InitOK;
}

SysStatus TIM::GenPulse(int32_t cycles)
{
    if (!TIMx) return SysStatus::NotInit;
    if (_mode != Mode::Quadrature) return SysStatus::Error;
    TIM_TypeDef* const counter = _counter->TIMx;
    if ((TIMx->CR1 | counter->CR1) & TIM_CR1_CEN) return SysStatus::Busy;
    if (!cycles) return SysStatus::OK;
    // Unsigned subtraction handles INT32_MIN without signed overflow.
    const uint32_t count = cycles < 0
        ? 0U - static_cast<uint32_t>(cycles) : static_cast<uint32_t>(cycles);
    if (count > counter->ARR) return SysStatus::Error;

    TIM_CLEAR_BIT(counter->DIER, TIM_DIER_CC1IE);
    for (const uint32_t i : {uint32_t(_quadrature_a), uint32_t(_quadrature_b)}) {
        volatile uint32_t& ccmr = i < 2U ? TIMx->CCMR1 : TIMx->CCMR2;
        const uint32_t shift = (i & 1U) * 8U;
        // Restore a known reference even after a manually interrupted series.
        TIM_MODIFY_REG(ccmr, TIM_CCMR1_OC1M << shift, TIM_CCMR1_OC1M_2 << shift);
    }
    const uint32_t polarity = TIM_CCER_CC1P << (uint32_t(_quadrature_a) * 4U);
    TIM_MODIFY_REG(TIMx->CCER, polarity, cycles < 0 ? polarity : 0U);
    // Enable-mode TRGO is LOW while stopped. Unlike Reset/Update mode it does
    // not forward this service UG to the cycle counter.
    TIM_MODIFY_REG(TIMx->CR2, TIM_CR2_MMS, TIM_CR2_MMS_0);
    TIMx->EGR = TIM_EGR_UG;
    TIMx->SR = ~TIM_SR_UIF;
    counter->EGR = TIM_EGR_UG; // Reset CNT and its divide-by-two prescaler phase.
    counter->CCR1 = count;
    counter->SR = ~(TIM_SR_UIF | TIM_SR_CC1IF);
    TIM_MODIFY_REG(TIMx->CR2, TIM_CR2_MMS, static_cast<uint32_t>(Trigger::Update));
    for (const uint32_t i : {uint32_t(_quadrature_a), uint32_t(_quadrature_b)}) {
        volatile uint32_t& ccmr = i < 2U ? TIMx->CCMR1 : TIMx->CCMR2;
        TIM_MODIFY_REG(ccmr, TIM_CCMR1_OC1M << ((i & 1U) * 8U),
            (TIM_CCMR1_OC1M_0 | TIM_CCMR1_OC1M_1) << ((i & 1U) * 8U));
    }
    TIM_SET_BIT(counter->DIER, TIM_DIER_CC1IE);
    TIM_SET_BIT(counter->CR1, TIM_CR1_CEN); // Arm the receiver before the source.
    TIM_SET_BIT(TIMx->CR1, TIM_CR1_CEN);
    return SysStatus::OK;
}

SysInitStatus TIM::SetUpPulseBurst(Channel ch, uint32_t freq, uint32_t period, void (*cb)(void))
{
    const uint32_t i = static_cast<uint32_t>(ch);
    if (TIMx || i >= 4U || !IS_TIM_REPETITION_COUNTER_INSTANCE(GetTIMx())
        || period < 2U || !_channels[i].IsValid() || _channels[i].dma.HasSelection())
        return SysInitStatus::InitError;
    if (SetUp(freq, cb, period) != SysInitStatus::InitOK) return SysInitStatus::InitError;
    if (i >= _instance.channel_count) {
        DeInit();
        return SysInitStatus::InitError;
    }
    // Select inactive-at-zero PWM2 before connecting/enabling the output.
    // Unlike PWM1, terminal overflow leaves LOW without an IRQ intervention.
    volatile uint32_t& ccmr = i < 2U ? TIMx->CCMR1 : TIMx->CCMR2;
    const uint32_t shift = (i & 1U) * 8U;
    constexpr uint32_t pwm2 = TIM_CCMR1_OC1M_0 | TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2;
    TIM_MODIFY_REG(ccmr, (TIM_CCMR1_CC1S | TIM_CCMR1_OC1M | TIM_CCMR1_OC1PE) << shift,
        pwm2 << shift);
    *ccr(i) = period - period / 2U;
    TIM_SET_BIT(ccmr, TIM_CCMR1_OC1PE << shift);
    _channels[i].ToPin().SetUp(PIN::TYPE::AF_PushPull, PIN::OUTPUT_SPEED::High);
    TIM_SET_BIT(TIMx->CCER, TIM_CCER_CC1E << (i * 4U));
    if (_instance.has_bdtr) TIM_SET_BIT(TIMx->BDTR, TIM_BDTR_MOE);
    TIM_SET_BIT(TIMx->CR1, TIM_CR1_OPM | TIM_CR1_URS);
    _channels_used = static_cast<uint8_t>(1U << i);
    _mode = Mode::Burst;
    return SysInitStatus::InitOK;
}

SysStatus TIM::RunPulses(uint32_t count)
{
    if (!TIMx) return SysStatus::NotInit;
    if (_mode != Mode::Burst) return SysStatus::Error;
    if (TIMx->CR1 & TIM_CR1_CEN) return SysStatus::Busy;
    if (!count) return SysStatus::OK;
    uint32_t rcr_max = TIM_RCR_REP;
#if defined(STM32G0)
    // The shared CMSIS mask is 16 bits, but only TIM1 has a 16-bit RCR.
    if (TIMx != TIM1) rcr_max = 0xFFU;
#endif
    if (count - 1U > rcr_max) return SysStatus::Error;
    TIMx->RCR = count - 1U;
    // URS suppresses the service IRQ; UG loads RCR/PSC/CCR and resets phase.
    TIMx->EGR = TIM_EGR_UG;
    TIMx->SR = ~TIM_SR_UIF;
    Start();
    return SysStatus::OK;
}

int TIM::FindITR(TIM_TypeDef* source, TIM_TypeDef* slave)
{
    for (const auto& route : itr_table)
        if (route.slave == slave)
            for (int i = 0; i < 4; ++i)
                if (route.source[i] == source) return i;
    return -1;
}

bool TIM::CanUseSource(const TIM* source) const
{
    return source && source != this && source->TIMx && source->_trigger_set
        && !(source->TIMx->CR1 & TIM_CR1_CEN) && IS_TIM_SLAVE_INSTANCE(GetTIMx());
}

SysInitStatus TIM::SetUpSlave(TIM* source, SlaveMode mode)
{
    const uint32_t sms = static_cast<uint32_t>(mode);
    if (!TIMx || _mode != Mode::Base || _slave_set || (TIMx->CR1 & TIM_CR1_CEN)
        || !CanUseSource(source) || sms < 4U || sms > 7U)
        return SysInitStatus::InitError;
    const int itr = FindITR(source->TIMx, TIMx);
    if (itr < 0) return SysInitStatus::InitError;
    TIM_MODIFY_REG(TIMx->SMCR, TIM_SMCR_TS | TIM_SMCR_SMS,
        (static_cast<uint32_t>(itr) << TIM_SMCR_TS_Pos) | sms);
    _slave_set = true;
    return SysInitStatus::InitOK;
}

SysInitStatus TIM::SetUpOnePulse()
{
    if (!TIMx || _mode != Mode::Base || _opm_set || (TIMx->CR1 & TIM_CR1_CEN))
        return SysInitStatus::InitError;
    TIM_SET_BIT(TIMx->CR1, TIM_CR1_OPM);
    _opm_set = true;
    return SysInitStatus::InitOK;
}

SysInitStatus TIM::SetUpCounter(TIM* source, uint32_t period, void (*cb)(void))
{
    if (TIMx || !period || !CanUseSource(source)
        || (source->TIMx->CR2 & TIM_CR2_MMS) != static_cast<uint32_t>(Trigger::Update))
        return SysInitStatus::InitError;
    const int itr = FindITR(source->TIMx, GetTIMx());
    if (itr < 0 || Init() != SysInitStatus::InitOK) return SysInitStatus::InitError;
    if (period - 1U > _instance.arr_max || !_instance.channel_count) {
        DeInit();
        return SysInitStatus::InitError;
    }
    TIMx->ARR = period - 1U;
    TIM_MODIFY_REG(TIMx->SMCR, TIM_SMCR_TS | TIM_SMCR_SMS,
        (static_cast<uint32_t>(itr) << TIM_SMCR_TS_Pos)
        | static_cast<uint32_t>(SlaveMode::ExternalClock));
    _channels_used = 1U;
    _cc_cb[0] = cb;
    _master = source;
    _mode = Mode::Counter;
    return SysInitStatus::InitOK;
}

SysStatus TIM::StopAfter(uint32_t count)
{
    if (!TIMx) return SysStatus::NotInit;
    if (_mode != Mode::Counter || !count) return SysStatus::Error;
    const uint32_t current = TIMx->CNT;
    if (count > TIMx->ARR - current) return SysStatus::Error;
    return StopAt(current + count);
}

SysStatus TIM::StopAt(uint32_t target)
{
    if (!TIMx) return SysStatus::NotInit;
    if (_mode != Mode::Counter) return SysStatus::Error;
    if ((TIMx->CR1 | _master->TIMx->CR1) & TIM_CR1_CEN) return SysStatus::Busy;
    if (target <= TIMx->CNT || target > TIMx->ARR) return SysStatus::Error;
    TIMx->CCR1 = target;
    TIMx->SR = ~TIM_SR_CC1IF;
    TIM_SET_BIT(TIMx->DIER, TIM_DIER_CC1IE);
    return SysStatus::OK;
}

SysInitStatus TIM::SetUpPulseMeasure(Channel input, void (*cb)(void))
{
    const uint32_t i = static_cast<uint32_t>(input);
    if (i > 1U || !CanSetUpChannel(0) || !CanSetUpChannel(1) || !_period_explicit
        || _slave_set || !IS_TIM_SLAVE_INSTANCE(TIMx) || !_channels[i].IsValid()
        || _channels[0].dma.HasSelection() || _channels[1].dma.HasSelection())
        return SysInitStatus::InitError;
    _channels[i].ToPin().SetUp(PIN::TYPE::AF_PushPull);
    const uint32_t mapping = i == 0U
        ? TIM_CCMR1_CC1S_0 | TIM_CCMR1_CC2S_1
        : TIM_CCMR1_CC1S_1 | TIM_CCMR1_CC2S_0;
    constexpr uint32_t input_fields = TIM_CCMR1_CC1S | TIM_CCMR1_IC1PSC | TIM_CCMR1_IC1F
        | TIM_CCMR1_CC2S | TIM_CCMR1_IC2PSC | TIM_CCMR1_IC2F;
    TIM_MODIFY_REG(TIMx->CCMR1, input_fields, mapping);
    constexpr uint32_t ccer_mask = TIM_CCER_CC1E | TIM_CCER_CC1P | TIM_CCER_CC1NP
        | TIM_CCER_CC2E | TIM_CCER_CC2P | TIM_CCER_CC2NP;
    TIM_MODIFY_REG(TIMx->CCER, ccer_mask, TIM_CCER_CC1E | TIM_CCER_CC2E
        | (i == 0U ? TIM_CCER_CC2P : TIM_CCER_CC1P));
    constexpr uint32_t ti1fp1 = 5U, ti2fp2 = 6U;
    TIM_MODIFY_REG(TIMx->SMCR, TIM_SMCR_TS | TIM_SMCR_SMS,
        ((i == 0U ? ti1fp1 : ti2fp2) << TIM_SMCR_TS_Pos)
        | static_cast<uint32_t>(SlaveMode::Reset));
    TIMx->SR = ~(TIM_SR_CC1IF | TIM_SR_CC2IF | TIM_SR_CC1OF | TIM_SR_CC2OF);
    _channels_used |= 3U;
    _capture_channels |= 3U;
    _slave_set = true;
    _cc_cb[i] = cb;
    if (cb) TIM_SET_BIT(TIMx->DIER, TIM_DIER_CC1IE << i);
    return SysInitStatus::InitOK;
}

SysInitStatus TIM::SetUpCapture(Channel ch, void (*cb)(void))
{
    const uint32_t i = static_cast<uint32_t>(ch);
    if (!CanSetUpChannel(i) || !_period_explicit || !_channels[i].IsValid() || _channels[i].dma.HasSelection())
        return SysInitStatus::InitError;
    _channels[i].ToPin().SetUp(PIN::TYPE::AF_PushPull);
    const uint32_t shift = (i & 1U) * 8U;
    volatile uint32_t& ccmr = i < 2 ? TIMx->CCMR1 : TIMx->CCMR2;
    // Direct TIx; clear this channel's input mapping, prescaler and filter fields.
    constexpr uint32_t input_mask = TIM_CCMR1_CC1S | TIM_CCMR1_IC1PSC | TIM_CCMR1_IC1F;
    TIM_MODIFY_REG(ccmr, input_mask << shift, TIM_CCMR1_CC1S_0 << shift);
    const uint32_t ccer_mask = (TIM_CCER_CC1E | TIM_CCER_CC1P | TIM_CCER_CC1NP) << (i * 4U);
    TIM_MODIFY_REG(TIMx->CCER, ccer_mask, TIM_CCER_CC1E << (i * 4U));
    TIMx->SR = ~((TIM_SR_CC1IF | TIM_SR_CC1OF) << i);
    _cc_cb[i] = cb;
    _capture_channels |= static_cast<uint8_t>(1U << i);
    _channels_used |= static_cast<uint8_t>(1U << i);
    if (cb) TIM_SET_BIT(TIMx->DIER, TIM_DIER_CC1IE << i);
    return SysInitStatus::InitOK;
}

SysInitStatus TIM::SetCompareIRQ(Channel ch, uint32_t compare, void (*cb)(void))
{
    const uint32_t i = static_cast<uint32_t>(ch);
    if (!CanSetUpChannel(i) || compare > TIMx->ARR) return SysInitStatus::InitError;
    *ccr(i) = compare; // Frozen output compare, no GPIO output and no preload.
    TIMx->SR = ~(TIM_SR_CC1IF << i);
    _cc_cb[i] = cb;
    _channels_used |= static_cast<uint8_t>(1U << i);
    if (cb) TIM_SET_BIT(TIMx->DIER, TIM_DIER_CC1IE << i);
    return SysInitStatus::InitOK;
}

SysInitStatus TIM::SetUpTrigger(Trigger trigger)
{
    const uint32_t mms = static_cast<uint32_t>(trigger);
    if (!TIMx || _trigger_set || (TIMx->CR1 & TIM_CR1_CEN) || !IS_TIM_MASTER_INSTANCE(TIMx)
        || (mms & ~TIM_CR2_MMS) || mms < static_cast<uint32_t>(Trigger::Update))
        return SysInitStatus::InitError;
    if (trigger == Trigger::ComparePulse && !(_channels_used & 1U)) return SysInitStatus::InitError;
    if (mms >= static_cast<uint32_t>(Trigger::OC1Ref)) {
        const uint32_t i = (mms >> TIM_CR2_MMS_Pos) - 4U;
        if (!(_pwm_channels & (1U << i))) return SysInitStatus::InitError;
    }
    TIM_MODIFY_REG(TIMx->CR2, TIM_CR2_MMS, mms);
    _trigger_set = true;
    return SysInitStatus::InitOK;
}

void TIM::IRQ_en(IRQ irq, FunctionalState en)
{
    if (!TIMx) return;
    const uint32_t bit = static_cast<uint32_t>(irq);
    if (irq != IRQ::UE && (bit < TIM_DIER_CC1IE || bit >= (TIM_DIER_CC1IE << _instance.channel_count))) return;
    if (en) TIM_SET_BIT(TIMx->DIER, bit); else TIM_CLEAR_BIT(TIMx->DIER, bit);
}

SysStatus TIM::SetCCR(Channel ch, uint32_t value)
{
    if (!TIMx) return SysStatus::NotInit;
    const uint32_t i = static_cast<uint32_t>(ch);
    if (i >= _instance.channel_count || !(_pwm_channels & (1U << i)) || value > GetPeriod()) return SysStatus::Error;
    if (_dma_status[i] == SysStatus::Busy) return SysStatus::Busy;
    *ccr(i) = value;
    return SysStatus::OK;
}

SysStatus TIM::SetDuty(Channel ch, uint32_t percent)
{
    if (!TIMx) return SysStatus::NotInit;
    if (percent > 100U) percent = 100U;
    const uint32_t period = GetPeriod();
    // Exact floor(period*percent/100), without a wide multiply/divide or overflow.
    return SetCCR(ch, (period / 100U) * percent + ((period % 100U) * percent) / 100U);
}

uint32_t TIM::GetCapture(Channel ch) const
{
    const uint32_t i = static_cast<uint32_t>(ch);
    if (!TIMx || i >= _instance.channel_count || !(_capture_channels & (1U << i))) return 0;
    return *ccr(i);
}

void TIM::HandleIRQ()
{
    if (!TIMx) return;
    constexpr uint32_t events = TIM_SR_UIF | TIM_SR_CC1IF | TIM_SR_CC2IF | TIM_SR_CC3IF | TIM_SR_CC4IF;
    const uint32_t sr = TIMx->SR & TIMx->DIER & events;
    // Acknowledge enabled events only. Capture callbacks notify; CCR is read
    // directly by GetCapture, with no RAM snapshot or hidden data access here.
    TIMx->SR = ~sr;
    if ((sr & TIM_SR_UIF) && _update_cb) _update_cb();
    if ((sr & TIM_SR_CC1IF) && _mode == Mode::Counter) {
        // Disarm first: no callback replacement or repeated completion on wrap.
        TIM_CLEAR_BIT(TIMx->DIER, TIM_DIER_CC1IE);
        // Both resources are known here. Direct CEN writes avoid walking a
        // quadrature pair again through the public Stop functions.
        TIM_CLEAR_BIT(_master->TIMx->CR1, TIM_CR1_CEN);
        TIM_CLEAR_BIT(TIMx->CR1, TIM_CR1_CEN);
    }
    if ((sr & TIM_SR_CC1IF) && _cc_cb[0]) _cc_cb[0]();
    if ((sr & TIM_SR_CC2IF) && _cc_cb[1]) _cc_cb[1]();
    if ((sr & TIM_SR_CC3IF) && _cc_cb[2]) _cc_cb[2]();
    if ((sr & TIM_SR_CC4IF) && _cc_cb[3]) _cc_cb[3]();
}

SysInitStatus TIM::SetUpDMA(uint32_t ch)
{
    DMA_Sx* const dma = &_channels[ch].dma;
#if defined(STM32F4) || defined(STM32F7)
#ifdef TIM2_BASE
    // Stream6/CH3 is wired to both CH2 and CH4. Those requests cannot be
    // independent when both are emitted at the same Update (CCDS=1).
    if (TIMx == TIM2 && ((ch == 1U && DmaOwned(3)) || (ch == 3U && DmaOwned(1))))
        return SysInitStatus::InitError;
#endif
#endif
    DMA_Sx::StreamSettings cfg;
    cfg.direction = DMA_Sx::DIR::To_Per;
    cfg.data_size = _instance.arr_max == UINT32_MAX ? DMA_Sx::SIZE::Word : DMA_Sx::SIZE::Half_Word;
    cfg.minc = true;
    cfg.per_address = reinterpret_cast<uint32_t>(ccr(ch));
    if (dma->SetUp(cfg) != SysInitStatus::InitOK) return SysInitStatus::InitError;
    const IRQn_Type irq = dma->GetIRQn();
    const bool enabled = NVIC_GetEnableIRQ(irq) != 0;
    NVIC_DisableIRQ(irq);
    if (IRQ_Registry::Register(irq, DmaHandler(ch)) != SysInitStatus::InitOK) {
        dma->Release();
        if (enabled) NVIC_EnableIRQ(irq);
        return SysInitStatus::InitError;
    }
    _dma_status[ch] = SysStatus::OK;
    TIM_SET_BIT(TIMx->CR2, TIM_CR2_CCDS); // All CC DMA requests are generated on Update.
    dma->Enable_IRQ(DMA_Sx::IRQ::TC);
    dma->Enable_IRQ(DMA_Sx::IRQ::TE);
#if defined(STM32F4) || defined(STM32F7)
    dma->Enable_IRQ(DMA_Sx::IRQ::DME);
#endif
    return SysInitStatus::InitOK;
}

SysStatus TIM::SendDMA(Channel ch, const uint16_t* data, uint32_t len)
{
    return StartDMA(ch, data, len, DMA_Sx::SIZE::Half_Word);
}

SysStatus TIM::SendDMA(Channel ch, const uint32_t* data, uint32_t len)
{
    return StartDMA(ch, data, len, DMA_Sx::SIZE::Word);
}

SysStatus TIM::StartDMA(Channel ch, const void* data, uint32_t len, DMA_Sx::SIZE width)
{
    if (!TIMx) return SysStatus::NotInit;
    const uint32_t i = static_cast<uint32_t>(ch);
    const uint32_t bytes = width == DMA_Sx::SIZE::Word ? 4U : 2U;
    if (i >= _instance.channel_count || !DmaOwned(i) || !data || !len || len > 65535U
        || ((_instance.arr_max == UINT32_MAX) != (width == DMA_Sx::SIZE::Word))
        || (reinterpret_cast<uintptr_t>(data) & (bytes - 1U))) return SysStatus::Error;
    if (_dma_status[i] == SysStatus::Busy) return SysStatus::Busy;
    DMA_Sx* const dma = &_channels[i].dma;
    if (dma->SetMemAddr(reinterpret_cast<uint32_t>(data)) != SysStatus::OK
        || dma->SetCount(len) != SysStatus::OK) return SysStatus::Error;
    dma->ClearFlags();
    System::CacheClean(data, len * bytes);
    _dma_error[i] = false;
    dma->Enable_IRQ(DMA_Sx::IRQ::TE, false);
#if defined(STM32F4) || defined(STM32F7)
    dma->Enable_IRQ(DMA_Sx::IRQ::DME, false);
#endif
    _dma_status[i] = SysStatus::Busy;
    __DMB();
    dma->Stream_EN(ENABLE); // Arm DMA before allowing this channel's requests.
    TIM_SET_BIT(TIMx->DIER, TIM_DIER_CC1DE << i);
    return SysStatus::OK;
}

void TIM::HandleDmaIRQ(uint32_t ch)
{
    if (_dma_status[ch] != SysStatus::Busy) return;
    DMA_Sx* const dma = &_channels[ch].dma;
    const uint32_t flags = dma->GetIRQFlags();
    const uint32_t errors = flags & DMA_Sx::FLAG_ERRORS;
    if (errors) {
        _dma_error[ch] = true;
        TIM_CLEAR_BIT(TIMx->DIER, (TIM_DIER_CC1DE << ch));
        dma->Disable_IRQ(DMA_Sx::IRQ::TE);
#if defined(STM32F4) || defined(STM32F7)
        dma->Disable_IRQ(DMA_Sx::IRQ::DME);
#endif
        dma->RequestStop();
    }
#if defined(STM32F4) || defined(STM32F7)
    if (!(flags & DMA_Sx::FLAG_TC) && !((errors & DMA_LISR_TEIF0) && !dma->IsEnabled())) return;
#else
    if (!(flags & DMA_Sx::FLAG_TC) && !errors) return;
#endif
    TIM_CLEAR_BIT(TIMx->DIER, (TIM_DIER_CC1DE << ch));
    dma->RequestStop();
    dma->ClearFlags();
    __DMB();
    _dma_status[ch] = _dma_error[ch] ? SysStatus::Error : SysStatus::OK;
    if (_dma_cb[ch]) _dma_cb[ch]();
}

void TIM::DeInit()
{
    if (!TIMx) return;
    TIMx->DIER = 0;
    Stop();
    if (_mode == Mode::Quadrature) _counter->DeInit();
    for (uint32_t i = 0; i < 4; ++i) {
        if (!DmaOwned(i)) continue;
        DMA_Sx* const dma = &_channels[i].dma;
        const IRQn_Type irq = dma->GetIRQn();
        const bool enabled = NVIC_GetEnableIRQ(irq) != 0;
        NVIC_DisableIRQ(irq);
        dma->Stream_EN(DISABLE); // Main only; no waits exist in the IRQ handlers.
#if defined(STM32F4) || defined(STM32F7)
        // EN can clear before the DMA FIFO finishes. TC confirms forced stop;
        // a hardware TE terminates the stream without a normal completion.
        while (_dma_status[i] == SysStatus::Busy
            && !(dma->GetIRQFlags() & (DMA_Sx::FLAG_TC | DMA_LISR_TEIF0))) {}
#endif
        IRQ_Registry::Unregister(irq, DmaHandler(i));
        dma->Release();
        if (IRQ_Registry::HasHandlers(irq)) {
            if (enabled) NVIC_EnableIRQ(irq);
        } else NVIC_ClearPendingIRQ(irq);
    }
    const IRQn_Type irqs[2] = {_instance.irq_up, _instance.irq_cc};
    for (unsigned i = 0; i < 2; ++i) {
        if (i && irqs[i] == irqs[0]) continue;
        const bool enabled = NVIC_GetEnableIRQ(irqs[i]) != 0;
        NVIC_DisableIRQ(irqs[i]);
        IRQ_Registry::Unregister(irqs[i], this);
        if (IRQ_Registry::HasHandlers(irqs[i])) {
            if (enabled) NVIC_EnableIRQ(irqs[i]);
        } else NVIC_ClearPendingIRQ(irqs[i]);
    }
    TIM_SET_BIT(*reinterpret_cast<volatile uint32_t*>(_instance.rst_reg), _instance.rst_bit);
    TIM_CLEAR_BIT(*reinterpret_cast<volatile uint32_t*>(_instance.rst_reg), _instance.rst_bit);
    TIM_CLEAR_BIT(*reinterpret_cast<volatile uint32_t*>(_instance.clk_reg), _instance.clk_bit);
    _channels_used = _pwm_channels = _capture_channels = 0;
    _update_cb = nullptr;
    for (unsigned i = 0; i < 4; ++i) {
        _cc_cb[i] = _dma_cb[i] = nullptr;
        _dma_error[i] = false;
        _dma_status[i] = SysStatus::NotInit;
    }
    _period_explicit = _trigger_set = false;
    _mode = Mode::Base;
    _slave_set = _opm_set = false;
    _quadrature_a = _quadrature_b = 0;
    _master = nullptr;
    TIMx = nullptr;
}

// Previous mode implementation, retained only for reference; not compiled.
// // ---------------------------------------------------------------------------
// // TIM_PeriodicIRQ
// // ---------------------------------------------------------------------------
//
// SysInitStatus TIM_PeriodicIRQ::SetUp(uint32_t freq, void (*cb)(void), uint32_t arr)
// {
// 	SysInitStatus s = Init();
// 	if (s != SysInitStatus::InitOK) return s;
// 	s = SetFrequency(freq, arr);
// 	if (s != SysInitStatus::InitOK) { DeInit(); return s; }
// 	_update_cb = cb;
// 	TIMx->SR = 0;
// 	IRQ_en(IRQ::UE, ENABLE);
// 	return SysInitStatus::InitOK;
// }
//
// SysInitStatus TIM_PeriodicIRQ::SetCompareIRQ(Channel ch, uint32_t compare, void (*cb)(void))
// {
// 	uint32_t ch_idx = static_cast<uint32_t>(ch);
// 	if (!_info || ch_idx >= _info->channel_count || (TIMx->CR1 & TIM_CR1_CEN)
// 		|| (_channels_used & (1u << ch_idx)) || compare > TIMx->ARR)
// 		return SysInitStatus::InitError;
//
// 	*ccr(ch_idx) = compare;
// 	TIMx->SR = ~(TIM_SR_CC1IF << ch_idx);
// 	_cc_cb[ch_idx] = cb;
// 	_channels_used |= static_cast<uint8_t>(1u << ch_idx);
// 	// CC1IE..CC4IE are contiguous DIER bits — same shift trick used elsewhere in this file.
// 	IRQ_en(static_cast<IRQ>(TIM_DIER_CC1IE << ch_idx), ENABLE);
// 	return SysInitStatus::InitOK;
// }
//
// // ---------------------------------------------------------------------------
// // TIM_PWM
// // ---------------------------------------------------------------------------
//
// void TIM_PWM::SetDuty(Channel ch, uint32_t percent)
// {
// 	if (percent > 100) percent = 100;
// 	// (ARR+1) is the PWM period in ticks; scale by percent instead of assuming
// 	// the default arr=999 from SetUp() (which only holds for 0.1% resolution).
// 	// SetDuty targets percentage-scale periods; for ARR near the 32-bit limit
// 	// (TIM2/TIM5 without a prescaler) use SetCCR() instead.
// 	*ccr(static_cast<uint32_t>(ch)) = (TIMx->ARR + 1) * percent / 100;
// }
//
// void TIM_PWM::SetCCR(Channel ch, uint32_t val)
// {
// 	*ccr(static_cast<uint32_t>(ch)) = val;
// }
//
// SysInitStatus TIM_PWM::SetUp(uint32_t freq, uint32_t arr)
// {
// 	bool any = false;
// 	for (int i = 0; i < 4; i++) if (_ch[i].IsValid()) { any = true; break; }
// 	if (!any) return SysInitStatus::InitError;
//
// 	// Init() must run before SetFrequency() — it needs _info (bus clock,
// 	// arr_max), which Init() is what actually populates.
// 	SysInitStatus s = Init();
// 	if (s != SysInitStatus::InitOK) return s;
//
// 	s = SetFrequency(freq, arr);
// 	if (s != SysInitStatus::InitOK) { DeInit(); return s; }
//
// 	TIMx->CCMR1 = 0;
// 	TIMx->CCMR2 = 0;
// 	TIMx->CCER  = 0;
// 	TIMx->CCR1  = 0;
// 	TIMx->CCR2  = 0;
// 	TIMx->CCR3  = 0;
// 	TIMx->CCR4  = 0;
//
// 	for (uint32_t i = 0; i < 4; i++) {
// 		if (!_ch[i].IsValid()) continue;
// 		_ch[i].ToPin().SetUp(PIN::TYPE::AF_PushPull, PIN::OUTPUT_SPEED::High);
// 		// OC1M=6 (PWM mode 1) | OC1PE=1 (CCR preload enable).
// 		// Preload is required for DMA-driven output: without it the DMA write
// 		// arrives ~3 cycles after the UPDATE event (AHB latency) and overwrites
// 		// CCR mid-period, truncating the leading edge of the first bit whenever
// 		// the CCR value changes.  With preload, DMA writes go to the shadow
// 		// register and are applied atomically at the next UPDATE.
// 		SetCCMRField(TIM_CCMR1_OC1M_Pos, i, 6u);
// 		SetCCMRField(TIM_CCMR1_OC1PE_Pos, i, 1u);
// 		TIMx->CCER |= TIM_CCER_CC1E << (i * 4u);
// 	}
//
// 	if (_info->has_bdtr) TIMx->BDTR = TIM_BDTR_MOE;
// 	Start();
// 	return SysInitStatus::InitOK;
// }
//
// void TIM_PWM::AttachDMA(DMA_Sx* dma, Channel ch)
// {
// 	if (!_info || !dma) return;
// 	int idx = static_cast<int>(ch);
// 	_dma[idx] = dma;
//
// 	DMA_Sx::StreamSettings cfg;
// 	cfg.channel     = _info->dma_up_req;
// 	cfg.direction   = DMA_Sx::DIR::To_Per;
// 	cfg.data_size   = DMA_Sx::SIZE::Half_Word;
// 	cfg.minc        = true;
// 	cfg.per_address = reinterpret_cast<uint32_t>(ccr(static_cast<uint32_t>(ch)));
// 	dma->SetUp(cfg);
//
// 	IRQn_Type irqn = dma->GetIRQn();
// 	if (irqn != static_cast<IRQn_Type>(-1)) {
// 		IRQ_Registry::Register(irqn, this);
// 		NVIC_EnableIRQ(irqn);
// 		dma->Enable_IRQ(DMA_Sx::IRQ::TC);
// 	}
// }
//
// SysStatus TIM_PWM::SendDMA(Channel ch, const uint16_t* data, uint32_t len)
// {
// 	int idx = static_cast<int>(ch);
// 	if (!_dma[idx] || !data || !len) return SysStatus::Error;
// 	if (_dma_busy & (1u << idx))     return SysStatus::Busy;
//
// 	_dma_busy |= static_cast<uint8_t>(1u << idx);
// 	_dma[idx]->ClearFlags();
// 	_dma[idx]->SetMemAddr(reinterpret_cast<uint32_t>(data));
// 	_dma[idx]->SetCount(len);
// 	// Enable the timer→DMA request on the first channel to start.
// 	// Subsequent channels reuse the already-set UDE bit.
// 	if ((_dma_busy & ~static_cast<uint8_t>(1u << idx)) == 0)
// 		TIMx->DIER |= TIM_DIER_UDE;
// 	_dma[idx]->Stream_EN(ENABLE);
// 	return SysStatus::OK;
// }
//
// void TIM_PWM::HandleIRQ()
// {
// 	// Check every attached DMA channel — any of them may have triggered this IRQ.
// 	bool any_done = false;
// 	for (int i = 0; i < 4; ++i) {
// 		if (_dma[i] && (_dma_busy & (1u << i)) && _dma[i]->GetTC_Flag()) {
// 			_dma[i]->Stream_EN(DISABLE);
// 			_dma[i]->ClearFlags();
// 			_dma_busy &= ~static_cast<uint8_t>(1u << i);
// 			any_done = true;
// 		}
// 	}
// 	// Disable the timer→DMA trigger and invoke the callback once all channels finish.
// 	if (any_done && _dma_busy == 0) {
// 		TIMx->DIER &= ~TIM_DIER_UDE;
// 		if (_dma_cb) _dma_cb();
// 	}
// 	// Handle timer update / CC interrupts (UIF, CCxIF) if any are enabled.
// 	TIM::HandleIRQ();
// }
//
// // ---------------------------------------------------------------------------
// // TIM_EncoderGenerator
// // ---------------------------------------------------------------------------
//
// SysInitStatus TIM_EncoderGenerator::SetUp(uint32_t freq, uint32_t period,
//                                            uint32_t ch1_width, uint32_t ch2_width)
// {
// 	if (!freq || !_a.pin.IsValid() || !_b.pin.IsValid())
// 		return SysInitStatus::InitError;
//
// 	SysInitStatus s = Init();
// 	if (s != SysInitStatus::InitOK) return s;
//
// 	_a.pin.ToPin().SetUp(PIN::TYPE::AF_PushPull);
// 	_b.pin.ToPin().SetUp(PIN::TYPE::AF_PushPull);
//
// 	uint32_t ai = static_cast<uint32_t>(_a.GetChannel());
// 	uint32_t bi = static_cast<uint32_t>(_b.GetChannel());
//
// 	TIMx->ARR = period;
// 	TIMx->PSC = *_info->bus_clk / ((period + 1u) * freq * 2u) - 1u;
//
// 	*ccr(ai) = ch1_width;
// 	*ccr(bi) = ch2_width;
//
// 	TIMx->CCMR1 = 0;
// 	TIMx->CCMR2 = 0;
//
// 	// Toggle mode = 0b011 on both channels
// 	SetCCMRField(TIM_CCMR1_OC1M_Pos, ai, 3u);
// 	SetCCMRField(TIM_CCMR1_OC1M_Pos, bi, 3u);
//
// 	TIMx->CCER = (TIM_CCER_CC1E << (ai * 4u)) | (TIM_CCER_CC1E << (bi * 4u));
// 	if (_info->has_bdtr) TIMx->BDTR = TIM_BDTR_MOE;
//
// 	IRQ_en(IRQ::UE, ENABLE);
// 	return SysInitStatus::InitOK;
// }
//
// void TIM_EncoderGenerator::GenPulse(int32_t pulses)
// {
// 	if (pulses > 0)
// 		TIMx->CCER &= ~TIM_CCER_CC1P;
// 	else {
// 		TIMx->CCER |= TIM_CCER_CC1P;
// 		pulses = -pulses;
// 	}
// 	_pulses = pulses;
// 	Start();
// }
//
// void TIM_EncoderGenerator::HandleIRQ()
// {
// 	TIMx->SR = 0;
// 	int32_t p = _pulses - 1;
// 	_pulses = p;
// 	if (p <= 0) {
// 		Stop();
// 		if (_on_complete) _on_complete();
// 	}
// }
//
// // ---------------------------------------------------------------------------
// // TIM_StepGenerator
// // ---------------------------------------------------------------------------
//
// SysInitStatus TIM_StepGenerator::SetUp(bool count_in_isr)
// {
// 	if (!_step.pin.IsValid()) return SysInitStatus::InitError;
//
// 	SysInitStatus s = Init();
// 	if (s != SysInitStatus::InitOK) return s;
//
// 	_step.pin.ToPin().SetUp(PIN::TYPE::AF_PushPull);
//
// 	uint32_t idx = static_cast<uint32_t>(_step.GetChannel());
// 	TIMx->CCMR1 = 0;
// 	TIMx->CCMR2 = 0;
// 	TIMx->CCER  = 0;
//
// 	// PWM mode 1 (OC1M=6): HIGH while CNT < CCR, LOW otherwise. CCR is kept
// 	// at ARR/2 (see SetStepFrequency()) for an exact 50% duty pulse once per
// 	// period — one Update event per step, not two like toggle mode would need.
// 	// OC1PE (CCR preload) pairs with ARPE below so a live SetStepFrequency()
// 	// call's new ARR and CCR both take effect together at the next Update
// 	// event, instead of CCR jumping immediately while ARR is still buffered.
// 	SetCCMRField(TIM_CCMR1_OC1M_Pos, idx, 6u);
// 	SetCCMRField(TIM_CCMR1_OC1PE_Pos, idx, 1u);
// 	TIMx->CCER |= TIM_CCER_CC1E << (idx * 4u);
// 	TIMx->CR1 |= TIM_CR1_ARPE;
// 	TIMx->ARR = 0;
// 	TIMx->PSC = 0;
//
// 	if (_info->has_bdtr) TIMx->BDTR = TIM_BDTR_MOE;
//
// 	// Only pay for the Update IRQ when software step counting was asked for;
// 	// with a TIM_HWCounter attached instead, no IRQ is needed at all.
// 	if (count_in_isr) IRQ_en(IRQ::UE, ENABLE);
// 	return SysInitStatus::InitOK;//SetStepFrequency(freq);
// }
//
// SysInitStatus TIM_StepGenerator::SetStepFrequency(uint32_t freq)
// {
// 	TIMx->CR1 &= ~TIM_CR1_OPM;  // continuous mode: undo any earlier RunSteps() one-shot
// 	SysInitStatus s = SetFrequency(freq);
// 	if (s != SysInitStatus::InitOK) return s;
// 	*ccr(static_cast<uint32_t>(_step.GetChannel())) = TIMx->ARR >> 1;  // keep the 50% duty in sync with the new ARR
// 	return SysInitStatus::InitOK;
// }
//
// SysInitStatus TIM_StepGenerator::RunSteps(uint32_t steps)
// {
// 	if (!_info || !steps) return SysInitStatus::InitError;
// 	if (!_info->has_bdtr) return SysInitStatus::InitError;  // no RCR without BDTR
//
// 	// One Update event (period) per output step (PWM mode).
// 	uint32_t events = steps - 1u;
// 	if (events > TIM_RCR_REP_Msk) return SysInitStatus::InitError;  // exceeds this timer's RCR width
//
// 	Stop();
// 	TIMx->CNT  = 0;
// 	TIMx->RCR  = events;
// 	TIMx->CR1 |= TIM_CR1_OPM;  // auto-clear CEN once RCR reaches 0
// 	TIMx->EGR  = TIM_EGR_UG;   // force the RCR preload into the active counter before the first period
// 	Start();
// 	return SysInitStatus::InitOK;
// }
//
// void TIM_StepGenerator::HandleIRQ()
// {
// 	TIMx->SR = 0;
// 	_step_count = _step_count + 1;
// }
//
// // ---------------------------------------------------------------------------
// // TIM_InputCapture
// // ---------------------------------------------------------------------------
//
// SysInitStatus TIM_InputCapture::SetUp(uint32_t max_freq, void (*cb)(void))
// {
// 	if (!_input.pin.IsValid() || !max_freq) return SysInitStatus::InitError;
//
// 	SysInitStatus s = Init();
// 	if (s != SysInitStatus::InitOK) return s;
//
// 	_input.pin.ToPin().SetUp(PIN::TYPE::AF_PushPull);
//
// 	TIMx->PSC = *_info->bus_clk / max_freq - 1;
// 	TIMx->ARR = 0xFFFF;
//
// 	uint32_t ch_idx = static_cast<uint32_t>(_input.GetChannel());
// 	SetCCMRField(TIM_CCMR1_CC1S_Pos, ch_idx, 0b01u);  // map CCx to this channel's own TI input
// 	TIMx->CCER |= TIM_CCER_CC1E << (ch_idx * 4u);
//
// 	_cc_cb[ch_idx] = cb;
// 	// CC1IE..CC4IE are contiguous DIER bits — same shift trick HandleIRQ() uses for the SR flags.
// 	IRQ_en(static_cast<IRQ>(TIM_DIER_CC1IE << ch_idx), ENABLE);
//
// 	Start();
// 	return SysInitStatus::InitOK;
// }
//
// void TIM_InputCapture::HandleIRQ()
// {
// 	uint32_t sr = TIMx->SR;
// 	TIMx->SR = ~sr;  // see TIM::HandleIRQ() for why not a blanket `= 0`
// 	uint32_t ch_idx = static_cast<uint32_t>(_input.GetChannel());
// 	if (sr & (TIM_SR_CC1IF << ch_idx)) {
// 		_capture = *ccr(ch_idx);
// 		if (_cc_cb[ch_idx]) _cc_cb[ch_idx]();
// 	}
// }
//
// // ---------------------------------------------------------------------------
// // TIM_PulseMeasure
// // ---------------------------------------------------------------------------
//
// SysInitStatus TIM_PulseMeasure::SetUp(uint32_t max_freq)
// {
// 	uint32_t ch_idx = static_cast<uint32_t>(_input.GetChannel());
// 	if (!_input.pin.IsValid() || ch_idx > 1 || !max_freq)
// 		return SysInitStatus::InitError;
//
// 	SysInitStatus s = Init();
// 	if (s != SysInitStatus::InitOK) return s;
//
// 	_input.pin.ToPin().SetUp(PIN::TYPE::AF_PushPull);
//
// 	TIMx->PSC = *_info->bus_clk / max_freq - 1;
// 	TIMx->ARR = 0xFFFF;
//
// 	// Direct mapping on the selected channel, indirect on the complementary one
// 	TIMx->CCMR1 = (0b01u << (ch_idx        * TIM_CCMR1_CC2S_Pos)) |
// 	              (0b10u << ((!ch_idx & 1u) * TIM_CCMR1_CC2S_Pos));
// 	TIMx->SMCR  = ((5u + ch_idx) << TIM_SMCR_TS_Pos) |
// 	              (0b100u         << TIM_SMCR_SMS_Pos);  // reset mode
// 	TIMx->CCER  = TIM_CCER_CC1P | TIM_CCER_CC1E | TIM_CCER_CC2E;
//
// 	Start();
// 	return SysInitStatus::InitOK;
// }
//
// // ---------------------------------------------------------------------------
// // TIM_EncoderReader
// // ---------------------------------------------------------------------------
//
// SysInitStatus TIM_EncoderReader::SetUp(Mode mode)
// {
// 	if (!_a.pin.IsValid() || !_b.pin.IsValid()) return SysInitStatus::InitError;
//
// 	SysInitStatus s = Init();
// 	if (s != SysInitStatus::InitOK) return s;
//
// 	_a.pin.ToPin().SetUp(PIN::TYPE::AF_PushPull);
// 	_b.pin.ToPin().SetUp(PIN::TYPE::AF_PushPull);
//
// 	// CC1S=01/CC2S=01: map CH1/CH2 to their own TI input, same encoding
// 	// TIM_InputCapture uses. Encoder Interface mode reads its edges off this
// 	// mapping internally — CCER (capture/output enable) is not needed since
// 	// nothing is captured into CCR1/CCR2 or driven onto a pin here, only
// 	// counted.
// 	TIMx->CCMR1 = (0b01u << TIM_CCMR1_CC1S_Pos) | (0b01u << TIM_CCMR1_CC2S_Pos);
// 	TIMx->CCER  = 0;
// 	TIMx->CNT   = 0;
// 	TIMx->ARR   = _info->arr_max;  // maximise range before wraparound
// 	TIMx->SMCR  = static_cast<uint32_t>(mode) << TIM_SMCR_SMS_Pos;
//
// 	Start();
// 	return SysInitStatus::InitOK;
// }
//
// // ---------------------------------------------------------------------------
// // TIM_HWCounter
// // ---------------------------------------------------------------------------
//
// SysInitStatus TIM_HWCounter::SetUp(TIM& master, void (*on_complete)(void))
// {
// 	SysInitStatus s = Init();
// 	if (s != SysInitStatus::InitOK) return s;
//
// 	uint8_t itr = FindITR(master.GetTIMx(), TIMx);
// 	if (itr == 0xFF) { DeInit(); return SysInitStatus::InitError; }
//
// 	master.EnableTriggerOutput();  // MMS = 010: TRGO on Update event
// 	_master      = &master;
// 	_on_complete = on_complete;
//
// 	TIMx->CNT  = 0;
// 	TIMx->SMCR = (itr << TIM_SMCR_TS_Pos) | (0b111u << TIM_SMCR_SMS_Pos);  // External clock mode 1
// 	TIMx->ARR  = _info->arr_max;  // maximise range before wraparound
// 	// Enabled unconditionally, not just when on_complete is non-null: the
// 	// master auto-stop from StopAfter()/StopAT() is useful on its own, same
// 	// as TIM_PeriodicIRQ::SetCompareIRQ()'s "nullptr = no callback, just the
// 	// auto-stop" convention.
// 	IRQ_en(IRQ::CC1, ENABLE);
// 	Start();
// 	return SysInitStatus::InitOK;
// }
//
// SysInitStatus TIM_HWCounter::StopAfter(uint32_t ticks)
// {
// 	if (!_info || !ticks) return SysInitStatus::InitError;
// 	if (ticks > _info->arr_max) return SysInitStatus::InitError;  // move too long for this counter's width
//
// 	// 64-bit: arr_max can be 0xFFFFFFFF (32-bit-ARR timers), where period
// 	// itself overflows a uint32_t; the modulo below needs the real period.
// 	uint64_t period = static_cast<uint64_t>(_info->arr_max) + 1;
// 	bool     down   = IsCountingDown();
// 	uint32_t target = down
// 		? static_cast<uint32_t>((TIMx->CNT + period - ticks) % period)
// 		: static_cast<uint32_t>((TIMx->CNT + ticks) % period);
//
// 	*ccr(0) = target;  // CH1, used purely as a pin-free comparator
// 	IRQ_en(IRQ::CC1, ENABLE);  // re-arm in case CancelStopAfter() switched it off
// 	return SysInitStatus::InitOK;
// }
//
// SysInitStatus TIM_HWCounter::StopAT(uint32_t ticks)
// {
// 	if (!_info || !ticks) return SysInitStatus::InitError;
// 	if (ticks > _info->arr_max) return SysInitStatus::InitError;  // doesn't fit this counter's width
//
// 	*ccr(0) = ticks;  // CH1, used purely as a pin-free comparator
// 	IRQ_en(IRQ::CC1, ENABLE);  // re-arm in case CancelStopAfter() switched it off
// 	return SysInitStatus::InitOK;
// }
//
// void TIM_HWCounter::HandleIRQ()
// {
// 	uint32_t sr = TIMx->SR;
// 	TIMx->SR = ~sr;  // see TIM::HandleIRQ() for why not a blanket `= 0`
// 	if (sr & TIM_SR_CC1IF) {
// 		if (_master) _master->Stop();
// 		if (_on_complete) _on_complete();
// 	}
// }
