#include "adc.hpp"

// ---------------------------------------------------------------------------
// Peripheral table — defined in adc_defs.hpp Section B
// ---------------------------------------------------------------------------

#define ADC_DEFS_CPP
#include "adc_defs.hpp"
#undef  ADC_DEFS_CPP

// Blocking configuration/start/stop needs SysTick interrupts to run.
static bool adc_thread_context()
{
	return !__get_IPSR() && !__get_PRIMASK();
}

#if defined(STM32G0)

// ---------------------------------------------------------------------------
// G0-only helpers (enable/disable handshakes, CHSELR, sequencer mode)
// ---------------------------------------------------------------------------

static constexpr uint32_t CHSELR_EMPTY = 0xFFFFFFFFu;   // sequencer mode: SQ1 = 0xF, no channel
static constexpr uint32_t SMPSEL_ALL   = 0x7FFFFu << ADC_SMPR_SMPSEL0_Pos;
// CFGR1 fields set by the StartXxx() functions, cleared by Stop()
static constexpr uint32_t CFGR1_RUN    = ADC_CFGR1_EXTEN | ADC_CFGR1_EXTSEL | ADC_CFGR1_DMAEN
                                       | ADC_CFGR1_DMACFG | ADC_CFGR1_CONT | ADC_CFGR1_OVRMOD;

static constexpr uint32_t ADC_TIMEOUT_MS = 1u;

static bool wait_bits(volatile uint32_t& reg, uint32_t mask, bool set)
{
	const uint32_t start = System::GetTick();
	while (((reg & mask) != 0u) != set)
		if (System::GetTick() - start > ADC_TIMEOUT_MS) return false;
	return true;
}

bool ADC_N::Enable()
{
	ADCx->ISR = ADC_ISR_ADRDY;
	const uint32_t start = System::GetTick();
	while (!(ADCx->ISR & ADC_ISR_ADRDY)) {
		if (!(ADCx->CR & ADC_CR_ADEN)) ADCx->CR |= ADC_CR_ADEN;
		if (System::GetTick() - start > ADC_TIMEOUT_MS) return false;
	}
	return true;
}

bool ADC_N::Disable()
{
	if (ADCx->CR & ADC_CR_ADSTART) {
		if (!(ADCx->CR & ADC_CR_ADSTP)) ADCx->CR |= ADC_CR_ADSTP;
	}
	if (!wait_bits(ADCx->CR, ADC_CR_ADSTP, false)) return false;
	if (ADCx->CR & ADC_CR_ADEN) {
		if (!(ADCx->CR & ADC_CR_ADDIS)) ADCx->CR |= ADC_CR_ADDIS;
	}
	return wait_bits(ADCx->CR, ADC_CR_ADEN | ADC_CR_ADDIS, false);
}

bool ADC_N::WriteChselr(uint32_t chsel)
{
	ADCx->ISR    = ADC_ISR_CCRDY;
	ADCx->CHSELR = chsel;
	if (!wait_bits(ADCx->ISR, ADC_ISR_CCRDY, true)) return false;
	ADCx->ISR    = ADC_ISR_CCRDY;   // clear by writing 1
	return true;
}

bool ADC_N::SetSeqMode(bool seq)
{
	// Sequencer -> bitmask: same channels as bits (the caller checked they
	// are ascending). Bitmask -> sequencer: only used to empty the sequence.
	uint32_t chsel = CHSELR_EMPTY;
	if (!seq) {
		const uint32_t old = ADCx->CHSELR;
		chsel = 0;
		for (uint8_t i = 0; i < _ch_count; ++i)
			chsel |= 1u << ((old >> (i * 4u)) & 0xFu);
	}

	if (!WriteCfgr1(ADC_CFGR1_CHSELRMOD, seq ? ADC_CFGR1_CHSELRMOD : 0u)) return false;
	_seq_mode = seq;
	return WriteChselr(chsel);
}

bool ADC_N::WriteCfgr1(uint32_t clear, uint32_t set)
{
	if (!Disable()) return false;   // CFGR1 must not be written with ADEN = 1
	ADCx->CFGR1 = (ADCx->CFGR1 & ~clear) | set;
	return Enable();
}

#endif

void ADC_N::CheckSingleDone(uint32_t sr)
{
	if (_mode != Mode::Single) return;
#if defined(STM32G0)
	(void)sr;
	if (!(ADCx->CR & ADC_CR_ADSTART)) _mode = Mode::Idle;   // cleared by hardware at end of sequence
#elif defined(STM32F4) || defined(STM32F7)
	if (sr & ADC_SR_EOC) _mode = Mode::Idle;                // EOCS = 0: EOC = end of sequence
#endif
}

