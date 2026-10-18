#include "adc.hpp"

#define ADC_DEFS_CPP
#include "adc_defs.hpp"
#undef ADC_DEFS_CPP

#if defined(STM32G0)
static constexpr uint32_t ADC_TIMEOUT_MS = 1u;
static constexpr uint32_t CHSELR_EMPTY = 0xFFFFFFFFu;
static constexpr uint32_t SMPSEL_ALL = 0x7FFFFu << ADC_SMPR_SMPSEL0_Pos;
static constexpr uint32_t RUN_FIELDS = ADC_CFGR1_EXTEN | ADC_CFGR1_EXTSEL
	| ADC_CFGR1_DMAEN | ADC_CFGR1_DMACFG | ADC_CFGR1_CONT | ADC_CFGR1_OVRMOD;

bool ADC_N::Enable()
{
	if (ADCx->CR & ADC_CR_ADEN) return true;
	ADCx->ISR = ADC_ISR_ADRDY;
	const uint32_t start = System::GetTick();
	// ADEN can be ignored just after calibration; retry until ADRDY or timeout.
	while (!(ADCx->ISR & ADC_ISR_ADRDY)) {
		if (!(ADCx->CR & ADC_CR_ADEN)) ADCx->CR |= ADC_CR_ADEN;
		if (System::GetTick() - start >= ADC_TIMEOUT_MS) return false;
	}
	return true;
}

bool ADC_N::Disable()
{
	if ((ADCx->CR & ADC_CR_ADSTART) && !(ADCx->CR & ADC_CR_ADSTP))
		ADCx->CR |= ADC_CR_ADSTP;
	if (!System::WaitBits(ADCx->CR, ADC_CR_ADSTART | ADC_CR_ADSTP, 0u, ADC_TIMEOUT_MS))
		return false;
	if ((ADCx->CR & ADC_CR_ADEN) && !(ADCx->CR & ADC_CR_ADDIS))
		ADCx->CR |= ADC_CR_ADDIS;
	return System::WaitBits(ADCx->CR, ADC_CR_ADEN | ADC_CR_ADDIS, 0u, ADC_TIMEOUT_MS);
}

bool ADC_N::WriteChselr(uint32_t chsel)
{
	ADCx->ISR = ADC_ISR_CCRDY;
	ADCx->CHSELR = chsel;
	if (!System::WaitBits(ADCx->ISR, ADC_ISR_CCRDY, ADC_ISR_CCRDY, ADC_TIMEOUT_MS))
		return false;
	ADCx->ISR = ADC_ISR_CCRDY;
	return true;
}

bool ADC_N::WriteCfgr1(uint32_t clear, uint32_t set)
{
	if (!Disable()) return false;
	ADCx->CFGR1 = (ADCx->CFGR1 & ~clear) | set;
	return Enable();
}

bool ADC_N::SetSeqMode(bool seq)
{
	uint32_t chsel = CHSELR_EMPTY;
	if (!seq) {
		const uint32_t old = ADCx->CHSELR;
		chsel = 0;
		for (uint8_t i = 0; i < _ch_count; ++i)
			chsel |= 1u << ((old >> (i * 4u)) & 0xFu);
	}
	if (!WriteCfgr1(ADC_CFGR1_CHSELRMOD, seq ? ADC_CFGR1_CHSELRMOD : 0u))
		return false;
	_seq_mode = seq;
	return WriteChselr(chsel);
}
#else
static constexpr uint32_t RUN_FIELDS = ADC_CR2_EXTEN | ADC_CR2_EXTSEL
	| ADC_CR2_DMA | ADC_CR2_DDS | ADC_CR2_CONT;
#endif

static void ClearRunFlags(ADC_TypeDef* adc)
{
#if defined(STM32G0)
	adc->ISR = ADC_ISR_EOC | ADC_ISR_EOS | ADC_ISR_OVR;
#else
	adc->SR = ~(ADC_SR_EOC | ADC_SR_OVR); // rc_w0: preserve unrelated flags.
#endif
}

