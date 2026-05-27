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

void ADC_N::AddChannel(ADC_PIN pin)
{
	if (!pin.IsValid()) return;
	if (_ch_count >= 4) { System::DebugTrap("ADC_N: channel slot overflow (max 4)"); }
	if (pin.adc_base && pin.adc_base != reinterpret_cast<uint32_t>(ADCx))
		System::DebugTrap("ADC_N: pin belongs to wrong ADC peripheral");
	_ch[_ch_count++] = pin;
}

// ---------------------------------------------------------------------------
// ADC_N::Init — table lookup + clock enable
// ---------------------------------------------------------------------------

SysInitStatus ADC_N::Init()
{
	for (const auto& e : adc_table)
		if (e.periph == ADCx) { _info = &e; break; }
	if (!_info) return SysInitStatus::InitError;
	*_info->clk_reg |= _info->clk_bit;
	return SysInitStatus::InitOK;
}

// ---------------------------------------------------------------------------
// ADC_N::SetUp — public overloads
// ---------------------------------------------------------------------------

SysInitStatus ADC_N::SetUp(SMPL smpl)
{
	return SetUpInternal(TRIG::SW, smpl);
}

SysInitStatus ADC_N::SetUp(TRIG trig, SMPL smpl)
{
	return SetUpInternal(trig, smpl);
}

// ---------------------------------------------------------------------------
// ADC_N::SetUpInternal — common init path
// ---------------------------------------------------------------------------

SysInitStatus ADC_N::SetUpInternal(TRIG trig, SMPL smpl)
{
	if (_ch_count == 0) return SysInitStatus::InitError;

	SysInitStatus s = Init();
	if (s != SysInitStatus::InitOK) return s;

#if defined(STM32G0)

	// 1. Enable voltage regulator (must be done before calibration)
	ADCx->CR = ADC_CR_ADVREGEN;
	System::Delay_ms(1);   // t_ADCVREG_STUP ≤ 20 µs; 1 ms is safe

	// 2. Calibration — clear ADEN first (must be 0 during calibration)
	ADCx->CR &= ~ADC_CR_ADEN;
	ADCx->CR |=  ADC_CR_ADCAL;
	while (ADCx->CR & ADC_CR_ADCAL) {}

	// 3. Sampling time — all channels use SMP1 (SMPSEL = 0 by default)
	ADCx->SMPR = static_cast<uint32_t>(smpl);   // SMP1[2:0]

	// 4. Build CHSELR bitmask from the stored channel list
	uint32_t chsel = 0;
	for (uint8_t i = 0; i < _ch_count; ++i)
		chsel |= 1u << _ch[i].channel;
	ADCx->CHSELR = chsel;

	// Wait for channel configuration ready
	while (!(ADCx->ISR & ADC_ISR_CCRDY)) {}
	ADCx->ISR = ADC_ISR_CCRDY;   // clear by writing 1

	// 5. CFGR1 — external trigger (if requested)
	uint32_t cfgr = 0;
	if (trig != TRIG::SW) {
		cfgr |= ADC_CFGR1_EXTEN_0;   // rising-edge sensitivity
		cfgr |= (static_cast<uint32_t>(trig) << ADC_CFGR1_EXTSEL_Pos);
	}
	ADCx->CFGR1 = cfgr;

	// 6. Enable ADC
	ADCx->ISR = ADC_ISR_ADRDY;
	ADCx->CR |= ADC_CR_ADEN;
	while (!(ADCx->ISR & ADC_ISR_ADRDY)) {}

	// 7. Configure GPIO pins as ANALOG
	for (uint8_t i = 0; i < _ch_count; ++i)
		PIN(reinterpret_cast<GPIO_TypeDef*>(_ch[i].port), _ch[i].pin)
		    .SetUp(PIN::TYPE::ANALOG);

#elif defined(STM32F4)

	// 1. Common control: TSVREFE for internal channels if needed
	//    (not set here — user enables it manually if reading Temp/Vref)

	// 2. Sampling time — set uniformly for all selected channels
	//    SMPR2 covers CH0-CH9 (3 bits each), SMPR1 covers CH10-CH18
	uint32_t smpr_val  = static_cast<uint32_t>(smpl);
	uint32_t smpr2 = 0, smpr1 = 0;
	for (uint8_t i = 0; i < _ch_count; ++i) {
		uint8_t ch = _ch[i].channel;
		if (ch < 10)
			smpr2 |= smpr_val << (ch * 3u);
		else
			smpr1 |= smpr_val << ((ch - 10u) * 3u);
	}
	ADCx->SMPR1 = smpr1;
	ADCx->SMPR2 = smpr2;

	// 3. Build conversion sequence (SQR registers)
	//    Single channel or first channel for scan mode
	ADCx->SQR1 = ((_ch_count - 1u) << ADC_SQR1_L_Pos);   // sequence length
	for (uint8_t i = 0; i < _ch_count; ++i) {
		if (i < 6)
			ADCx->SQR3 |= (uint32_t)_ch[i].channel << (i * 5u);
		else if (i < 12)
			ADCx->SQR2 |= (uint32_t)_ch[i].channel << ((i - 6u) * 5u);
		else
			ADCx->SQR1 |= (uint32_t)_ch[i].channel << ((i - 12u) * 5u);
	}

	// 4. CR1 — SCAN mode if more than one channel
	ADCx->CR1 = (_ch_count > 1) ? ADC_CR1_SCAN : 0u;

	// 5. CR2 — power on, external trigger
	uint32_t cr2 = ADC_CR2_ADON;
	if (trig != TRIG::SW) {
		cr2 |= ADC_CR2_EXTEN_0;   // rising edge
		cr2 |= (static_cast<uint32_t>(trig) << ADC_CR2_EXTSEL_Pos);
	}
	ADCx->CR2 = cr2;

	// 6. Configure GPIO as ANALOG
	for (uint8_t i = 0; i < _ch_count; ++i)
		PIN(reinterpret_cast<GPIO_TypeDef*>(_ch[i].port), _ch[i].pin)
		    .SetUp(PIN::TYPE::ANALOG);

#endif

	return SysInitStatus::InitOK;
}