// Stale end/overrun flags of an earlier run must not end or disturb a new
// one (ST's driver clears them on start too)
static inline void clear_run_flags(ADC_TypeDef* adc)
{
#if defined(STM32G0)
	adc->ISR = ADC_ISR_EOC | ADC_ISR_EOS | ADC_ISR_OVR;
#elif defined(STM32F4) || defined(STM32F7)
	adc->SR  = ~(ADC_SR_EOC | ADC_SR_OVR);   // rc_w0
#endif
}

uint16_t ADC_N::GetData()
{
#if defined(STM32F4) || defined(STM32F7)
	// Reading DR clears EOC, the only end-of-run mark of Start(). While the
	// run is not over DR is not read at all — an EOC arriving in between
	// would be lost — and the last value read is returned: it is the same
	// one DR holds until the new result arrives.
	IrqLock lock;
	if (_mode == Mode::Single) {
		const uint32_t sr = ADCx->SR;
		if (!(sr & ADC_SR_EOC)) return _last_data;
		CheckSingleDone(sr);
	}
	_last_data = static_cast<uint16_t>(ADCx->DR);
	return _last_data;
#else
	return static_cast<uint16_t>(ADCx->DR);
#endif
}

// ---------------------------------------------------------------------------
// ADC_N::InSequence
// ---------------------------------------------------------------------------

bool ADC_N::InSequence(uint8_t ch) const
{
#if defined(STM32G0)
	const uint32_t chsel = ADCx->CHSELR;
	if (!_seq_mode) return (chsel >> ch) & 1u;
	for (uint8_t i = 0; i < _ch_count; ++i)
		if (((chsel >> (i * 4u)) & 0xFu) == ch) return true;
#elif defined(STM32F4) || defined(STM32F7)
	for (uint8_t i = 0; i < _ch_count; ++i) {
		const uint32_t sqr = (i < 6) ? ADCx->SQR3 : (i < 12) ? ADCx->SQR2 : ADCx->SQR1;
		if (((sqr >> ((i % 6u) * 5u)) & 0x1Fu) == ch) return true;
	}
#endif
	return false;
}

// ---------------------------------------------------------------------------
// Synchronisation model
//
// Main-context operations reserve Mode::Config under a short lock, then
// wait/configure with IRQs enabled. IRQs request a stop without waiting;
// FinishStop() confirms it before the next configuration/start or in Stop().
// Events of a cancelled run are cleared without callbacks. Callbacks run
// outside the lock and notify the main loop; control operations run there.
// ---------------------------------------------------------------------------

SysStatus ADC_N::BeginConfig()
{
	if (!adc_thread_context()) return SysStatus::Error;
	{
		IrqLock lock;
		if (_mode == Mode::NotInit) return SysStatus::NotInit;
		if (RunActive()) return SysStatus::Busy;
		_mode = Mode::Config;
		_cancelled = true;
	}
	if (!FinishStop()) {
		_mode = Mode::NotInit;
		return SysStatus::Timeout;
	}
	return SysStatus::OK;
}

// ---------------------------------------------------------------------------
// ADC_N::SetUp — basic set-up, empty sequence
// ---------------------------------------------------------------------------