// Setup is performed once; IRQ registration is published only after hardware setup.
SysInitStatus ADC_N::SetUp(SMPL smp1, SMPL smp2)
{
	if (_info) return SysInitStatus::InitError;
	const PeriphInfo* info = nullptr;
	for (const auto& e : adc_table)
		if (e.periph == ADCx) { info = &e; break; }
	if (!info) return SysInitStatus::InitError;
	*info->clk_reg |= info->clk_bit;
	(void)*info->clk_reg;

	if (ADCx == ADC1) ADC->CCR &= ~(EN_VREFINT | EN_TEMP | EN_VBAT);
	bool ready = true;
#if defined(STM32G0)
	ADCx->IER = 0;
	ready = System::WaitBits(ADCx->CR, ADC_CR_ADCAL, 0u, ADC_TIMEOUT_MS) && Disable();
	if (ready) {
		ADCx->CFGR1 = 0;
		ADCx->CFGR2 = ADC_CFGR2_CKMODE_0; // Synchronous PCLK/2.
		ADCx->CR |= ADC_CR_ADVREGEN;
		System::Delay_ms(1);             // Regulator startup.
		ADCx->CR |= ADC_CR_ADCAL;
		ready = System::WaitBits(ADCx->CR, ADC_CR_ADCAL, 0u, ADC_TIMEOUT_MS);
	}
	if (ready) {
		ADCx->SMPR = (static_cast<uint32_t>(smp1) << ADC_SMPR_SMP1_Pos)
			| (static_cast<uint32_t>(smp2) << ADC_SMPR_SMP2_Pos);
		ADCx->CFGR1 = ADC_CFGR1_CHSELRMOD;
		_seq_mode = true;
		_ascending = true;
		_last_ch = 0;
		ready = Enable() && WriteChselr(CHSELR_EMPTY);
	}
#else
	(void)smp1;
	(void)smp2;
	ADCx->CR2 = 0;
	ADCx->CR1 = 0;
	// ADCPRE is shared by the ADCs even when conversion is independent.
	if ((ADC->CCR & ADC_CCR_ADCPRE) != ADC_CCR_ADCPRE_0)
		ADC->CCR = (ADC->CCR & ~ADC_CCR_ADCPRE) | ADC_CCR_ADCPRE_0;
	ADCx->SMPR1 = 0;
	ADCx->SMPR2 = 0;
	ADCx->SQR1 = 0;
	ADCx->SQR2 = 0;
	ADCx->SQR3 = 0;
	ADCx->CR2 = ADC_CR2_ADON;
	System::Delay_ms(1);                 // ADC stabilization.
	_last_data = 0;
#endif
	ClearRunFlags(ADCx);
	if (!ready) {
		*info->clk_reg &= ~info->clk_bit;
		return SysInitStatus::InitError;
	}

	const bool enabled = NVIC_GetEnableIRQ(info->irqn) != 0;
	NVIC_DisableIRQ(info->irqn);
	if (IRQ_Registry::Register(info->irqn, this) != SysInitStatus::InitOK) {
#if defined(STM32G0)
		Disable();
#else
		ADCx->CR2 = 0;
#endif
		*info->clk_reg &= ~info->clk_bit;
		if (enabled) NVIC_EnableIRQ(info->irqn);
		return SysInitStatus::InitError;
	}
	_ch_count = 0;
	_status = SysStatus::OK;
	_info = info;
	NVIC_EnableIRQ(info->irqn);
	return SysInitStatus::InitOK;
}

bool ADC_N::InSequence(uint8_t ch) const
{
#if defined(STM32G0)
	const uint32_t chsel = ADCx->CHSELR;
	if (!_seq_mode) return (chsel >> ch) & 1u;
	for (uint8_t i = 0; i < _ch_count; ++i)
		if (((chsel >> (i * 4u)) & 0xFu) == ch) return true;
#else
	for (uint8_t i = 0; i < _ch_count; ++i) {
		const uint32_t sqr = i < 6 ? ADCx->SQR3 : i < 12 ? ADCx->SQR2 : ADCx->SQR1;
		if (((sqr >> ((i % 6u) * 5u)) & 0x1Fu) == ch) return true;
	}
#endif
	return false;
}

