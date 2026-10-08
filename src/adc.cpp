#include "adc.hpp"

// ---------------------------------------------------------------------------
// Peripheral table — defined in adc_defs.hpp Section B
// ---------------------------------------------------------------------------

#define ADC_DEFS_CPP
#include "adc_defs.hpp"
#undef  ADC_DEFS_CPP

// ---------------------------------------------------------------------------
// ADC_N::AddChannel
// ---------------------------------------------------------------------------

#if defined(STM32G0)
void ADC_N::AddChannel(ADC_PIN pin, SMP_SEL sel)
#elif defined(STM32F4) || defined(STM32F7)
void ADC_N::AddChannel(ADC_PIN pin, SMPL smpl)
#endif
{
#if defined(STM32G0)
	const uint8_t smp = static_cast<uint8_t>(sel);
#elif defined(STM32F4) || defined(STM32F7)
	const uint8_t smp = static_cast<uint8_t>(smpl);
#endif
	if (!pin.IsValid()) return;
	if (_ch_count >= ADC_N_MAX_CHANNELS)
		System::DebugTrap("ADC_N: sequence longer than ADC_N_MAX_CHANNELS");
	if (pin.adc_base && pin.adc_base != reinterpret_cast<uint32_t>(ADCx))
		System::DebugTrap("ADC_N: pin belongs to wrong ADC peripheral");

	// The sampling setting belongs to the channel number (SMPSELx / SMPRx),
	// not to the rank
	for (uint8_t i = 0; i < _ch_count; ++i)
		if (_ch[i].channel == pin.channel && _ch[i].smp != smp)
			System::DebugTrap("ADC_N: same channel added with a different sampling setting");

	_ch[_ch_count++] = { pin.port, pin.pin, pin.channel, smp, pin.internal };
}

// ---------------------------------------------------------------------------
// ADC_N::SetUp
// ---------------------------------------------------------------------------