SysInitStatus ADC_N::SetUp(SMPL smp1, SMPL smp2)
{
	if (!adc_thread_context()) return SysInitStatus::InitError;
	for (const auto& e : adc_table)
		if (e.periph == ADCx) { _info = &e; break; }
	if (!_info) return SysInitStatus::InitError;
	*_info->clk_reg |= _info->clk_bit;

	{
		// A running acquisition is stopped; another configuration is not
		// interrupted. Not on the first call: G0 regulator still off.
		IrqLock lock;
		if (_mode == Mode::Config) return SysInitStatus::InitError;
		if (_mode != Mode::NotInit && _mode != Mode::Idle) RequestStop();
		_mode = Mode::Config;
		_cancelled = true;
	}
	_ch_count = 0;   // nothing can start until the sequence is rebuilt

	// Internal inputs off (ADC1 owns them on F4/F7; the only ADC on G0)
	if (ADCx == ADC1)
		ADC->CCR &= ~(EN_VREFINT | EN_TEMP | EN_VBAT);

#if defined(STM32G0)
	// Disabled for calibration (ADEN = 0, DMAEN = 0)
	if (!wait_bits(ADCx->CR, ADC_CR_ADCAL, false) || !Disable()) {
		_mode = Mode::NotInit;
		return SysInitStatus::InitError;
	}
	ADCx->CFGR1 = 0;

	// Clock: synchronous PCLK/2. The asynchronous default (CKMODE = 00)
	// runs from SYSCLK, which exceeds f_ADC max (35 MHz) at 64 MHz.
	ADCx->CFGR2 = ADC_CFGR2_CKMODE_0;

	// Voltage regulator, then calibration
	ADCx->CR |= ADC_CR_ADVREGEN;
	System::Delay_ms(1);   // t_ADCVREG_STUP ≤ 20 µs; 1 ms is safe
	ADCx->CR |= ADC_CR_ADCAL;
	if (!wait_bits(ADCx->CR, ADC_CR_ADCAL, false)) {
		_mode = Mode::NotInit;
		return SysInitStatus::InitError;
	}

	// The two sampling times; channels pick one with SMPSELx (AddChannel)
	ADCx->SMPR = (static_cast<uint32_t>(smp1) << ADC_SMPR_SMP1_Pos)
	           | (static_cast<uint32_t>(smp2) << ADC_SMPR_SMP2_Pos);

	// Fully configurable sequencer (CHSELRMOD changes only with ADEN = 0);
	// software trigger, DMA off. Enable, empty sequence.
	ADCx->CFGR1 = ADC_CFGR1_CHSELRMOD;
	_seq_mode  = true;
	if (!Enable() || !WriteChselr(CHSELR_EMPTY)) {
		_mode = Mode::NotInit;
		return SysInitStatus::InitError;
	}
	_ascending = true;
	_last_ch   = 0;

#elif defined(STM32F4) || defined(STM32F7)
	(void)smp1;   // sampling time is per channel (AddChannel)
	(void)smp2;

	// Power down during configuration
	ADCx->CR2 = 0;

	// Clock: PCLK2/4 (ADCPRE = 01). The default /2 gives 42 MHz at
	// PCLK2 = 84 MHz, above f_ADC max (36 MHz). ADC->CCR is common to all
	// ADCs, so it is only touched when it differs.
	if ((ADC->CCR & ADC_CCR_ADCPRE) != ADC_CCR_ADCPRE_0)
		ADC->CCR = (ADC->CCR & ~ADC_CCR_ADCPRE) | ADC_CCR_ADCPRE_0;

	// Empty sequence, sampling times reset; keep interrupt enables
	ADCx->SMPR1 = 0;
	ADCx->SMPR2 = 0;
	ADCx->SQR1  = 0;
	ADCx->SQR2  = 0;
	ADCx->SQR3  = 0;
	ADCx->CR1   = ADCx->CR1 & (ADC_CR1_EOCIE | ADC_CR1_OVRIE);

	// Power on; software trigger, DMA off
	ADCx->CR2 = ADC_CR2_ADON;
	System::Delay_ms(1);   // t_STAB ≤ 3 µs
#endif

	_mode = Mode::Idle;
	return SysInitStatus::InitOK;
}

// ---------------------------------------------------------------------------
// ADC_N::AddRank — next rank + sampling setting, straight into the registers
// (helper: the caller holds Mode::Config)
// ---------------------------------------------------------------------------

bool ADC_N::AddRank(uint8_t ch, CH_SMPL smpl)
{
	// The sampling setting belongs to the channel number, not to the rank
	const bool present = InSequence(ch);

#if defined(STM32G0)
	const uint32_t sel_bit = 1u << (ADC_SMPR_SMPSEL0_Pos + ch);
	const bool     sel2    = (smpl == CH_SMPL::SMP2);
	if (present && ((ADCx->SMPR & sel_bit) != 0u) != sel2)
		System::DebugTrap("ADC_N: same channel added with a different CH_SMPL");

	// Sequencer: 8 ranks of 4 bits (channels 0-14). Beyond that only the
	// bitmask sequencer, which converts in ascending channel order.
	const bool next_ascending = (_ch_count == 0) || (ch > _last_ch);
	if (!_seq_mode || ch > 14 || _ch_count >= 8) {
		if (!_ascending || !next_ascending)
			System::DebugTrap("ADC_N: G0 bitmask sequencer (channel 15-18 or > 8 channels) needs ascending channels");
		if (_seq_mode && !SetSeqMode(false)) return false;
		if (!WriteChselr(ADCx->CHSELR | (1u << ch))) return false;
	} else {
		const uint32_t shift = _ch_count * 4u;
		if (!WriteChselr((ADCx->CHSELR & ~(0xFu << shift)) | (static_cast<uint32_t>(ch) << shift)))
			return false;
	}

	if (sel2) ADCx->SMPR |=  sel_bit;
	else      ADCx->SMPR &= ~sel_bit;
	_ascending = _ascending && next_ascending;
	_last_ch   = ch;

#elif defined(STM32F4) || defined(STM32F7)
	if (_ch_count >= 16) System::DebugTrap("ADC_N: sequence full (16 ranks)");

	// Sampling time: 3 bits per channel number (SMPR2: CH0-9, SMPR1: CH10-18)
	const uint32_t     v     = static_cast<uint32_t>(smpl);
	volatile uint32_t& smpr  = (ch < 10) ? ADCx->SMPR2 : ADCx->SMPR1;
	const uint32_t     shift = ((ch < 10) ? ch : (ch - 10u)) * 3u;
	if (present && ((smpr >> shift) & 7u) != v)
		System::DebugTrap("ADC_N: same channel added with a different CH_SMPL");
	smpr = (smpr & ~(7u << shift)) | (v << shift);

	// Rank n = _ch_count: SQ field, sequence length L = n, SCAN from 2 ranks
	const uint8_t n = _ch_count;
	volatile uint32_t& sqr = (n < 6) ? ADCx->SQR3 : (n < 12) ? ADCx->SQR2 : ADCx->SQR1;
	sqr |= static_cast<uint32_t>(ch) << ((n % 6u) * 5u);
	ADCx->SQR1 = (ADCx->SQR1 & ~ADC_SQR1_L) | (static_cast<uint32_t>(n) << ADC_SQR1_L_Pos);
	if (n >= 1) ADCx->CR1 |= ADC_CR1_SCAN;
#endif

	++_ch_count;
	return true;
}