// This helper only appends a hardware rank and its per-channel sampling field.
SysStatus ADC_N::AddRank(uint8_t ch, CH_SMPL smpl)
{
	const bool present = InSequence(ch);
#if defined(STM32G0)
	const uint32_t sel_bit = 1u << (ADC_SMPR_SMPSEL0_Pos + ch);
	const bool sel2 = smpl == CH_SMPL::SMP2;
	if (present && (((ADCx->SMPR & sel_bit) != 0u) != sel2)) return SysStatus::Error;
	const bool ascending = !_ch_count || ch > _last_ch;
	if (!_seq_mode || ch > 14 || _ch_count >= 8) {
		if (!_ascending || !ascending) return SysStatus::Error;
		if ((_seq_mode && !SetSeqMode(false)) || !WriteChselr(ADCx->CHSELR | (1u << ch))) {
			_status = SysStatus::Timeout;
			return SysStatus::Timeout;
		}
	} else {
		const uint32_t shift = _ch_count * 4u;
		if (!WriteChselr((ADCx->CHSELR & ~(0xFu << shift)) | (static_cast<uint32_t>(ch) << shift))) {
			_status = SysStatus::Timeout;
			return SysStatus::Timeout;
		}
	}
	if (sel2) ADCx->SMPR |= sel_bit;
	else ADCx->SMPR &= ~sel_bit;
	_ascending = _ascending && ascending;
	_last_ch = ch;
#else
	if (_ch_count >= 16) return SysStatus::Error;
	const uint32_t v = static_cast<uint32_t>(smpl);
	volatile uint32_t& smpr = ch < 10 ? ADCx->SMPR2 : ADCx->SMPR1;
	const uint32_t shift = (ch < 10 ? ch : ch - 10u) * 3u;
	if (present && ((smpr >> shift) & 7u) != v) return SysStatus::Error;
	smpr = (smpr & ~(7u << shift)) | (v << shift);
	const uint8_t n = _ch_count;
	volatile uint32_t& sqr = n < 6 ? ADCx->SQR3 : n < 12 ? ADCx->SQR2 : ADCx->SQR1;
	const uint32_t rank_shift = (n % 6u) * 5u;
	sqr = (sqr & ~(0x1Fu << rank_shift)) | (static_cast<uint32_t>(ch) << rank_shift);
	ADCx->SQR1 = (ADCx->SQR1 & ~ADC_SQR1_L) | (static_cast<uint32_t>(n) << ADC_SQR1_L_Pos);
	if (n) ADCx->CR1 |= ADC_CR1_SCAN;
#endif
	++_ch_count;
	return SysStatus::OK;
}

SysStatus ADC_N::AddChannel(ADC_PIN pin, CH_SMPL smpl)
{
	if (!_info) return SysStatus::NotInit;
	if (IsBusy()) return SysStatus::Busy;
	if (!pin.IsValid() || pin.periph_base != reinterpret_cast<uint32_t>(ADCx))
		return SysStatus::Error;
	const SysStatus result = AddRank(pin.channel, smpl);
	if (result == SysStatus::OK) pin.SetUp(PIN::TYPE::ANALOG);
	return result;
}

SysStatus ADC_N::AddChannel(INTERNAL in, CH_SMPL smpl)
{
	if (!_info) return SysStatus::NotInit;
	if (IsBusy()) return SysStatus::Busy;
	if (ADCx != ADC1) return SysStatus::Error;
	uint8_t ch;
	uint32_t en;
	switch (in) {
		case INTERNAL::VREFINT: ch = CH_VREFINT; en = EN_VREFINT; break;
		case INTERNAL::TEMP: ch = CH_TEMP; en = EN_TEMP; break;
		case INTERNAL::VBAT: ch = CH_VBAT; en = EN_VBAT; break;
		default: return SysStatus::Error;
	}
	if (CH_TEMP == CH_VBAT) {
		const bool vbat = (ADC->CCR & EN_VBAT) != 0u;
		if ((in == INTERNAL::TEMP && vbat)
			|| (in == INTERNAL::VBAT && InSequence(CH_VBAT) && !vbat))
			return SysStatus::Error;
	}
	const SysStatus result = AddRank(ch, smpl);
	if (result != SysStatus::OK) return result;
	if (!(ADC->CCR & en)) {
		ADC->CCR |= en;
		System::Delay_ms(1); // Source startup, not sampling time.
	}
	return SysStatus::OK;
}