#if defined(STM32G0)
SysInitStatus ADC_N::SetUp(SMPL smp1, SMPL smp2)
#elif defined(STM32F4) || defined(STM32F7)
SysInitStatus ADC_N::SetUp()
#endif
{
	if (_ch_count == 0) return SysInitStatus::InitError;

	// Internal inputs in the sequence -> enable bits in ADC->CCR
	bool use_vref = false, use_temp = false, use_vbat = false;
	uint8_t temp_ch = 0, vbat_ch = 0;
	for (uint8_t i = 0; i < _ch_count; ++i) {
		switch (_ch[i].internal) {
			case ADC_INTERNAL::VREFINT: use_vref = true;                            break;
			case ADC_INTERNAL::TEMP:    use_temp = true; temp_ch = _ch[i].channel;  break;
			case ADC_INTERNAL::VBAT:    use_vbat = true; vbat_ch = _ch[i].channel;  break;
			default:                                                                break;
		}
	}
	// F4/F7 with the sensor on IN18: VBATE takes over the channel
	if (use_temp && use_vbat && temp_ch == vbat_ch) return SysInitStatus::InitError;

	// Table lookup + clock enable
	for (const auto& e : adc_table)
		if (e.periph == ADCx) { _info = &e; break; }
	if (!_info) return SysInitStatus::InitError;
	*_info->clk_reg |= _info->clk_bit;

#if defined(STM32G0)

	// Sequencer mode. Fully configurable (CHSELRMOD = 1) converts in
	// AddChannel() order, like SQRx on F4/F7, but has 8 ranks with 4-bit
	// fields (0xF = end of sequence): at most 8 channels, all in 0-14.
	// Otherwise the bitmask mode is used, which always converts in ascending
	// channel order — reject a different order instead of silently
	// reordering the buffer.
	bool seq_mode  = _ch_count <= 8;
	bool ascending = true;
	for (uint8_t i = 0; i < _ch_count; ++i) {
		if (_ch[i].channel > 14) seq_mode = false;
		if (i > 0 && _ch[i].channel <= _ch[i - 1].channel) ascending = false;
	}
	if (!seq_mode && !ascending) return SysInitStatus::InitError;

	// 0. Re-SetUp: stop and disable. ADEN is cleared only through ADDIS,
	//    and calibration requires ADEN = 0.
	Stop();
	if (ADCx->CR & ADC_CR_ADEN) {
		ADCx->CR |= ADC_CR_ADDIS;
		while (ADCx->CR & ADC_CR_ADEN) {}
	}
	ADCx->CFGR1 = 0;   // DMAEN must be 0 during calibration
	ADC->CCR = (ADC->CCR & ~(ADC_CCR_VREFEN | ADC_CCR_TSEN | ADC_CCR_VBATEN))
	         | (use_vref ? ADC_CCR_VREFEN : 0u)
	         | (use_temp ? ADC_CCR_TSEN   : 0u)
	         | (use_vbat ? ADC_CCR_VBATEN : 0u);   // internal inputs, start-up covered below

	// 1. Clock: synchronous PCLK/2. The asynchronous default (CKMODE = 00)
	//    runs from SYSCLK, which exceeds f_ADC max (35 MHz) at 64 MHz.
	ADCx->CFGR2 = ADC_CFGR2_CKMODE_0;

	// 2. Enable voltage regulator (must be done before calibration)
	ADCx->CR |= ADC_CR_ADVREGEN;
	System::Delay_ms(1);   // t_ADCVREG_STUP ≤ 20 µs; also covers the
	                       // internal inputs' start-up enabled below

	// 3. Calibration
	ADCx->CR |= ADC_CR_ADCAL;
	while (ADCx->CR & ADC_CR_ADCAL) {}

	// 4. Sampling time: SMP1/SMP2 from the arguments, SMPSELx per channel
	uint32_t smpr = (static_cast<uint32_t>(smp1) << ADC_SMPR_SMP1_Pos)
	              | (static_cast<uint32_t>(smp2) << ADC_SMPR_SMP2_Pos);
	for (uint8_t i = 0; i < _ch_count; ++i)
		if (_ch[i].smp == static_cast<uint8_t>(SMP_SEL::SMP2))
			smpr |= 1u << (ADC_SMPR_SMPSEL0_Pos + _ch[i].channel);
	ADCx->SMPR = smpr;

	// 5. CFGR1 — sequencer mode (may only change while ADEN = 0); software
	//    trigger, DMA off: StartDMA()/StartTrig() set what they need.
	ADCx->CFGR1 = seq_mode ? ADC_CFGR1_CHSELRMOD : 0u;

	// 6. Enable ADC. ADEN is ignored for a few ADC clocks after ADCAL
	//    clears, so keep setting it until ADRDY appears.
	ADCx->ISR = ADC_ISR_ADRDY;
	while (!(ADCx->ISR & ADC_ISR_ADRDY)) {
		if (!(ADCx->CR & ADC_CR_ADEN))
			ADCx->CR |= ADC_CR_ADEN;
	}

	// 7. Channel selection (CHSELR) — applied with the ADC clock running
	uint32_t chsel;
	if (seq_mode) {
		// SQ1..SQn = channels in call order; the unused SQ fields stay 0xF,
		// so SQ(n+1) terminates the sequence
		chsel = 0xFFFFFFFFu;
		for (uint8_t i = 0; i < _ch_count; ++i) {
			chsel &= ~(0xFu << (i * 4u));
			chsel |= static_cast<uint32_t>(_ch[i].channel) << (i * 4u);
		}
	} else {
		chsel = 0;
		for (uint8_t i = 0; i < _ch_count; ++i)
			chsel |= 1u << _ch[i].channel;
	}
	ADCx->ISR    = ADC_ISR_CCRDY;
	ADCx->CHSELR = chsel;
	while (!(ADCx->ISR & ADC_ISR_CCRDY)) {}
	ADCx->ISR    = ADC_ISR_CCRDY;   // clear by writing 1

#elif defined(STM32F4) || defined(STM32F7)

	// 0. Re-SetUp: stop, then power down during configuration
	Stop();
	ADCx->CR2 = 0;

	// 1. Clock: PCLK2/4 (ADCPRE = 01). The default /2 gives 42 MHz at
	//    PCLK2 = 84 MHz, above f_ADC max (36 MHz). ADC->CCR is common to
	//    all ADCs, so it is only touched when it differs.
	if ((ADC->CCR & ADC_CCR_ADCPRE) != ADC_CCR_ADCPRE_0)
		ADC->CCR = (ADC->CCR & ~ADC_CCR_ADCPRE) | ADC_CCR_ADCPRE_0;

	//    Internal inputs exist on ADC1 only, which owns TSVREFE/VBATE.
	if (ADCx == ADC1)
		ADC->CCR = (ADC->CCR & ~(ADC_CCR_TSVREFE | ADC_CCR_VBATE))
		         | ((use_temp || use_vref) ? ADC_CCR_TSVREFE : 0u)
		         | (use_vbat ? ADC_CCR_VBATE : 0u);

	// 2. Sampling time per channel: SMPR2 covers CH0-CH9 (3 bits each),
	//    SMPR1 covers CH10-CH18
	uint32_t smpr2 = 0, smpr1 = 0;
	for (uint8_t i = 0; i < _ch_count; ++i) {
		const uint32_t v  = _ch[i].smp;
		const uint8_t  ch = _ch[i].channel;
		if (ch < 10)
			smpr2 |= v << (ch * 3u);
		else
			smpr1 |= v << ((ch - 10u) * 3u);
	}
	ADCx->SMPR1 = smpr1;
	ADCx->SMPR2 = smpr2;

	// 3. Conversion sequence (SQR registers), built in full so a re-SetUp
	//    does not OR into the old sequence
	uint32_t sqr1 = (_ch_count - 1u) << ADC_SQR1_L_Pos;   // sequence length
	uint32_t sqr2 = 0, sqr3 = 0;
	for (uint8_t i = 0; i < _ch_count; ++i) {
		uint32_t ch = _ch[i].channel;
		if (i < 6)
			sqr3 |= ch << (i * 5u);
		else if (i < 12)
			sqr2 |= ch << ((i - 6u) * 5u);
		else
			sqr1 |= ch << ((i - 12u) * 5u);
	}
	ADCx->SQR1 = sqr1;
	ADCx->SQR2 = sqr2;
	ADCx->SQR3 = sqr3;

	// 4. CR1 — SCAN mode if more than one channel; keep interrupt enables
	ADCx->CR1 = (ADCx->CR1 & (ADC_CR1_EOCIE | ADC_CR1_OVRIE))
	          | ((_ch_count > 1) ? ADC_CR1_SCAN : 0u);

	// 5. CR2 — power on; software trigger, DMA off: StartDMA()/StartTrig()
	//    set what they need.
	ADCx->CR2 = ADC_CR2_ADON;
	System::Delay_ms(1);   // t_STAB ≤ 3 µs; also covers the internal inputs' start-up

#endif

	// Configure GPIO pins as ANALOG (internal inputs have none)
	for (uint8_t i = 0; i < _ch_count; ++i)
		if (_ch[i].port)
			PIN(reinterpret_cast<GPIO_TypeDef*>(_ch[i].port), _ch[i].pin)
			    .SetUp(PIN::TYPE::ANALOG);

	return SysInitStatus::InitOK;
}

