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
		if (!(ADCx->CR & ADC_CR_ADEN)) ADC_SET_BIT(ADCx->CR, ADC_CR_ADEN);
		if (System::GetTick() - start >= ADC_TIMEOUT_MS) return false;
	}
	return true;
}

bool ADC_N::Disable()
{
	uint32_t cr = ADCx->CR;
	if ((cr & ADC_CR_ADSTART) && !(cr & ADC_CR_ADSTP))
		ADC_SET_BIT(ADCx->CR, ADC_CR_ADSTP);
	if (!System::WaitBits(ADCx->CR, ADC_CR_ADSTART | ADC_CR_ADSTP, 0u, ADC_TIMEOUT_MS))
		return false;
	cr = ADCx->CR; // ADSTART/ADSTP have changed during the completed wait.
	if ((cr & ADC_CR_ADEN) && !(cr & ADC_CR_ADDIS))
		ADC_SET_BIT(ADCx->CR, ADC_CR_ADDIS);
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
	ADC_MODIFY_REG(ADCx->CFGR1, clear, set);
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
	if ((_conversion != Conversion::Single && _conversion != Conversion::Continuous)
		|| (_conversion == Conversion::Continuous && _trigger != Trigger::None))
		return SysInitStatus::InitError;
	const bool timer_source = _trigger != Trigger::None && _trigger != Trigger::EXTI11;
	if (timer_source != (_timer != nullptr) || (_timer && !_timer->GetPeriod()))
		return SysInitStatus::InitError;
	const PeriphInfo* info = nullptr;
	for (const auto& e : adc_table)
		if (e.periph == ADCx) { info = &e; break; }
	if (!info) return SysInitStatus::InitError;
	ADC_SET_BIT(*info->clk_reg, info->clk_bit);
	(void)*info->clk_reg;

	if (ADCx == ADC1) ADC_CLEAR_BIT(ADC->CCR, (EN_VREFINT | EN_TEMP | EN_VBAT));
	bool ready = true;
#if defined(STM32G0)
	ADCx->IER = 0;
	ready = System::WaitBits(ADCx->CR, ADC_CR_ADCAL, 0u, ADC_TIMEOUT_MS) && Disable();
	if (ready) {
		ADCx->CFGR1 = 0;
		ADCx->CFGR2 = ADC_CFGR2_CKMODE_0; // Synchronous PCLK/2.
		ADC_SET_BIT(ADCx->CR, ADC_CR_ADVREGEN);
		System::Delay_ms(1);             // Regulator startup.
		ADC_SET_BIT(ADCx->CR, ADC_CR_ADCAL);
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
		ADC_MODIFY_REG(ADC->CCR, ADC_CCR_ADCPRE, ADC_CCR_ADCPRE_0);
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
		ADC_CLEAR_BIT(*info->clk_reg, info->clk_bit);
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
		ADC_CLEAR_BIT(*info->clk_reg, info->clk_bit);
		if (enabled) NVIC_EnableIRQ(info->irqn);
		return SysInitStatus::InitError;
	}
	_ch_count = 0;
	_status = SysStatus::OK;
	_info = info;
	if (_dma.HasSelection() && SetUpDMA() != SysInitStatus::InitOK) {
		DeInit();
		if (enabled) NVIC_EnableIRQ(info->irqn);
		return SysInitStatus::InitError;
	}
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
	if (sel2) ADC_SET_BIT(ADCx->SMPR, sel_bit);
	else ADC_CLEAR_BIT(ADCx->SMPR, sel_bit);
	_ascending = _ascending && ascending;
	_last_ch = ch;
#else
	if (_ch_count >= 16) return SysStatus::Error;
	const uint32_t v = static_cast<uint32_t>(smpl);
	volatile uint32_t& smpr = ch < 10 ? ADCx->SMPR2 : ADCx->SMPR1;
	const uint32_t shift = (ch < 10 ? ch : ch - 10u) * 3u;
	if (present && ((smpr >> shift) & 7u) != v) return SysStatus::Error;
	ADC_MODIFY_REG(smpr, 7u << shift, v << shift);
	const uint8_t n = _ch_count;
	volatile uint32_t& sqr = n < 6 ? ADCx->SQR3 : n < 12 ? ADCx->SQR2 : ADCx->SQR1;
	const uint32_t rank_shift = (n % 6u) * 5u;
	ADC_MODIFY_REG(sqr, 0x1Fu << rank_shift, static_cast<uint32_t>(ch) << rank_shift);
	ADC_MODIFY_REG(ADCx->SQR1, ADC_SQR1_L, static_cast<uint32_t>(n) << ADC_SQR1_L_Pos);
	if (n) ADC_SET_BIT(ADCx->CR1, ADC_CR1_SCAN);
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
		ADC_SET_BIT(ADC->CCR, en);
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
	ADC_CLEAR_BIT(ADCx->SMPR, SMPSEL_ALL);
#else
	ADCx->SQR1 = 0;
	ADCx->SQR2 = 0;
	ADCx->SQR3 = 0;
	ADC_CLEAR_BIT(ADCx->CR1, ADC_CR1_SCAN);
#endif
	_ch_count = 0;
	if (ADCx == ADC1) ADC_CLEAR_BIT(ADC->CCR, (EN_VREFINT | EN_TEMP | EN_VBAT));
	ClearRunFlags(ADCx);
	_status = SysStatus::OK;
	return SysStatus::OK;
}