SysStatus ADC_N::ClearChannels()
{
	if (!_info) return SysStatus::NotInit;
	if (IsBusy()) return SysStatus::Busy;
#if defined(STM32G0)
	// Also re-establish the hardware mode after an earlier CHSELR timeout.
	if (!WriteCfgr1(ADC_CFGR1_CHSELRMOD, ADC_CFGR1_CHSELRMOD)
		|| !WriteChselr(CHSELR_EMPTY)) {
		_status = SysStatus::Timeout;
		return SysStatus::Timeout;
	}
	_seq_mode = true;
	_ascending = true;
	_last_ch = 0;
	ADCx->SMPR &= ~SMPSEL_ALL;
#else
	ADCx->SQR1 = 0;
	ADCx->SQR2 = 0;
	ADCx->SQR3 = 0;
	ADCx->CR1 &= ~ADC_CR1_SCAN;
#endif
	_ch_count = 0;
	if (ADCx == ADC1) ADC->CCR &= ~(EN_VREFINT | EN_TEMP | EN_VBAT);
	ClearRunFlags(ADCx);
	_status = SysStatus::OK;
	return SysStatus::OK;
}

// Validate the selected route and acquire once, as in USART::AttachDMA.
SysInitStatus ADC_N::AttachDMA(DMA_Sx* dma)
{
	if (!_info || !dma || _dma || IsBusy()) return SysInitStatus::InitError;
	DMA_Sx::DMAReq req{};
	for (const auto& route : _info->dma_req)
		if (dma->CanUseRequest(route)) { req = route; break; }
	if (!req.IsValid()) return SysInitStatus::InitError;
	DMA_Sx::StreamSettings cfg;
	cfg.channel = req.ch;
	cfg.direction = DMA_Sx::DIR::From_Per;
	cfg.data_size = DMA_Sx::SIZE::Half_Word;
	cfg.minc = true;
	cfg.per_address = reinterpret_cast<uint32_t>(&ADCx->DR);
	if (dma->SetUp(cfg) != SysInitStatus::InitOK) return SysInitStatus::InitError;

	const IRQn_Type irqn = dma->GetIRQn();
	const bool enabled = NVIC_GetEnableIRQ(irqn) != 0;
	NVIC_DisableIRQ(irqn);
	if (IRQ_Registry::Register(irqn, this) != SysInitStatus::InitOK) {
		dma->Release();
		if (enabled) NVIC_EnableIRQ(irqn);
		return SysInitStatus::InitError;
	}
	_dma = dma;
	_dma_tc_irq = true;
	dma->Enable_IRQ(DMA_Sx::IRQ::TC);
	dma->Enable_IRQ(DMA_Sx::IRQ::TE);
#if defined(STM32F4) || defined(STM32F7)
	dma->Enable_IRQ(DMA_Sx::IRQ::DME);
#endif
	return SysInitStatus::InitOK;
}

SysInitStatus ADC_N::AttachTrig(TIM_TriggerGenerator* trg)
{
	if (!_info || !trg || IsBusy()) return SysInitStatus::InitError;
	_trg = trg;
	_extsel = trg->GetReq().extsel;
	_trig_set = true;
	return SysInitStatus::InitOK;
}

SysInitStatus ADC_N::AttachExtTrig()
{
	if (!_info || IsBusy()) return SysInitStatus::InitError;
	_trg = nullptr;
	_extsel = EXTSEL_EXTI11;
	_trig_set = true;
	return SysInitStatus::InitOK;
}

bool ADC_N::SetRunMode(uint32_t bits)
{
#if defined(STM32G0)
	return WriteCfgr1(RUN_FIELDS, bits);
#else
	const bool enabled = (ADCx->CR2 & ADC_CR2_ADON) != 0u;
	ADCx->CR2 = (ADCx->CR2 & ~RUN_FIELDS) | bits | ADC_CR2_ADON;
	if (!enabled) System::Delay_ms(1);
	return true;
#endif
}

SysStatus ADC_N::Start()
{
	if (!_info) return SysStatus::NotInit;
	if (IsBusy()) return SysStatus::Busy;
	if (!_ch_count) return SysStatus::Error;
	if (!SetRunMode(0u)) {
		_status = SysStatus::Timeout;
		return SysStatus::Timeout;
	}
	ClearRunFlags(ADCx);
	_mode = Mode::Single;
	_status = SysStatus::Busy;
#if defined(STM32G0)
	ADCx->CR |= ADC_CR_ADSTART;
#else
	ADCx->CR2 |= ADC_CR2_SWSTART;
#endif
	return SysStatus::OK;
}