// ---------------------------------------------------------------------------
// ADC_N::AddChannel — pin / internal input
// ---------------------------------------------------------------------------

SysStatus ADC_N::AddChannel(ADC_PIN pin, CH_SMPL smpl)
{
	if (!pin.IsValid()) return SysStatus::Error;
	if (!_info) System::DebugTrap("ADC_N: AddChannel() before SetUp()");
	if (pin.adc_base != reinterpret_cast<uint32_t>(ADCx))
		System::DebugTrap("ADC_N: pin belongs to wrong ADC peripheral");
	const SysStatus status = BeginConfig();
	if (status == SysStatus::Busy) System::DebugTrap("ADC_N: AddChannel() while running");
	if (status != SysStatus::OK) return status;

	PIN(reinterpret_cast<GPIO_TypeDef*>(pin.port), pin.pin).SetUp(PIN::TYPE::ANALOG);
	const bool ok = AddRank(pin.channel, smpl);
	_mode = ok ? Mode::Idle : Mode::NotInit;
	return ok ? SysStatus::OK : SysStatus::Timeout;
}

SysStatus ADC_N::AddChannel(INTERNAL in, CH_SMPL smpl)
{
	if (!_info)       System::DebugTrap("ADC_N: AddChannel() before SetUp()");
	if (ADCx != ADC1) System::DebugTrap("ADC_N: internal inputs exist on ADC1 only");
	const SysStatus status = BeginConfig();
	if (status == SysStatus::Busy) System::DebugTrap("ADC_N: AddChannel() while running");
	if (status != SysStatus::OK) return status;

	uint8_t  ch = 0;
	uint32_t en = 0;
	switch (in) {
		case INTERNAL::VREFINT: ch = CH_VREFINT; en = EN_VREFINT; break;
		case INTERNAL::TEMP:    ch = CH_TEMP;    en = EN_TEMP;    break;
		case INTERNAL::VBAT:    ch = CH_VBAT;    en = EN_VBAT;    break;
	}

	// Where TEMP and VBAT share a channel (F4/F7 from F411 on, all F7), the
	// VBAT enable takes the channel over. That channel in the sequence
	// without the VBAT enable means it is the temperature sensor.
	if (CH_TEMP == CH_VBAT) {
		const bool vbat_on = (ADC->CCR & EN_VBAT) != 0u;
		if ((in == INTERNAL::TEMP && vbat_on) ||
		    (in == INTERNAL::VBAT && InSequence(CH_VBAT) && !vbat_on))
			System::DebugTrap("ADC_N: TEMP and VBAT share one channel on this device");
	}

	// Source enable, start-up delay (interrupts on: SysTick) and the rank are
	// one operation under Mode::Config — no ClearChannels() can come between
	if (!(ADC->CCR & en)) {
		ADC->CCR |= en;
		System::Delay_ms(1);   // start-up of the sensor / reference: tens of µs
	}
	const bool ok = AddRank(ch, smpl);
	_mode = ok ? Mode::Idle : Mode::NotInit;
	return ok ? SysStatus::OK : SysStatus::Timeout;
}

// ---------------------------------------------------------------------------
// ADC_N::ClearChannels
// ---------------------------------------------------------------------------