// Constructor-bound DMA is acquired once, with its own registry object.
SysInitStatus ADC_N::SetUpDMA()
{
	DMA_Sx::StreamSettings cfg;
	cfg.direction = DMA_Sx::DIR::From_Per;
	cfg.data_size = DMA_Sx::SIZE::Half_Word;
	cfg.minc = true;
	cfg.circ = IsRepeating();
	cfg.per_address = reinterpret_cast<uint32_t>(&ADCx->DR);
	if (_dma.SetUp(cfg) != SysInitStatus::InitOK) return SysInitStatus::InitError;

	const IRQn_Type irqn = _dma.GetIRQn();
	const bool enabled = NVIC_GetEnableIRQ(irqn) != 0;
	NVIC_DisableIRQ(irqn);
	if (IRQ_Registry::Register(irqn, &_dma_irq) != SysInitStatus::InitOK) {
		_dma.Release();
		if (enabled) NVIC_EnableIRQ(irqn);
		return SysInitStatus::InitError;
	}
	_dma_owned = true;
	_dma_tc_irq = true;
	_dma.Enable_IRQ(DMA_Sx::IRQ::TC);
	_dma.Enable_IRQ(DMA_Sx::IRQ::TE);
#if defined(STM32F4) || defined(STM32F7)
	_dma.Enable_IRQ(DMA_Sx::IRQ::DME);
#endif
	return SysInitStatus::InitOK;
}

bool ADC_N::SetRunMode(uint32_t bits)
{
#if defined(STM32G0)
	return WriteCfgr1(RUN_FIELDS, bits);
#else
	const bool enabled = (ADCx->CR2 & ADC_CR2_ADON) != 0u;
	ADC_MODIFY_REG(ADCx->CR2, RUN_FIELDS, bits | ADC_CR2_ADON);
	if (!enabled) System::Delay_ms(1);
	return true;
#endif
}