SysStatus ADC_N::Start_DMA(uint16_t* buf) { return StartDma(buf, _ch_count, Mode::Dma); }
SysStatus ADC_N::StartTrig_DMA(uint16_t* buf, uint32_t len) { return StartDma(buf, len, Mode::Trig); }
SysStatus ADC_N::StartCont_DMA(uint16_t* buf, uint32_t len) { return StartDma(buf, len, Mode::ContDma); }

SysStatus ADC_N::StartDma(uint16_t* buf, uint32_t len, Mode mode)
{
	if (!_info) return SysStatus::NotInit;
	if (IsBusy()) return SysStatus::Busy;
	if (!_dma || !buf || !_ch_count || !len || len > 0xFFFFu || len % _ch_count)
		return SysStatus::Error;
	if (mode == Mode::Trig && !_trig_set) return SysStatus::Error;
	_status = SysStatus::OK; // A previously polled single capture has already ended.
	ClearRunFlags(ADCx); // Clear an earlier EOC before enabling ADC DMA requests.
#if defined(STM32G0)
	uint32_t bits = ADC_CFGR1_DMAEN;
	if (mode != Mode::Dma) bits |= ADC_CFGR1_DMACFG;
	if (mode == Mode::ContDma) bits |= ADC_CFGR1_CONT;
	if (mode == Mode::Trig)
		bits |= ADC_CFGR1_EXTEN_0 | (static_cast<uint32_t>(_extsel) << ADC_CFGR1_EXTSEL_Pos);
#else
	uint32_t bits = ADC_CR2_DMA;
	if (mode != Mode::Dma) bits |= ADC_CR2_DDS;
	if (mode == Mode::ContDma) bits |= ADC_CR2_CONT;
	if (mode == Mode::Trig) bits |= static_cast<uint32_t>(_extsel) << ADC_CR2_EXTSEL_Pos;
#endif
	if (!SetRunMode(bits)) {
		_status = SysStatus::Timeout;
		return SysStatus::Timeout;
	}
	_dma->CIRC(mode == Mode::Dma ? DISABLE : ENABLE);
	if (_dma->SetMemAddr(reinterpret_cast<uint32_t>(buf)) != SysStatus::OK
		|| _dma->SetCount(len) != SysStatus::OK) return SysStatus::Error;
	_dma->ClearFlags();
	System::CacheCleanInvalidate(buf, len * sizeof(uint16_t));
	_buf = buf;
	_buf_bytes = len * sizeof(uint16_t);
	if (mode == Mode::Dma) {
		_dma_tc_irq = true; // Finite DMA completion is serviced automatically.
		_dma->Enable_IRQ(DMA_Sx::IRQ::TC);
	}
	_mode = mode;
	_status = SysStatus::Busy;
	_dma->Stream_EN(ENABLE);
#if defined(STM32G0)
	ADCx->CR |= ADC_CR_ADSTART;
#else
	if (mode == Mode::Trig) ADCx->CR2 |= ADC_CR2_EXTEN_0;
	else ADCx->CR2 |= ADC_CR2_SWSTART;
#endif
	if (mode == Mode::Trig && _trg) _trg->Start(); // Timer last, after ADC is armed.
	return SysStatus::OK;
}

SysStatus ADC_N::StartCont()
{
	if (!_info) return SysStatus::NotInit;
	if (IsBusy()) return SysStatus::Busy;
	if (_ch_count != 1) return SysStatus::Error;
#if defined(STM32G0)
	if (!SetRunMode(ADC_CFGR1_CONT | ADC_CFGR1_OVRMOD)) {
		_status = SysStatus::Timeout;
		return SysStatus::Timeout;
	}
#else
	SetRunMode(ADC_CR2_CONT);
#endif
	ClearRunFlags(ADCx);
	_mode = Mode::Cont;
	_status = SysStatus::Busy;
#if defined(STM32G0)
	ADCx->CR |= ADC_CR_ADSTART;
#else
	ADCx->CR2 |= ADC_CR2_SWSTART;
#endif
	return SysStatus::OK;
}

bool ADC_N::IsBusy() const
{
	if (!_info || _status != SysStatus::Busy) return false;
	if (_mode != Mode::Single) return true;
#if defined(STM32G0)
	return (ADCx->CR & ADC_CR_ADSTART) != 0u;
#else
	return !(ADCx->SR & ADC_SR_EOC);
#endif
}