SysStatus ADC_N::ClearChannels()
{
	if (!_info) return SysStatus::NotInit;
	const SysStatus status = BeginConfig();
	if (status == SysStatus::Busy) System::DebugTrap("ADC_N: ClearChannels() while running");
	if (status != SysStatus::OK) return status;

	bool ok = true;
	_ch_count = 0;
#if defined(STM32G0)
	ok = _seq_mode ? WriteChselr(CHSELR_EMPTY) : SetSeqMode(true);
	ADCx->SMPR &= ~SMPSEL_ALL;
	_ascending = true;
	_last_ch   = 0;
#elif defined(STM32F4) || defined(STM32F7)
	ADCx->SQR1 = 0;
	ADCx->SQR2 = 0;
	ADCx->SQR3 = 0;
	ADCx->CR1 &= ~ADC_CR1_SCAN;
#endif

	if (ADCx == ADC1)
		ADC->CCR &= ~(EN_VREFINT | EN_TEMP | EN_VBAT);
	_mode = ok ? Mode::Idle : Mode::NotInit;
	return ok ? SysStatus::OK : SysStatus::Timeout;
}

// ---------------------------------------------------------------------------
// ADC_N::AttachDMA
// ---------------------------------------------------------------------------

SysInitStatus ADC_N::AttachDMA(DMA_Sx* dma)
{
	if (!_info || !dma)  return SysInitStatus::InitError;
	if (BeginConfig() != SysStatus::OK) return SysInitStatus::InitError;

	SysInitStatus s = SysInitStatus::InitOK;
	if (dma != _dma) {
		DMA_Sx::StreamSettings cfg;
		cfg.direction   = DMA_Sx::DIR::From_Per;
		cfg.data_size   = DMA_Sx::SIZE::Half_Word;
		cfg.minc        = true;   // CIRC is set by StartDMA() / StartTrig()
		cfg.per_address = reinterpret_cast<uint32_t>(&ADCx->DR);
		s = dma->SetUp(cfg, _info->dma_req);
		if (s == SysInitStatus::InitOK) {
			// Replacing a stream: its DMA_TC interrupt moves to the new one,
			// the old stream is released (another driver may take it)
			DMA_Sx* old = _dma;
			const bool tc = _dma_registered;
			if (tc) IRQ_en(IRQ::DMA_TC, DISABLE);
			_dma = dma;   // no interrupt: IRQ_en(IRQ::DMA_TC, ENABLE) if wanted
			if (tc) IRQ_en(IRQ::DMA_TC, ENABLE);
			if (old) old->Release();
		}
	}
	_mode = Mode::Idle;
	return s;
}

// ---------------------------------------------------------------------------
// ADC_N::AttachTrig / AttachExtTrig
// ---------------------------------------------------------------------------

SysInitStatus ADC_N::AttachTrig(TIM_TriggerGenerator* trg)
{
	if (!trg || trg->GetReq().target != TIM_TriggerGenerator::TrigTarget::Adc)
		return SysInitStatus::InitError;
	if (BeginConfig() != SysStatus::OK) return SysInitStatus::InitError;

	_trg      = trg;
	_extsel   = trg->GetReq().extsel;
	_trig_set = true;
	_mode = Mode::Idle;
	return SysInitStatus::InitOK;
}

SysInitStatus ADC_N::AttachExtTrig()
{
	if (BeginConfig() != SysStatus::OK) return SysInitStatus::InitError;

	_trg      = nullptr;
	_extsel   = EXTSEL_EXTI11;
	_trig_set = true;
	_mode = Mode::Idle;
	return SysInitStatus::InitOK;
}

// ---------------------------------------------------------------------------
// ADC_N::Start / StartDMA / StartTrig / StartCont
//
// Same pattern everywhere: reserve (Mode::Config), set up DMA and the mode
// fields with interrupts enabled (G0: WriteCfgr1 disables/enables the ADC),
// then one lock for: stale flags cleared, final mode, hardware start — an
// interrupt between the mode and the start would see no conversion yet.
// ---------------------------------------------------------------------------

void ADC_N::Start()
{
	if (!_info || !_ch_count) return;

	if (BeginConfig() != SysStatus::OK) return;
	IrqLock lock;
	clear_run_flags(ADCx);  // F4/F7: a stale EOC would end the run at once
	_cancelled = false;
	_mode = Mode::Single;
#if defined(STM32G0)
	ADCx->CR  |= ADC_CR_ADSTART;
#elif defined(STM32F4) || defined(STM32F7)
	ADCx->CR2 |= ADC_CR2_SWSTART;
#endif
}