SysStatus ADC_N::Start(uint16_t* buf, uint32_t len)
{
	if (!_info) return SysStatus::NotInit;
	if (IsBusy()) return SysStatus::Busy;
	if (!_ch_count) return SysStatus::Error;
	if (_dma.HasSelection()) {
		if (!buf || (reinterpret_cast<uintptr_t>(buf) & 1u) || !len
			|| len > 0xFFFFu || len % _ch_count || (!IsRepeating() && len != _ch_count)
			|| (_dma_ht_irq && (len & 1u)))
			return SysStatus::Error;
		if (_dma.IsEnabled()) return SysStatus::Busy;
	} else if (buf || len) return SysStatus::Error;
	_status = SysStatus::OK; // A previously polled single capture has already ended.
	ClearRunFlags(ADCx); // Clear an earlier EOC before enabling ADC DMA requests.
#if defined(STM32G0)
	uint32_t bits = _dma.HasSelection() ? ADC_CFGR1_DMAEN : 0u;
	if (_dma.HasSelection() && IsRepeating()) bits |= ADC_CFGR1_DMACFG;
	if (_conversion == Conversion::Continuous) {
		bits |= ADC_CFGR1_CONT;
		if (!_dma.HasSelection()) bits |= ADC_CFGR1_OVRMOD;
	}
	if (_trigger != Trigger::None)
		bits |= ADC_CFGR1_EXTEN_0 | (static_cast<uint32_t>(_trigger) << ADC_CFGR1_EXTSEL_Pos);
#else
	uint32_t bits = _dma.HasSelection() ? ADC_CR2_DMA : 0u;
	if (_dma.HasSelection() && IsRepeating()) bits |= ADC_CR2_DDS;
	if (_conversion == Conversion::Continuous) bits |= ADC_CR2_CONT;
	if (_trigger != Trigger::None)
		bits |= static_cast<uint32_t>(_trigger) << ADC_CR2_EXTSEL_Pos;
#endif
	if (!SetRunMode(bits)) {
		_status = SysStatus::Timeout;
		return SysStatus::Timeout;
	}
	_errors = 0;
	if (_dma.HasSelection()) {
		if (_dma.SetMemAddr(reinterpret_cast<uint32_t>(buf)) != SysStatus::OK
			|| _dma.SetCount(len) != SysStatus::OK) return SysStatus::Error;
		_dma.ClearFlags();
		System::CacheCleanInvalidate(buf, len * sizeof(uint16_t));
		_buf = buf;
		_buf_bytes = len * sizeof(uint16_t);
		if (_dma_tc_irq || !IsRepeating()) {
			_dma.Enable_IRQ(DMA_Sx::IRQ::TC, false);
		} else _dma.Disable_IRQ(DMA_Sx::IRQ::TC);
		if (_dma_ht_irq) _dma.Enable_IRQ(DMA_Sx::IRQ::HTC, false);
		else _dma.Disable_IRQ(DMA_Sx::IRQ::HTC);
		_dma.Enable_IRQ(DMA_Sx::IRQ::TE, false);
#if defined(STM32F4) || defined(STM32F7)
		_dma.Enable_IRQ(DMA_Sx::IRQ::DME, false);
#endif
	}
	_status = SysStatus::Busy;
	if (_dma.HasSelection()) _dma.Stream_EN(ENABLE);
#if defined(STM32G0)
	ADC_SET_BIT(ADCx->CR, ADC_CR_ADSTART);
#else
	if (_trigger != Trigger::None) ADC_SET_BIT(ADCx->CR2, ADC_CR2_EXTEN_0);
	else ADC_SET_BIT(ADCx->CR2, ADC_CR2_SWSTART);
#endif
	if (_timer) _timer->Start(); // Timer last, after ADC is armed.
	return SysStatus::OK;
}