SysStatus ADC_N::GetStatus() const
{
	return _status == SysStatus::Busy && !IsBusy() ? SysStatus::OK : _status;
}

uint16_t ADC_N::GetData()
{
	if (!_info) return 0;
#if defined(STM32F4) || defined(STM32F7)
	if (_mode == Mode::Single && _status == SysStatus::Busy) {
		if (!(ADCx->SR & ADC_SR_EOC)) return _last_data;
		_status = SysStatus::OK;
	}
	_last_data = static_cast<uint16_t>(ADCx->DR);
	return _last_data;
#else
	return static_cast<uint16_t>(ADCx->DR);
#endif
}

// Main-loop Stop/DeInit only. Failed DMA data is discarded without cache handover.
uint32_t ADC_N::StopDMA()
{
	if (!_dma || !_buf) return 0;
	_dma->Stream_EN(DISABLE);
	const uint32_t errors = _dma->GetErrorFlags();
	_dma->ClearFlags();
	if (!errors) System::CacheInvalidate(_buf, _buf_bytes);
	_buf = nullptr;
	_buf_bytes = 0;
	return errors;
}

SysStatus ADC_N::Stop()
{
	if (!_info) return SysStatus::NotInit;
	_status = SysStatus::OK; // Suppress capture notifications while stopping in main.
	if (_trg) _trg->Stop();
#if defined(STM32G0)
	if ((ADCx->CR & ADC_CR_ADSTART) && !(ADCx->CR & ADC_CR_ADSTP))
		ADCx->CR |= ADC_CR_ADSTP;
#else
	ADCx->CR2 &= ~(ADC_CR2_ADON | RUN_FIELDS);
#endif
	const uint32_t errors = StopDMA();
#if defined(STM32G0)
	if (!Disable()) {
		_status = SysStatus::Timeout;
		return SysStatus::Timeout;
	}
	ADCx->CFGR1 &= ~RUN_FIELDS;
	if (!Enable()) {
		_status = SysStatus::Timeout;
		return SysStatus::Timeout;
	}
#endif
	ClearRunFlags(ADCx);
	_status = errors ? SysStatus::Error : SysStatus::OK;
	return _status;
}

void ADC_N::IRQ_en(IRQ irq, FunctionalState en)
{
	if (!_info) return;
	if (irq == IRQ::DMA_TC) {
		if (!_dma) return;
		_dma_tc_irq = en != DISABLE;
		if (en) _dma->Enable_IRQ(DMA_Sx::IRQ::TC);
		else _dma->Disable_IRQ(DMA_Sx::IRQ::TC);
		return;
	}
#if defined(STM32G0)
	if (en) ADCx->IER |= static_cast<uint32_t>(irq);
	else ADCx->IER &= ~static_cast<uint32_t>(irq);
#else
	if (en) ADCx->CR1 |= static_cast<uint32_t>(irq);
	else ADCx->CR1 &= ~static_cast<uint32_t>(irq);
#endif
}