SysStatus ADC_N::StartDMA(uint16_t* buf)
{
	if (_mode == Mode::NotInit) return SysStatus::NotInit;
	if (!_dma || !buf || !_ch_count) return SysStatus::Error;
	const uint32_t bytes = _ch_count * 2u;
	if (!DMA_Sx::CacheRxOk(buf, bytes)) return SysStatus::Error;   // F7 D-cache: whole lines
	const SysStatus status = BeginConfig();
	if (status != SysStatus::OK) return status;
#if defined(STM32G0)
	if (!WriteCfgr1(CFGR1_RUN, ADC_CFGR1_DMAEN)) {
		_mode = Mode::NotInit;
		return SysStatus::Timeout;
	}
#elif defined(STM32F4) || defined(STM32F7)
	ADCx->CR2 |= ADC_CR2_DMA;
#endif
	SetDmaBuffer(buf, bytes);

	// One sequence, one DMA pass: normal DMA, one-shot ADC DMA requests
	// (DMACFG / DDS = 0: no request after the last transfer).
	_dma->CIRC(DISABLE);
	_dma->SetMemAddr(reinterpret_cast<uint32_t>(buf));
	_dma->SetCount(_ch_count);

	IrqLock lock;
	clear_run_flags(ADCx);
	_dma->ClearFlags();
	_cancelled = false;
	_mode = Mode::Dma;
	_dma->Stream_EN(ENABLE);
#if defined(STM32G0)
	ADCx->CR  |= ADC_CR_ADSTART;
#elif defined(STM32F4) || defined(STM32F7)
	ADCx->CR2 |= ADC_CR2_SWSTART;
#endif
	return SysStatus::OK;
}

SysStatus ADC_N::StartTrig(uint16_t* buf, uint32_t len)
{
	if (_mode == Mode::NotInit) return SysStatus::NotInit;
	if (!_dma || !_trig_set || !buf || !_ch_count)  return SysStatus::Error;
	if (!len || len % _ch_count || len > 0xFFFFu)    return SysStatus::Error;
	if (!DMA_Sx::CacheRxOk(buf, len * 2u))           return SysStatus::Error;   // F7 D-cache
	const SysStatus status = BeginConfig();
	if (status != SysStatus::OK) return status;
#if defined(STM32G0)
	if (!WriteCfgr1(CFGR1_RUN, ADC_CFGR1_DMAEN | ADC_CFGR1_DMACFG | ADC_CFGR1_EXTEN_0
	                         | (static_cast<uint32_t>(_extsel) << ADC_CFGR1_EXTSEL_Pos))) {
		_mode = Mode::NotInit;
		return SysStatus::Timeout;
	}
#elif defined(STM32F4) || defined(STM32F7)
	ADCx->CR2 = (ADCx->CR2 & ~(ADC_CR2_EXTSEL | ADC_CR2_EXTEN))
	          | ADC_CR2_DMA | ADC_CR2_DDS
	          | (static_cast<uint32_t>(_extsel) << ADC_CR2_EXTSEL_Pos);
#endif
	SetDmaBuffer(buf, len * 2u);

	// Circular DMA over buf, circular ADC DMA requests (DMACFG / DDS = 1),
	// trigger source selected, rising edge.
	_dma->CIRC(ENABLE);
	_dma->SetMemAddr(reinterpret_cast<uint32_t>(buf));
	_dma->SetCount(len);

	IrqLock lock;
	clear_run_flags(ADCx);
	_dma->ClearFlags();
	_cancelled = false;
	_mode = Mode::Trig;
	_dma->Stream_EN(ENABLE);
#if defined(STM32G0)
	ADCx->CR  |= ADC_CR_ADSTART;     // arms: conversions start on the trigger
#elif defined(STM32F4) || defined(STM32F7)
	ADCx->CR2 |= ADC_CR2_EXTEN_0;    // arms: rising edge
#endif
	// Trigger timer last, so its first event finds the ADC armed
	if (_trg) _trg->Start();
	return SysStatus::OK;
}

SysStatus ADC_N::StartCont(uint16_t* buf, uint32_t len)
{
	if (_mode == Mode::NotInit) return SysStatus::NotInit;
	if (!_dma || !buf || !_ch_count)              return SysStatus::Error;
	if (!len || len % _ch_count || len > 0xFFFFu) return SysStatus::Error;
	if (!DMA_Sx::CacheRxOk(buf, len * 2u))        return SysStatus::Error;   // F7 D-cache
	const SysStatus status = BeginConfig();
	if (status != SysStatus::OK) return status;
#if defined(STM32G0)
	if (!WriteCfgr1(CFGR1_RUN, ADC_CFGR1_DMAEN | ADC_CFGR1_DMACFG | ADC_CFGR1_CONT)) {
		_mode = Mode::NotInit;
		return SysStatus::Timeout;
	}
#elif defined(STM32F4) || defined(STM32F7)
	ADCx->CR2 |= ADC_CR2_DMA | ADC_CR2_DDS | ADC_CR2_CONT;
#endif
	SetDmaBuffer(buf, len * 2u);

	// Circular DMA over buf, circular ADC DMA requests, continuous mode,
	// software start.
	_dma->CIRC(ENABLE);
	_dma->SetMemAddr(reinterpret_cast<uint32_t>(buf));
	_dma->SetCount(len);

	IrqLock lock;
	clear_run_flags(ADCx);
	_dma->ClearFlags();
	_cancelled = false;
	_mode = Mode::ContDma;
	_dma->Stream_EN(ENABLE);
#if defined(STM32G0)
	ADCx->CR  |= ADC_CR_ADSTART;
#elif defined(STM32F4) || defined(STM32F7)
	ADCx->CR2 |= ADC_CR2_SWSTART;
#endif
	return SysStatus::OK;
}