bool ADC_N::IsBusy() const
{
	if (!_info || _status != SysStatus::Busy) return false;
	if (_dma.HasSelection() || IsRepeating()) return true;
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
	if (!_dma.HasSelection() && !IsRepeating() && _status == SysStatus::Busy) {
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
	if (!_dma.HasSelection() || !_buf) return 0;
	// Main owns this stop. Preserve completion/error flags until they are read;
	// no pending DMA callback may consume the software-stop TC in the meantime.
	_dma.Disable_IRQ(DMA_Sx::IRQ::HTC);
	_dma.Disable_IRQ(DMA_Sx::IRQ::TC);
	_dma.Disable_IRQ(DMA_Sx::IRQ::TE);
#if defined(STM32F4) || defined(STM32F7)
	_dma.Disable_IRQ(DMA_Sx::IRQ::DME);
#endif
	_dma.Stream_EN(DISABLE);
	uint32_t flags = _dma.GetIRQFlags();
#if defined(STM32F4) || defined(STM32F7)
	// Main only: EN can clear before the FIFO finishes; TC confirms software
	// stop. Hardware TE terminates without requiring a normal TC event.
	while (!(flags & (DMA_Sx::FLAG_TC | DMA_LISR_TEIF0))) flags = _dma.GetIRQFlags();
#endif
	const uint32_t errors = flags & DMA_Sx::FLAG_ERRORS;
	_dma.ClearFlags();
	if (!errors && !_errors) System::CacheInvalidate(_buf, _buf_bytes);
	_buf = nullptr;
	_buf_bytes = 0;
	return errors;
}

SysStatus ADC_N::Stop()
{
	if (!_info) return SysStatus::NotInit;
	_status = SysStatus::NotInit; // Suppress notifications while stopping in main.
	if (_timer) _timer->Stop();
#if defined(STM32G0)
	const uint32_t cr = ADCx->CR;
	if ((cr & ADC_CR_ADSTART) && !(cr & ADC_CR_ADSTP))
		ADC_SET_BIT(ADCx->CR, ADC_CR_ADSTP);
#else
	ADC_CLEAR_BIT(ADCx->CR2, (ADC_CR2_ADON | RUN_FIELDS));
#endif
	const uint32_t errors = StopDMA() | _errors;
#if defined(STM32G0)
	if (!Disable()) {
		_status = SysStatus::Timeout;
		return SysStatus::Timeout;
	}
	ADC_CLEAR_BIT(ADCx->CFGR1, RUN_FIELDS);
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
	if (irq == IRQ::DMA_TC || irq == IRQ::DMA_HT) {
		if (!_dma_owned) return;
		if (irq == IRQ::DMA_HT && IsBusy()) return; // Fix half-buffer layout before Start.
		if (irq == IRQ::DMA_TC && !IsRepeating() && en == DISABLE) return;
		bool& enabled = irq == IRQ::DMA_HT ? _dma_ht_irq : _dma_tc_irq;
		enabled = en != DISABLE;
		const DMA_Sx::IRQ source = irq == IRQ::DMA_HT ? DMA_Sx::IRQ::HTC : DMA_Sx::IRQ::TC;
		if (en) _dma.Enable_IRQ(source, false);
		else _dma.Disable_IRQ(source);
		return;
	}
#if defined(STM32G0)
	if (en) ADC_SET_BIT(ADCx->IER, static_cast<uint32_t>(irq));
	else ADC_CLEAR_BIT(ADCx->IER, static_cast<uint32_t>(irq));
#else
	if (en) ADC_SET_BIT(ADCx->CR1, static_cast<uint32_t>(irq));
	else ADC_CLEAR_BIT(ADCx->CR1, static_cast<uint32_t>(irq));
#endif
}

// Shared error action, not a dispatcher: no DMA/ADC flag polling or wait loops.
void ADC_N::StopOnError(uint32_t errors)
{
	_errors = _errors | errors;
	if (_timer) _timer->Stop();
#if defined(STM32G0)
	const uint32_t cr = ADCx->CR;
	if ((cr & ADC_CR_ADSTART) && !(cr & ADC_CR_ADSTP))
		ADC_SET_BIT(ADCx->CR, ADC_CR_ADSTP);
#else
	ADC_CLEAR_BIT(ADCx->CR2, (ADC_CR2_ADON | RUN_FIELDS));
#endif
	if (_buf) {
		_dma.Disable_IRQ(DMA_Sx::IRQ::HTC);
		_dma.Disable_IRQ(DMA_Sx::IRQ::TE);
#if defined(STM32F4) || defined(STM32F7)
		_dma.Disable_IRQ(DMA_Sx::IRQ::DME);
		_dma.Enable_IRQ(DMA_Sx::IRQ::TC, false); // Also needed if circular TC was off.
		_dma.RequestStop();
		// Ensure the DMA handler observes a stop that was already complete.
		NVIC_SetPendingIRQ(_dma.GetIRQn());
		return;
#else
		_dma.RequestStop(); // Error packet is discarded; no CNDTR/buffer read.
		_dma.ClearFlags();
#endif
	}
	_buf = nullptr;
	_buf_bytes = 0;
	__DMB();
	_status = SysStatus::Error;
	OnError(_errors);
}

// ADC flags only. DMA completion and errors have a distinct registered handler.
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
	if (_status != SysStatus::Busy && !(_dma.HasSelection() && _status == SysStatus::OK)) return;
	if (_errors) return; // Await the separate DMA error-stop completion.
	if (overrun && (_dma.HasSelection() || _conversion != Conversion::Continuous)) {
		StopOnError(static_cast<uint32_t>(Error::Overrun));
		return;
	}
	if (!_dma.HasSelection() && !IsRepeating()) {
#if defined(STM32G0)
		if (!(ADCx->CR & ADC_CR_ADSTART)) _status = SysStatus::OK;
#else
		if (sr & ADC_SR_EOC) _status = SysStatus::OK;
#endif
	}
#if defined(STM32G0)
	if (eoc) OnEOC();
#endif
	if (eos) OnEOS();
}