void ADC_N::HandleIRQ()
{
	if (!_info) return;
#if defined(STM32G0)
	const uint32_t sr = ADCx->ISR;
	const uint32_t flags = sr & ADCx->IER & (ADC_ISR_EOC | ADC_ISR_EOS | ADC_ISR_OVR);
	if (flags) ADCx->ISR = flags;
	const bool eoc = (flags & ADC_ISR_EOC) != 0u;
	const bool eos = (flags & ADC_ISR_EOS) != 0u;
	const bool overrun = (flags & ADC_ISR_OVR) != 0u;
#else
	const uint32_t sr = ADCx->SR;
	const uint32_t cr1 = ADCx->CR1;
	const bool eos = (sr & ADC_SR_EOC) && (cr1 & ADC_CR1_EOCIE);
	const bool overrun = (sr & ADC_SR_OVR) && (cr1 & ADC_CR1_OVRIE);
	const uint32_t flags = (eos ? ADC_SR_EOC : 0u) | (overrun ? ADC_SR_OVR : 0u);
	if (flags) ADCx->SR = ~flags;
#endif
	if (_status != SysStatus::Busy) {
		// Stop/DeInit may be preempted by an already pending DMA IRQ. Clear
		// only our resource's flags so it cannot keep retriggering this line.
		if (_dma) _dma->ClearFlags();
		return;
	}
	uint32_t errors = 0;
	if (overrun && _mode != Mode::Cont) errors |= static_cast<uint32_t>(Error::Overrun);
	if (_buf) {
		const bool complete = _dma_tc_irq && _dma->GetTC_Flag();
		// Stop a finite transfer before the single error snapshot and handover.
		if (complete && _mode == Mode::Dma) _dma->Stream_EN(DISABLE);
		if (_dma->GetErrorFlags()) errors |= static_cast<uint32_t>(Error::Dma);
		if (!errors && complete) {
			// Selective HT/TC clearing awaits the separate DMA API update.
			_dma->ClearFlags();
			System::CacheInvalidate(_buf, _buf_bytes);
			if (_mode == Mode::Dma) {
				_buf = nullptr;
				_buf_bytes = 0;
				_status = SysStatus::OK;
			}
			OnDmaComplete();
		}
	} else if (_mode == Mode::Single) {
#if defined(STM32G0)
		if (!(ADCx->CR & ADC_CR_ADSTART)) _status = SysStatus::OK;
#else
		if (sr & ADC_SR_EOC) _status = SysStatus::OK;
#endif
	}
	if (errors) {
		if (_trg && _mode == Mode::Trig) _trg->Stop();
#if defined(STM32G0)
		if ((ADCx->CR & ADC_CR_ADSTART) && !(ADCx->CR & ADC_CR_ADSTP))
			ADCx->CR |= ADC_CR_ADSTP; // No SysTick wait from IRQ.
#else
		ADCx->CR2 &= ~(ADC_CR2_ADON | RUN_FIELDS);
#endif
		if (_buf) {
			_dma->Stream_EN(DISABLE);
			_dma->ClearFlags();
			// Partial data is invalid: no cache operation or success notification.
			_buf = nullptr;
			_buf_bytes = 0;
		}
		_status = SysStatus::Error;
		OnError(errors);
		return;
	}
#if defined(STM32G0)
	if (eoc) OnEOC();
#endif
	if (eos) OnEOS();
}

void ADC_N::DeInit()
{
	if (!_info) return;
	_status = SysStatus::NotInit;
	if (_trg) _trg->Stop();
#if defined(STM32G0)
	ADCx->IER = 0;
	if ((ADCx->CR & ADC_CR_ADSTART) && !(ADCx->CR & ADC_CR_ADSTP))
		ADCx->CR |= ADC_CR_ADSTP;
#else
	ADCx->CR1 = 0;
	ADCx->CR2 = 0;
#endif
	StopDMA();
	const IRQn_Type none = static_cast<IRQn_Type>(-1);
	const IRQn_Type irqs[2] = { _info->irqn, _dma ? _dma->GetIRQn() : none };
	for (unsigned i = 0; i < 2; ++i) {
		if (irqs[i] == none || (i && irqs[i] == irqs[0])) continue;
		const bool enabled = NVIC_GetEnableIRQ(irqs[i]) != 0;
		NVIC_DisableIRQ(irqs[i]);
		IRQ_Registry::Unregister(irqs[i], this);
		if (IRQ_Registry::HasHandlers(irqs[i])) {
			if (enabled) NVIC_EnableIRQ(irqs[i]);
		} else NVIC_ClearPendingIRQ(irqs[i]);
	}
	if (_dma) _dma->Release();
#if defined(STM32G0)
	// G0 has one ADC: its reset also releases the regulator/common configuration.
	RCC->APBRSTR2 |= RCC_APBRSTR2_ADCRST;
	RCC->APBRSTR2 &= ~RCC_APBRSTR2_ADCRST;
#else
	ADCx->SQR1 = 0;
	ADCx->SQR2 = 0;
	ADCx->SQR3 = 0;
	ADCx->SMPR1 = 0;
	ADCx->SMPR2 = 0;
	_last_data = 0;
	if (ADCx == ADC1) ADC->CCR &= ~(EN_VREFINT | EN_TEMP | EN_VBAT);
#endif
	*_info->clk_reg &= ~_info->clk_bit;
	_info = nullptr;
	_dma = nullptr;
	_trg = nullptr;
	_trig_set = false;
	_dma_tc_irq = false;
	_extsel = 0;
	_ch_count = 0;
	_mode = Mode::Single;
}