// ---------------------------------------------------------------------------
// ADC_N::IRQ_en
// ---------------------------------------------------------------------------

void ADC_N::IRQ_en(IRQ irq, FunctionalState en)
{
	if (!_info) return;

#if defined(STM32G0)
	if (en) ADCx->IER |=  static_cast<uint32_t>(irq);
	else    ADCx->IER &= ~static_cast<uint32_t>(irq);
	constexpr uint32_t all = ADC_IER_EOCIE | ADC_IER_EOSIE | ADC_IER_OVRIE;
	bool any = (ADCx->IER & all) != 0u;
#elif defined(STM32F4)
	if (en) ADCx->CR1 |=  static_cast<uint32_t>(irq);
	else    ADCx->CR1 &= ~static_cast<uint32_t>(irq);
	constexpr uint32_t all = ADC_CR1_EOCIE | ADC_CR1_OVRIE;
	bool any = (ADCx->CR1 & all) != 0u;
#endif

	if (en && !NVIC_GetEnableIRQ(_info->irqn)) {
		IRQ_Registry::Register(_info->irqn, this);
		NVIC_EnableIRQ(_info->irqn);
	} else if (!en && !any) {
		IRQ_Registry::Unregister(_info->irqn);
		NVIC_DisableIRQ(_info->irqn);
	}
}

// ---------------------------------------------------------------------------
// ADC_N::AttachDMA
// ---------------------------------------------------------------------------

void ADC_N::AttachDMA(DMA_Sx* dma)
{
	if (!_info || !dma) return;
	_dma = dma;

	DMA_Sx::StreamSettings cfg;
	cfg.channel     = _info->dma_req;
	cfg.direction   = DMA_Sx::DIR::From_Per;
	cfg.data_size   = DMA_Sx::SIZE::Half_Word;
	cfg.minc        = true;
	cfg.per_address = reinterpret_cast<uint32_t>(&ADCx->DR);
	dma->SetUp(cfg);

	IRQn_Type irqn = dma->GetIRQn();
	if (irqn != static_cast<IRQn_Type>(-1)) {
		IRQ_Registry::Register(irqn, this);
		NVIC_EnableIRQ(irqn);
		dma->Enable_IRQ(DMA_Sx::IRQ::TC);
	}
}

// ---------------------------------------------------------------------------
// ADC_N::StartDMA
// ---------------------------------------------------------------------------

SysStatus ADC_N::StartDMA(uint16_t* buf, uint32_t len)
{
	if (!_dma || !buf || !len) return SysStatus::Error;
	if (_dma_busy)             return SysStatus::Busy;

	_dma_busy = true;
	_dma->ClearFlags();
	_dma->SetMemAddr(reinterpret_cast<uint32_t>(buf));
	_dma->SetCount(len);

#if defined(STM32G0)
	ADCx->CFGR1 |= ADC_CFGR1_DMAEN;   // one-shot (DMACFG = 0)
#elif defined(STM32F4)
	ADCx->CR2   |= ADC_CR2_DMA | ADC_CR2_DDS;
#endif

	_dma->Stream_EN(ENABLE);
	Start();
	return SysStatus::OK;
}

// ---------------------------------------------------------------------------
// ADC_N::Start / Stop
// ---------------------------------------------------------------------------

void ADC_N::Start()
{
#if defined(STM32G0)
	ADCx->CR |= ADC_CR_ADSTART;
#elif defined(STM32F4)
	ADCx->CR2 |= ADC_CR2_SWSTART;
#endif
}

void ADC_N::Stop()
{
#if defined(STM32G0)
	ADCx->CR |= ADC_CR_ADSTP;
	while (ADCx->CR & ADC_CR_ADSTP) {}
#elif defined(STM32F4)
	// F4: no ADSTP — disable ADC
	ADCx->CR2 &= ~ADC_CR2_ADON;
#endif
}

// ---------------------------------------------------------------------------
// ADC_N::HandleIRQ
// ---------------------------------------------------------------------------

void ADC_N::HandleIRQ()
{
	// --- ADC interrupt (EOC / EOS / OVR) ---
#if defined(STM32G0)
	uint32_t isr = ADCx->ISR;
	ADCx->ISR = isr;                             // clear by writing 1
	if ((isr & ADC_ISR_EOC) && _eoc_cb) _eoc_cb();
	if ((isr & ADC_ISR_EOS) && _eos_cb) _eos_cb();
#elif defined(STM32F4)
	uint32_t sr = ADCx->SR;
	ADCx->SR = 0;
	if ((sr & ADC_SR_EOC) && _eoc_cb) _eoc_cb();
#endif

	// --- DMA TC interrupt (fired by the DMA stream, routed here) ---
	if (_dma && _dma_busy && _dma->GetTC_Flag()) {
		_dma->Stream_EN(DISABLE);
		_dma->ClearFlags();
#if defined(STM32G0)
		ADCx->CFGR1 &= ~ADC_CFGR1_DMAEN;
#elif defined(STM32F4)
		ADCx->CR2 &= ~(ADC_CR2_DMA | ADC_CR2_DDS);
#endif
		_dma_busy = false;
		if (_dma_cb) _dma_cb();
	}
}