void ADC_N::HandleDmaIRQ()
{
	if (!_info || !_dma_owned) return;
	if (_status != SysStatus::Busy || !_buf) {
		// Stop/DeInit disabled the sources and still need their final flags.
		if (_status != SysStatus::NotInit) _dma.ClearFlags();
		return;
	}
	const uint32_t flags = _dma.GetIRQFlags();
	const uint32_t dma_errors = flags & DMA_Sx::FLAG_ERRORS;
	if (dma_errors && !(_errors & static_cast<uint32_t>(Error::Dma)))
		StopOnError(static_cast<uint32_t>(Error::Dma));
	if (_errors) {
#if defined(STM32F4) || defined(STM32F7)
		// No spin: a later TC IRQ confirms software stop. Hardware TE can stop
		// without TC. Checking EN also rejects a stale circular TC during abort.
		if (_dma.IsEnabled()) return;
		if (!(flags & DMA_Sx::FLAG_TC) && !(dma_errors & DMA_LISR_TEIF0)) return;
		_dma.ClearFlags();
		_buf = nullptr;
		_buf_bytes = 0;
		__DMB();
		_status = SysStatus::Error;
		OnError(_errors);
#endif
		return;
	}
	const bool half = _dma_ht_irq && (flags & DMA_Sx::FLAG_HT);
	const bool complete = _dma_tc_irq && (flags & DMA_Sx::FLAG_TC);
	__DMB(); // Observe completed DMA writes before accessing/notifying buffer data.
	if (half) {
		_dma.ClearHT_Flag();
		System::CacheInvalidate(_buf, _buf_bytes / 2u);
		OnDmaHalf();
	}
	if (!complete) return;
	_dma.ClearTC_Flag();
	if (!IsRepeating()) {
		_dma.RequestStop(); // TC already confirms finite completion; no EN wait.
		System::CacheInvalidate(_buf, _buf_bytes);
		_buf = nullptr;
		_buf_bytes = 0;
		__DMB();
		_status = SysStatus::OK;
	} else if (_dma_ht_irq) {
		System::CacheInvalidate(_buf + _buf_bytes / (2u * sizeof(uint16_t)), _buf_bytes / 2u);
	} else System::CacheInvalidate(_buf, _buf_bytes);
	OnDmaComplete();
}

void ADC_N::DeInit()
{
	if (!_info) return;
	_status = SysStatus::NotInit;
	if (_timer) _timer->Stop();
#if defined(STM32G0)
	ADCx->IER = 0;
	const uint32_t cr = ADCx->CR;
	if ((cr & ADC_CR_ADSTART) && !(cr & ADC_CR_ADSTP))
		ADC_SET_BIT(ADCx->CR, ADC_CR_ADSTP);
#else
	ADCx->CR1 = 0;
	ADCx->CR2 = 0;
#endif
	StopDMA();
	const IRQn_Type none = static_cast<IRQn_Type>(-1);
	const IRQn_Type irqs[2] = { _info->irqn, _dma_owned ? _dma.GetIRQn() : none };
	IIRQHandler* const handlers[2] = { this, &_dma_irq };
	for (unsigned i = 0; i < 2; ++i) {
		if (irqs[i] == none) continue;
		const bool enabled = NVIC_GetEnableIRQ(irqs[i]) != 0;
		NVIC_DisableIRQ(irqs[i]);
		IRQ_Registry::Unregister(irqs[i], handlers[i]);
		if (IRQ_Registry::HasHandlers(irqs[i])) {
			if (enabled) NVIC_EnableIRQ(irqs[i]);
		} else NVIC_ClearPendingIRQ(irqs[i]);
	}
	if (_dma_owned) _dma.Release();
#if defined(STM32G0)
	// G0 has one ADC: its reset also releases the regulator/common configuration.
	ADC_SET_BIT(RCC->APBRSTR2, RCC_APBRSTR2_ADCRST);
	ADC_CLEAR_BIT(RCC->APBRSTR2, RCC_APBRSTR2_ADCRST);
#else
	ADCx->SQR1 = 0;
	ADCx->SQR2 = 0;
	ADCx->SQR3 = 0;
	ADCx->SMPR1 = 0;
	ADCx->SMPR2 = 0;
	_last_data = 0;
	if (ADCx == ADC1) ADC_CLEAR_BIT(ADC->CCR, (EN_VREFINT | EN_TEMP | EN_VBAT));
#endif
	ADC_CLEAR_BIT(*_info->clk_reg, _info->clk_bit);
	_info = nullptr;
	_dma_owned = false;
	_dma_ht_irq = false;
	_dma_tc_irq = false;
	_errors = 0;
	_ch_count = 0;
}