// ---------------------------------------------------------------------------
// ADC_N::AttachDMA
// ---------------------------------------------------------------------------

SysInitStatus ADC_N::AttachDMA(DMA_Sx* dma)
{
	if (!_info || !dma) return SysInitStatus::InitError;

	DMA_Sx::StreamSettings cfg;
	cfg.channel     = _info->dma_req;
	cfg.direction   = DMA_Sx::DIR::From_Per;
	cfg.data_size   = DMA_Sx::SIZE::Half_Word;
	cfg.minc        = true;   // CIRC is set by StartDMA() / StartTrig()
	cfg.per_address = reinterpret_cast<uint32_t>(&ADCx->DR);
	SysInitStatus s = dma->SetUp(cfg);
	if (s != SysInitStatus::InitOK) return s;

	_dma = dma;   // no interrupt: IRQ_en(IRQ::DMA_TC, ENABLE) if wanted
	return SysInitStatus::InitOK;
}

// ---------------------------------------------------------------------------
// ADC_N::AttachTrig / AttachExtTrig
// ---------------------------------------------------------------------------

SysInitStatus ADC_N::AttachTrig(TIM_TriggerGenerator* trg)
{
	if (!trg || IsBusy()) return SysInitStatus::InitError;
	if (trg->GetReq().target != TIM_TriggerGenerator::TrigTarget::Adc)
		return SysInitStatus::InitError;

	_trg      = trg;
	_extsel   = trg->GetReq().extsel;
	_trig_set = true;
	return SysInitStatus::InitOK;
}