SysStatus ADC_N::StartCont()
{
	if (_mode == Mode::NotInit) return SysStatus::NotInit;
	if (!_info || _ch_count != 1) return SysStatus::Error;
	const SysStatus status = BeginConfig();
	if (status != SysStatus::OK) return status;

	// Continuous mode, no DMA; unread results are overwritten
#if defined(STM32G0)
	if (!WriteCfgr1(CFGR1_RUN, ADC_CFGR1_CONT | ADC_CFGR1_OVRMOD)) {
		_mode = Mode::NotInit;
		return SysStatus::Timeout;
	}
#elif defined(STM32F4) || defined(STM32F7)
	ADCx->CR2 |= ADC_CR2_CONT;     // EOCS = 0, DMA = 0: no overrun detection
#endif

	IrqLock lock;
	clear_run_flags(ADCx);
	_cancelled = false;
	_mode = Mode::Cont;
#if defined(STM32G0)
	ADCx->CR  |= ADC_CR_ADSTART;
#elif defined(STM32F4) || defined(STM32F7)
	ADCx->CR2 |= ADC_CR2_SWSTART;
#endif
	return SysStatus::OK;
}

// ---------------------------------------------------------------------------
// ADC_N::IsBusy / Stop and their lock-free helpers
// ---------------------------------------------------------------------------

void ADC_N::SetDmaBuffer(uint16_t* buf, uint32_t bytes)
{
	_buf       = buf;
	_buf_bytes = bytes;
	_dma_error = false;
	DMA_Sx::CacheInvalidate(buf, bytes);   // F7: no dirty line written over the DMA data later
}

bool ADC_N::RunActive()
{
	if (_mode == Mode::NotInit) return false;
#if defined(STM32G0)
	CheckSingleDone(0u);
#elif defined(STM32F4) || defined(STM32F7)
	CheckSingleDone(ADCx->SR);   // reading SR clears nothing
#endif
	// StartDMA() ends by itself once the DMA transfer is complete. With
	// IRQ::DMA_TC enabled the interrupt finishes it (and calls the callback);
	// only without it is the run finished here.
	if (_mode == Mode::Dma && !_dma_registered && _dma->GetTC_Flag()) RequestStop();
	// A DMA transfer error stops the stream: the run cannot continue
	if ((_mode == Mode::Dma || _mode == Mode::Trig || _mode == Mode::ContDma) && _dma->GetTE_Flag()) {
		_dma_error = true;
		RequestStop();
	}
	return _mode != Mode::Idle;
}

bool ADC_N::IsBusy()
{
	IrqLock lock;
	return RunActive();
}

void ADC_N::RequestStop()
{
	_cancelled = true;
	if (_trg) _trg->Stop();
	if (!_info) { _mode = Mode::NotInit; return; }

#if defined(STM32G0)
	// A request only: completion and CFGR1 writes belong to FinishStop().
	if ((ADCx->CR & ADC_CR_ADSTART) && !(ADCx->CR & ADC_CR_ADSTP))
		ADCx->CR |= ADC_CR_ADSTP;
#elif defined(STM32F4) || defined(STM32F7)
	ADCx->CR2 &= ~(ADC_CR2_ADON | ADC_CR2_EXTEN | ADC_CR2_DMA | ADC_CR2_DDS | ADC_CR2_CONT);
#endif

	// Every pending event of the stopped run is cancelled: a deferred
	// callback must not see (and restart from) an EOC/EOS/TC of it
	clear_run_flags(ADCx);
	if (_dma) {
		_dma->Stream_EN(DISABLE);   // waits until the stream is really off
		_dma->ClearFlags();
	}
	if (_buf) {
		DMA_Sx::CacheInvalidate(_buf, _buf_bytes);   // F7: the CPU reads what the DMA wrote
		_buf = nullptr;
	}

	_mode = Mode::Idle;
}

bool ADC_N::FinishStop()
{
	if (!_info) return true;
#if defined(STM32G0)
	const uint32_t cr = ADCx->CR;
	if ((cr & (ADC_CR_ADSTART | ADC_CR_ADSTP | ADC_CR_ADDIS))
	 || !(cr & ADC_CR_ADEN) || (ADCx->CFGR1 & CFGR1_RUN))
		return WriteCfgr1(CFGR1_RUN, 0u);
#elif defined(STM32F4) || defined(STM32F7)
	if (!(ADCx->CR2 & ADC_CR2_ADON)) {
		ADCx->CR2 |= ADC_CR2_ADON;
		System::Delay_ms(1);   // t_STAB; interrupts are enabled here
	}
#endif
	return true;
}