SysInitStatus ADC_N::AttachExtTrig()
{
	if (IsBusy()) return SysInitStatus::InitError;

	_trg      = nullptr;
	_extsel   = EXTSEL_EXTI11;
	_trig_set = true;
	return SysInitStatus::InitOK;
}

// ---------------------------------------------------------------------------
// ADC_N::Start / StartDMA / StartTrig / Stop
// ---------------------------------------------------------------------------

void ADC_N::Start()
{
	if (!_info || IsBusy()) return;
#if defined(STM32G0)
	ADCx->CR  |= ADC_CR_ADSTART;
#elif defined(STM32F4) || defined(STM32F7)
	ADCx->CR2 |= ADC_CR2_SWSTART;
#endif
}

SysStatus ADC_N::StartDMA(uint16_t* buf)
{
	if (!_dma || !buf || !_ch_count) return SysStatus::Error;
	if (IsBusy())                    return SysStatus::Busy;

	_mode = Mode::Dma;

	// One sequence, one DMA pass: normal DMA, one-shot ADC DMA requests
	// (DMACFG / DDS = 0: no request after the last transfer).
	_dma->ClearFlags();
	_dma->CIRC(DISABLE);
	_dma->SetMemAddr(reinterpret_cast<uint32_t>(buf));
	_dma->SetCount(_ch_count);
#if defined(STM32G0)
	ADCx->CFGR1 |= ADC_CFGR1_DMAEN;
	_dma->Stream_EN(ENABLE);
	ADCx->CR    |= ADC_CR_ADSTART;
#elif defined(STM32F4) || defined(STM32F7)
	ADCx->CR2   |= ADC_CR2_DMA;
	_dma->Stream_EN(ENABLE);
	ADCx->CR2   |= ADC_CR2_SWSTART;
#endif
	return SysStatus::OK;
}

SysStatus ADC_N::StartTrig(uint16_t* buf, uint32_t len)
{
	if (!_dma || !_trig_set || !buf || !_ch_count)  return SysStatus::Error;
	if (!len || len % _ch_count || len > 0xFFFFu)    return SysStatus::Error;
	if (IsBusy())                                     return SysStatus::Busy;

	_mode = Mode::Trig;

	// Circular DMA over buf, circular ADC DMA requests (DMACFG / DDS = 1),
	// trigger source selected, rising edge.
	_dma->ClearFlags();
	_dma->CIRC(ENABLE);
	_dma->SetMemAddr(reinterpret_cast<uint32_t>(buf));
	_dma->SetCount(len);
#if defined(STM32G0)
	ADCx->CFGR1 = (ADCx->CFGR1 & ~(ADC_CFGR1_EXTSEL | ADC_CFGR1_EXTEN))
	            | ADC_CFGR1_DMAEN | ADC_CFGR1_DMACFG
	            | ADC_CFGR1_EXTEN_0 | (static_cast<uint32_t>(_extsel) << ADC_CFGR1_EXTSEL_Pos);
	_dma->Stream_EN(ENABLE);
	ADCx->CR   |= ADC_CR_ADSTART;    // arms: conversions start on the trigger
#elif defined(STM32F4) || defined(STM32F7)
	ADCx->CR2   = (ADCx->CR2 & ~(ADC_CR2_EXTSEL | ADC_CR2_EXTEN))
	            | ADC_CR2_DMA | ADC_CR2_DDS
	            | (static_cast<uint32_t>(_extsel) << ADC_CR2_EXTSEL_Pos);
	_dma->Stream_EN(ENABLE);
	ADCx->CR2  |= ADC_CR2_EXTEN_0;   // arms: rising edge
#endif

	// Trigger timer last, so its first event finds the ADC armed
	if (_trg) _trg->Start();
	return SysStatus::OK;
}

SysStatus ADC_N::StartCont(uint16_t* buf, uint32_t len)
{
	if (!_dma || !buf || !_ch_count)              return SysStatus::Error;
	if (!len || len % _ch_count || len > 0xFFFFu) return SysStatus::Error;
	if (IsBusy())                                 return SysStatus::Busy;

	_mode = Mode::ContDma;

	// Circular DMA over buf, circular ADC DMA requests, continuous mode,
	// software start.
	_dma->ClearFlags();
	_dma->CIRC(ENABLE);
	_dma->SetMemAddr(reinterpret_cast<uint32_t>(buf));
	_dma->SetCount(len);
#if defined(STM32G0)
	ADCx->CFGR1 |= ADC_CFGR1_DMAEN | ADC_CFGR1_DMACFG | ADC_CFGR1_CONT;
	_dma->Stream_EN(ENABLE);
	ADCx->CR    |= ADC_CR_ADSTART;
#elif defined(STM32F4) || defined(STM32F7)
	ADCx->CR2   |= ADC_CR2_DMA | ADC_CR2_DDS | ADC_CR2_CONT;
	_dma->Stream_EN(ENABLE);
	ADCx->CR2   |= ADC_CR2_SWSTART;
#endif
	return SysStatus::OK;
}

SysStatus ADC_N::StartCont()
{
	if (!_info || _ch_count != 1) return SysStatus::Error;
	if (IsBusy())                 return SysStatus::Busy;

	_mode = Mode::Cont;

	// Continuous mode, no DMA; unread results are overwritten
#if defined(STM32G0)
	ADCx->CFGR1 |= ADC_CFGR1_CONT | ADC_CFGR1_OVRMOD;
	ADCx->CR    |= ADC_CR_ADSTART;
#elif defined(STM32F4) || defined(STM32F7)
	ADCx->CR2   |= ADC_CR2_CONT;   // EOCS = 0, DMA = 0: no overrun detection
	ADCx->CR2   |= ADC_CR2_SWSTART;
#endif
	return SysStatus::OK;
}

bool ADC_N::IsBusy()
{
	// StartDMA() without IRQ::DMA_TC: the hardware has stopped by itself
	// once the DMA transfer is complete — finish the run here
	if (_mode == Mode::Dma && _dma && _dma->GetTC_Flag()) Stop();
	return _mode != Mode::Idle;
}

void ADC_N::Stop()
{
	if (_trg) _trg->Stop();
	if (!_info) return;

#if defined(STM32G0)
	// ADSTP may only be set while ADSTART = 1; CFGR1 is writable after it
	if (ADCx->CR & ADC_CR_ADSTART) {
		ADCx->CR |= ADC_CR_ADSTP;
		while (ADCx->CR & ADC_CR_ADSTP) {}
	}
	ADCx->CFGR1 &= ~(ADC_CFGR1_EXTEN | ADC_CFGR1_DMAEN | ADC_CFGR1_DMACFG
	                 | ADC_CFGR1_CONT | ADC_CFGR1_OVRMOD);
	ADCx->ISR    = ADC_ISR_OVR;
#elif defined(STM32F4) || defined(STM32F7)
	// No ADSTP: disarm the trigger and leave continuous mode, a conversion
	// in progress completes
	ADCx->CR2   &= ~(ADC_CR2_EXTEN | ADC_CR2_DMA | ADC_CR2_DDS | ADC_CR2_CONT);
	ADCx->SR     = ~ADC_SR_OVR;
#endif

	if (_dma) {
		_dma->Stream_EN(DISABLE);
		_dma->ClearFlags();
	}
	_mode = Mode::Idle;
}

// ---------------------------------------------------------------------------
// ADC_N::IRQ_en
// ---------------------------------------------------------------------------