SysStatus ADC_N::Stop()
{
	if (!adc_thread_context()) return SysStatus::Error;
	{
		IrqLock lock;
		if (_mode == Mode::NotInit) return SysStatus::NotInit;
		if (_mode == Mode::Config) return SysStatus::Busy;
		RequestStop();
		_mode = Mode::Config;
	}
	const bool ok = FinishStop();
	_mode = ok ? Mode::Idle : Mode::NotInit;
	return ok ? SysStatus::OK : SysStatus::Timeout;
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
			_dma->Enable_IRQ(DMA_Sx::IRQ::TE);   // a transfer error ends the run too
		} else if (!en && _dma_registered) {
			_dma->Disable_IRQ(DMA_Sx::IRQ::TC);
			_dma->Disable_IRQ(DMA_Sx::IRQ::TE);
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
// ADC_N::HandleIRQ — phase 1 under the lock (events, flags, state), phase 2
// without it (callbacks). With ADC and DMA lines at different priorities a
// nested call can no longer handle a flag between its read and its clear.
// ---------------------------------------------------------------------------

void ADC_N::HandleIRQ()
{
	bool eoc, eos, ovr, dma_done = false;
	{
		IrqLock lock;

		// Only sources enabled in IER/CR1 are handled: this handler is also
		// called from the DMA line, and polled flags must stay untouched.
#if defined(STM32G0)
		CheckSingleDone(0u);
		// IER bits mirror ISR bit positions
		const uint32_t flags = ADCx->ISR & ADCx->IER
		                     & (ADC_ISR_EOC | ADC_ISR_EOS | ADC_ISR_OVR);
		if (flags) ADCx->ISR = flags;   // clear by writing 1
		eoc = (flags & ADC_ISR_EOC) != 0u;
		eos = (flags & ADC_ISR_EOS) != 0u;
		ovr = (flags & ADC_ISR_OVR) != 0u;
#elif defined(STM32F4) || defined(STM32F7)
		// One SR snapshot: the end of a Start() run is judged on the same EOC
		// that is cleared below
		const uint32_t sr  = ADCx->SR;
		CheckSingleDone(sr);
		const uint32_t cr1 = ADCx->CR1;
		eoc = (sr & ADC_SR_EOC) && (cr1 & ADC_CR1_EOCIE);
		eos = eoc;   // EOCS = 0: EOC marks end of sequence
		ovr = (sr & ADC_SR_OVR) && (cr1 & ADC_CR1_OVRIE);
		const uint32_t clr = (eoc ? ADC_SR_EOC : 0u) | (ovr ? ADC_SR_OVR : 0u);
		if (clr) ADCx->SR = ~clr;   // rc_w0: writing 1 leaves other flags as they are
#endif

		// A late EOC/EOS after an asynchronous stop is acknowledged above,
		// but belongs to the cancelled run and must not call its callbacks.
		if (_cancelled) eoc = eos = ovr = false;

		// Overrun: DMA requests stop, the run is stopped and its other events
		// are cancelled (its data is not reported as complete). StartCont()
		// without DMA overwrites by design and keeps running.
		if (ovr && _mode != Mode::Idle && _mode != Mode::Cont && _mode != Mode::Config) {
			RequestStop();
			eoc = eos = false;
		}

		// DMA TC (fired by the DMA stream, routed here) — only with
		// IRQ::DMA_TC enabled; without it IsBusy() finishes StartDMA()
		const Mode mode = _mode;
		const bool dma_mode = (mode == Mode::Dma || mode == Mode::Trig || mode == Mode::ContDma);
		if (_dma_registered && dma_mode && _dma->GetTE_Flag()) {
			_dma_error = true;                  // stream stopped: the run ends, callback reports it
			RequestStop();
			dma_done = true;
		} else if (_dma_registered && dma_mode && _dma->GetTC_Flag()) {
			if (mode == Mode::Dma) RequestStop();   // StartDMA: the sequence is in, done
			else {
				_dma->ClearTC_Flag();           // circular: the stream keeps running
				DMA_Sx::CacheInvalidate(_buf, _buf_bytes);   // F7: fresh data for the callback
			}
			dma_done = true;
		}
	}

	// Callbacks notify the main loop; configuration/start/Stop run there
	if (ovr      && _ovr_cb) _ovr_cb();
	if (dma_done && _dma_cb) _dma_cb();
	if (eoc      && _eoc_cb) _eoc_cb();
	if (eos      && _eos_cb) _eos_cb();
}