void ADC_N::IRQ_en(IRQ irq, FunctionalState en)
{
	if (!_info) return;

	// DMA transfer complete: interrupt of the attached DMA stream
	if (irq == IRQ::DMA_TC) {
		if (!_dma) return;
		const IRQn_Type irqn = _dma->GetIRQn();
		if (en && !_dma_registered) {
			IRQ_Registry::Register(irqn, this);
			_dma_registered = true;
			_dma->Enable_IRQ(DMA_Sx::IRQ::TC);   // also unmasks the NVIC line
		} else if (!en && _dma_registered) {
			_dma->Disable_IRQ(DMA_Sx::IRQ::TC);
			IRQ_Registry::Unregister(irqn, this);
			_dma_registered = false;
			if (!IRQ_Registry::HasHandlers(irqn)) NVIC_DisableIRQ(irqn);
		}
		return;
	}

#if defined(STM32G0)
	if (en) ADCx->IER |=  static_cast<uint32_t>(irq);
	else    ADCx->IER &= ~static_cast<uint32_t>(irq);
	constexpr uint32_t all = ADC_IER_EOCIE | ADC_IER_EOSIE | ADC_IER_OVRIE;
	bool any = (ADCx->IER & all) != 0u;
#elif defined(STM32F4) || defined(STM32F7)
	if (en) ADCx->CR1 |=  static_cast<uint32_t>(irq);
	else    ADCx->CR1 &= ~static_cast<uint32_t>(irq);
	constexpr uint32_t all = ADC_CR1_EOCIE | ADC_CR1_OVRIE;
	bool any = (ADCx->CR1 & all) != 0u;
#endif

	// The line may be shared (F4/F7: ADC1/2/3; G0: COMP), so track our own
	// registration instead of the NVIC state and mask only when nobody is left.
	if (any && !_irq_registered) {
		IRQ_Registry::Register(_info->irqn, this);
		_irq_registered = true;
		NVIC_EnableIRQ(_info->irqn);
	} else if (!any && _irq_registered) {
		IRQ_Registry::Unregister(_info->irqn, this);
		_irq_registered = false;
		if (!IRQ_Registry::HasHandlers(_info->irqn))
			NVIC_DisableIRQ(_info->irqn);
	}
}

// ---------------------------------------------------------------------------
// ADC_N::HandleIRQ
// ---------------------------------------------------------------------------

void ADC_N::HandleIRQ()
{
	// --- ADC interrupt (EOC / EOS / OVR) ---
	// Only sources enabled in IER/CR1 are handled: this handler is also
	// called from the DMA line, and polled flags must stay untouched.
#if defined(STM32G0)
	// IER bits mirror ISR bit positions
	const uint32_t flags = ADCx->ISR & ADCx->IER
	                     & (ADC_ISR_EOC | ADC_ISR_EOS | ADC_ISR_OVR);
	if (flags) ADCx->ISR = flags;   // clear by writing 1
	const bool eoc = (flags & ADC_ISR_EOC) != 0u;
	const bool eos = (flags & ADC_ISR_EOS) != 0u;
	const bool ovr = (flags & ADC_ISR_OVR) != 0u;
#elif defined(STM32F4) || defined(STM32F7)
	const uint32_t sr  = ADCx->SR;
	const uint32_t cr1 = ADCx->CR1;
	const bool eoc = (sr & ADC_SR_EOC) && (cr1 & ADC_CR1_EOCIE);
	const bool eos = eoc;   // EOCS = 0: EOC marks end of sequence
	const bool ovr = (sr & ADC_SR_OVR) && (cr1 & ADC_CR1_OVRIE);
	const uint32_t clr = (eoc ? ADC_SR_EOC : 0u) | (ovr ? ADC_SR_OVR : 0u);
	if (clr) ADCx->SR = ~clr;   // rc_w0: writing 1 leaves other flags as they are
#endif

	// --- DMA TC (fired by the DMA stream, routed here) ---
	const Mode mode = _mode;
	if (_dma && (mode == Mode::Dma || mode == Mode::Trig || mode == Mode::ContDma)
	         && _dma->GetTC_Flag()) {
		if (mode == Mode::Dma) Stop();       // StartDMA: the sequence is in, done
		else _dma->ClearTC_Flag();           // circular: the stream keeps running
		if (_dma_cb) _dma_cb();
	}

	if (eoc && _eoc_cb) _eoc_cb();
	if (eos && _eos_cb) _eos_cb();
	if (ovr) {
		// DMA requests stop on overrun; StartCont() without DMA overwrites
		// by design and keeps running
		if (mode != Mode::Idle && mode != Mode::Cont) Stop();
		if (_ovr_cb) _ovr_cb();
	}
}
