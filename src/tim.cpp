#include "tim.hpp"

// ---------------------------------------------------------------------------
// Peripheral table — defined in tim_defs.hpp Section B
// ---------------------------------------------------------------------------

#define TIM_DEFS_CPP
#include "tim_defs.hpp"
#undef  TIM_DEFS_CPP

// ---------------------------------------------------------------------------
// TIM base
// ---------------------------------------------------------------------------

SysInitStatus TIM::Init()
{
	if (_info) return SysInitStatus::InitError;
	const PeriphInfo* info = nullptr;
	for (const auto& e : tim_table)
		if (e.periph == TIMx) { info = &e; break; }
	if (!info) return SysInitStatus::InitError;
	*info->clk_reg |= info->clk_bit;
	(void)*info->clk_reg;
	*info->rst_reg |= info->rst_bit;
	*info->rst_reg &= ~info->rst_bit;

	const bool up_enabled = NVIC_GetEnableIRQ(info->irq_up) != 0;
	const bool separate_cc = info->irq_cc != info->irq_up;
	const bool cc_enabled = separate_cc && NVIC_GetEnableIRQ(info->irq_cc) != 0;
	NVIC_DisableIRQ(info->irq_up);
	if (separate_cc) NVIC_DisableIRQ(info->irq_cc);
	bool registered = IRQ_Registry::Register(info->irq_up, this) == SysInitStatus::InitOK;
	if (registered && separate_cc
		&& IRQ_Registry::Register(info->irq_cc, this) != SysInitStatus::InitOK) {
		IRQ_Registry::Unregister(info->irq_up, this);
		registered = false;
	}
	if (!registered) {
		*info->clk_reg &= ~info->clk_bit;
		if (up_enabled) NVIC_EnableIRQ(info->irq_up);
		if (cc_enabled) NVIC_EnableIRQ(info->irq_cc);
		return SysInitStatus::InitError;
	}
	_info = info;
	NVIC_EnableIRQ(info->irq_up);
	if (separate_cc) NVIC_EnableIRQ(info->irq_cc);
	return SysInitStatus::InitOK;
}

void TIM::DeInit()
{
	if (!_info) return;
	TIMx->DIER = 0;
	Stop();
	const IRQn_Type irqs[2] = { _info->irq_up, _info->irq_cc };
	for (unsigned i = 0; i < 2; ++i) {
		if (i && irqs[i] == irqs[0]) continue;
		const bool enabled = NVIC_GetEnableIRQ(irqs[i]) != 0;
		NVIC_DisableIRQ(irqs[i]);
		IRQ_Registry::Unregister(irqs[i], this);
		if (IRQ_Registry::HasHandlers(irqs[i])) {
			if (enabled) NVIC_EnableIRQ(irqs[i]);
		} else NVIC_ClearPendingIRQ(irqs[i]);
	}
	*_info->rst_reg |= _info->rst_bit;
	*_info->rst_reg &= ~_info->rst_bit;
	*_info->clk_reg &= ~_info->clk_bit;
	_update_cb = nullptr;
	_cc_cb[0] = _cc_cb[1] = _cc_cb[2] = _cc_cb[3] = nullptr;
	_info = nullptr;
}

uint8_t TIM::FindITR(TIM_TypeDef* master, TIM_TypeDef* slave)
{
#if defined(STM32F7)
	// F7 routes belong to the separate HWCounter review; none are claimed here.
	(void)master;
	(void)slave;
#else
	for (const auto& r : itr_table)
		if (r.master == master && r.slave == slave) return r.itr;
#endif
	return 0xFF;
}

SysInitStatus TIM::SetFrequency(uint32_t freq, uint32_t arr)
{
	if (!_info || !freq) return SysInitStatus::InitError;
	const uint32_t clock = *_info->bus_clk;
	if (freq > clock || (arr && arr - 1u > _info->arr_max))
		return SysInitStatus::InitError;
	const uint32_t ratio = clock / freq;
	uint32_t psc;
	if (!arr) {
		// Minimum divider that fits a 16-bit period; 32-bit timers need no divider.
		constexpr uint32_t period_16bit = 0x10000u;
		psc = _info->arr_max == UINT32_MAX ? 1u : (ratio - 1u) / period_16bit + 1u;
		arr = ratio / psc;
	} else {
		// floor(floor(clock/freq)/arr) == floor(clock/(freq*arr)); no wide multiply.
		psc = ratio / arr;
	}
	// arr is the period in ticks, not the register value. 65536 fits a 16-bit ARR.
	if (!psc || psc > 0x10000u || !arr || arr - 1u > _info->arr_max)
		return SysInitStatus::InitError;
	TIMx->PSC = psc - 1u;
	TIMx->ARR = arr - 1u;
	if (!(TIMx->CR1 & TIM_CR1_CEN)) {
		// Load PSC before the first period. URS suppresses a synthetic IRQ/DMA request.
		const uint32_t cr1 = TIMx->CR1;
		TIMx->CR1 = (cr1 | TIM_CR1_URS) & ~TIM_CR1_UDIS;
		TIMx->EGR = TIM_EGR_UG;
		TIMx->SR = ~TIM_SR_UIF;
		TIMx->CR1 = cr1;
	}
	return SysInitStatus::InitOK;
}

void TIM::IRQ_en(IRQ irq, FunctionalState en)
{
	if (!_info) return;
	// CC1IE..CC4IE are contiguous; reject channels absent on this instance.
	if (irq != IRQ::UE && static_cast<uint32_t>(irq) >= (TIM_DIER_CC1IE << _info->channel_count))
		return;
	if (en) TIMx->DIER |=  static_cast<uint32_t>(irq);
	else    TIMx->DIER &= ~static_cast<uint32_t>(irq);
}

void TIM::HandleIRQ()
{
	if (!_info) return;
	constexpr uint32_t events = TIM_SR_UIF | TIM_SR_CC1IF | TIM_SR_CC2IF | TIM_SR_CC3IF | TIM_SR_CC4IF;
	const uint32_t sr = TIMx->SR & TIMx->DIER & events;
	// rc_w0: acknowledge only enabled events from this snapshot. Polling flags survive.
	TIMx->SR = ~sr;
	// Fixed four comparisons: missing channel flags are hardwired to zero.
	if ((sr & TIM_SR_UIF)   && _update_cb) _update_cb();
	if ((sr & TIM_SR_CC1IF) && _cc_cb[0])  _cc_cb[0]();
	if ((sr & TIM_SR_CC2IF) && _cc_cb[1])  _cc_cb[1]();
	if ((sr & TIM_SR_CC3IF) && _cc_cb[2])  _cc_cb[2]();
	if ((sr & TIM_SR_CC4IF) && _cc_cb[3])  _cc_cb[3]();
}

// ---------------------------------------------------------------------------
// TIM_PeriodicIRQ
// ---------------------------------------------------------------------------

SysInitStatus TIM_PeriodicIRQ::SetUp(uint32_t freq, void (*cb)(void), uint32_t arr)
{
	SysInitStatus s = Init();
	if (s != SysInitStatus::InitOK) return s;
	s = SetFrequency(freq, arr);
	if (s != SysInitStatus::InitOK) { DeInit(); return s; }
	_update_cb = cb;
	TIMx->SR = 0;
	IRQ_en(IRQ::UE, ENABLE);
	return SysInitStatus::InitOK;
}

SysInitStatus TIM_PeriodicIRQ::SetCompareIRQ(Channel ch, uint32_t compare, void (*cb)(void))
{
	uint32_t ch_idx = static_cast<uint32_t>(ch);
	if (!_info || ch_idx >= _info->channel_count || (TIMx->CR1 & TIM_CR1_CEN)
		|| _cc_cb[ch_idx] || compare > TIMx->ARR)
		return SysInitStatus::InitError;

	*ccr(ch_idx) = compare;
	TIMx->SR = ~(TIM_SR_CC1IF << ch_idx);
	_cc_cb[ch_idx] = cb;
	// CC1IE..CC4IE are contiguous DIER bits — same shift trick used elsewhere in this file.
	IRQ_en(static_cast<IRQ>(TIM_DIER_CC1IE << ch_idx), ENABLE);
	return SysInitStatus::InitOK;
}

// ---------------------------------------------------------------------------
// TIM_PWM
// ---------------------------------------------------------------------------

void TIM_PWM::SetDuty(Channel ch, uint32_t percent)
{
	if (percent > 100) percent = 100;
	// (ARR+1) is the PWM period in ticks; scale by percent instead of assuming
	// the default arr=999 from SetUp() (which only holds for 0.1% resolution).
	// SetDuty targets percentage-scale periods; for ARR near the 32-bit limit
	// (TIM2/TIM5 without a prescaler) use SetCCR() instead.
	*ccr(static_cast<uint32_t>(ch)) = (TIMx->ARR + 1) * percent / 100;
}

void TIM_PWM::SetCCR(Channel ch, uint32_t val)
{
	*ccr(static_cast<uint32_t>(ch)) = val;
}

SysInitStatus TIM_PWM::SetUp(uint32_t freq, uint32_t arr)
{
	bool any = false;
	for (int i = 0; i < 4; i++) if (_ch[i].IsValid()) { any = true; break; }
	if (!any) return SysInitStatus::InitError;

	// Init() must run before SetFrequency() — it needs _info (bus clock,
	// arr_max), which Init() is what actually populates.
	SysInitStatus s = Init();
	if (s != SysInitStatus::InitOK) return s;

	s = SetFrequency(freq, arr);
	if (s != SysInitStatus::InitOK) { DeInit(); return s; }

	TIMx->CCMR1 = 0;
	TIMx->CCMR2 = 0;
	TIMx->CCER  = 0;
	TIMx->CCR1  = 0;
	TIMx->CCR2  = 0;
	TIMx->CCR3  = 0;
	TIMx->CCR4  = 0;

	for (uint32_t i = 0; i < 4; i++) {
		if (!_ch[i].IsValid()) continue;
		_ch[i].ToPin().SetUp(PIN::TYPE::AF_PushPull, PIN::OUTPUT_SPEED::High);
		// OC1M=6 (PWM mode 1) | OC1PE=1 (CCR preload enable).
		// Preload is required for DMA-driven output: without it the DMA write
		// arrives ~3 cycles after the UPDATE event (AHB latency) and overwrites
		// CCR mid-period, truncating the leading edge of the first bit whenever
		// the CCR value changes.  With preload, DMA writes go to the shadow
		// register and are applied atomically at the next UPDATE.
		SetCCMRField(TIM_CCMR1_OC1M_Pos, i, 6u);
		SetCCMRField(TIM_CCMR1_OC1PE_Pos, i, 1u);
		TIMx->CCER |= TIM_CCER_CC1E << (i * 4u);
	}

	if (_info->has_bdtr) TIMx->BDTR = TIM_BDTR_MOE;
	Start();
	return SysInitStatus::InitOK;
}

void TIM_PWM::AttachDMA(DMA_Sx* dma, Channel ch)
{
	if (!_info || !dma) return;
	int idx = static_cast<int>(ch);
	_dma[idx] = dma;

	DMA_Sx::StreamSettings cfg;
	cfg.channel     = _info->dma_up_req;
	cfg.direction   = DMA_Sx::DIR::To_Per;
	cfg.data_size   = DMA_Sx::SIZE::Half_Word;
	cfg.minc        = true;
	cfg.per_address = reinterpret_cast<uint32_t>(ccr(static_cast<uint32_t>(ch)));
	dma->SetUp(cfg);

	IRQn_Type irqn = dma->GetIRQn();
	if (irqn != static_cast<IRQn_Type>(-1)) {
		IRQ_Registry::Register(irqn, this);
		NVIC_EnableIRQ(irqn);
		dma->Enable_IRQ(DMA_Sx::IRQ::TC);
	}
}

SysStatus TIM_PWM::SendDMA(Channel ch, const uint16_t* data, uint32_t len)
{
	int idx = static_cast<int>(ch);
	if (!_dma[idx] || !data || !len) return SysStatus::Error;
	if (_dma_busy & (1u << idx))     return SysStatus::Busy;

	_dma_busy |= static_cast<uint8_t>(1u << idx);
	_dma[idx]->ClearFlags();
	_dma[idx]->SetMemAddr(reinterpret_cast<uint32_t>(data));
	_dma[idx]->SetCount(len);
	// Enable the timer→DMA request on the first channel to start.
	// Subsequent channels reuse the already-set UDE bit.
	if ((_dma_busy & ~static_cast<uint8_t>(1u << idx)) == 0)
		TIMx->DIER |= TIM_DIER_UDE;
	_dma[idx]->Stream_EN(ENABLE);
	return SysStatus::OK;
}

void TIM_PWM::HandleIRQ()
{
	// Check every attached DMA channel — any of them may have triggered this IRQ.
	bool any_done = false;
	for (int i = 0; i < 4; ++i) {
		if (_dma[i] && (_dma_busy & (1u << i)) && _dma[i]->GetTC_Flag()) {
			_dma[i]->Stream_EN(DISABLE);
			_dma[i]->ClearFlags();
			_dma_busy &= ~static_cast<uint8_t>(1u << i);
			any_done = true;
		}
	}
	// Disable the timer→DMA trigger and invoke the callback once all channels finish.
	if (any_done && _dma_busy == 0) {
		TIMx->DIER &= ~TIM_DIER_UDE;
		if (_dma_cb) _dma_cb();
	}
	// Handle timer update / CC interrupts (UIF, CCxIF) if any are enabled.
	TIM::HandleIRQ();
}

// ---------------------------------------------------------------------------
// TIM_EncoderGenerator
// ---------------------------------------------------------------------------

SysInitStatus TIM_EncoderGenerator::SetUp(uint32_t freq, uint32_t period,
                                           uint32_t ch1_width, uint32_t ch2_width)
{
	if (!freq || !_a.pin.IsValid() || !_b.pin.IsValid())
		return SysInitStatus::InitError;

	SysInitStatus s = Init();
	if (s != SysInitStatus::InitOK) return s;

	_a.pin.ToPin().SetUp(PIN::TYPE::AF_PushPull);
	_b.pin.ToPin().SetUp(PIN::TYPE::AF_PushPull);

	uint32_t ai = static_cast<uint32_t>(_a.GetChannel());
	uint32_t bi = static_cast<uint32_t>(_b.GetChannel());

	TIMx->ARR = period;
	TIMx->PSC = *_info->bus_clk / ((period + 1u) * freq * 2u) - 1u;

	*ccr(ai) = ch1_width;
	*ccr(bi) = ch2_width;

	TIMx->CCMR1 = 0;
	TIMx->CCMR2 = 0;

	// Toggle mode = 0b011 on both channels
	SetCCMRField(TIM_CCMR1_OC1M_Pos, ai, 3u);
	SetCCMRField(TIM_CCMR1_OC1M_Pos, bi, 3u);

	TIMx->CCER = (TIM_CCER_CC1E << (ai * 4u)) | (TIM_CCER_CC1E << (bi * 4u));
	if (_info->has_bdtr) TIMx->BDTR = TIM_BDTR_MOE;

	IRQ_en(IRQ::UE, ENABLE);
	return SysInitStatus::InitOK;
}

void TIM_EncoderGenerator::GenPulse(int32_t pulses)
{
	if (pulses > 0)
		TIMx->CCER &= ~TIM_CCER_CC1P;
	else {
		TIMx->CCER |= TIM_CCER_CC1P;
		pulses = -pulses;
	}
	_pulses = pulses;
	Start();
}

void TIM_EncoderGenerator::HandleIRQ()
{
	TIMx->SR = 0;
	int32_t p = _pulses - 1;
	_pulses = p;
	if (p <= 0) {
		Stop();
		if (_on_complete) _on_complete();
	}
}

// ---------------------------------------------------------------------------
// TIM_StepGenerator
// ---------------------------------------------------------------------------

SysInitStatus TIM_StepGenerator::SetUp(bool count_in_isr)
{
	if (!_step.pin.IsValid()) return SysInitStatus::InitError;

	SysInitStatus s = Init();
	if (s != SysInitStatus::InitOK) return s;

	_step.pin.ToPin().SetUp(PIN::TYPE::AF_PushPull);

	uint32_t idx = static_cast<uint32_t>(_step.GetChannel());
	TIMx->CCMR1 = 0;
	TIMx->CCMR2 = 0;
	TIMx->CCER  = 0;

	// PWM mode 1 (OC1M=6): HIGH while CNT < CCR, LOW otherwise. CCR is kept
	// at ARR/2 (see SetStepFrequency()) for an exact 50% duty pulse once per
	// period — one Update event per step, not two like toggle mode would need.
	// OC1PE (CCR preload) pairs with ARPE below so a live SetStepFrequency()
	// call's new ARR and CCR both take effect together at the next Update
	// event, instead of CCR jumping immediately while ARR is still buffered.
	SetCCMRField(TIM_CCMR1_OC1M_Pos, idx, 6u);
	SetCCMRField(TIM_CCMR1_OC1PE_Pos, idx, 1u);
	TIMx->CCER |= TIM_CCER_CC1E << (idx * 4u);
	TIMx->CR1 |= TIM_CR1_ARPE;
	TIMx->ARR = 0;
	TIMx->PSC = 0;

	if (_info->has_bdtr) TIMx->BDTR = TIM_BDTR_MOE;

	// Only pay for the Update IRQ when software step counting was asked for;
	// with a TIM_HWCounter attached instead, no IRQ is needed at all.
	if (count_in_isr) IRQ_en(IRQ::UE, ENABLE);
	return SysInitStatus::InitOK;//SetStepFrequency(freq);
}

SysInitStatus TIM_StepGenerator::SetStepFrequency(uint32_t freq)
{
	TIMx->CR1 &= ~TIM_CR1_OPM;  // continuous mode: undo any earlier RunSteps() one-shot
	SysInitStatus s = SetFrequency(freq);
	if (s != SysInitStatus::InitOK) return s;
	*ccr(static_cast<uint32_t>(_step.GetChannel())) = TIMx->ARR >> 1;  // keep the 50% duty in sync with the new ARR
	return SysInitStatus::InitOK;
}

SysInitStatus TIM_StepGenerator::RunSteps(uint32_t steps)
{
	if (!_info || !steps) return SysInitStatus::InitError;
	if (!_info->has_bdtr) return SysInitStatus::InitError;  // no RCR without BDTR

	// One Update event (period) per output step (PWM mode).
	uint32_t events = steps - 1u;
	if (events > TIM_RCR_REP_Msk) return SysInitStatus::InitError;  // exceeds this timer's RCR width

	Stop();
	TIMx->CNT  = 0;
	TIMx->RCR  = events;
	TIMx->CR1 |= TIM_CR1_OPM;  // auto-clear CEN once RCR reaches 0
	TIMx->EGR  = TIM_EGR_UG;   // force the RCR preload into the active counter before the first period
	Start();
	return SysInitStatus::InitOK;
}

void TIM_StepGenerator::HandleIRQ()
{
	TIMx->SR = 0;
	_step_count = _step_count + 1;
}

// ---------------------------------------------------------------------------
// TIM_InputCapture
// ---------------------------------------------------------------------------

SysInitStatus TIM_InputCapture::SetUp(uint32_t max_freq, void (*cb)(void))
{
	if (!_input.pin.IsValid() || !max_freq) return SysInitStatus::InitError;

	SysInitStatus s = Init();
	if (s != SysInitStatus::InitOK) return s;

	_input.pin.ToPin().SetUp(PIN::TYPE::AF_PushPull);

	TIMx->PSC = *_info->bus_clk / max_freq - 1;
	TIMx->ARR = 0xFFFF;

	uint32_t ch_idx = static_cast<uint32_t>(_input.GetChannel());
	SetCCMRField(TIM_CCMR1_CC1S_Pos, ch_idx, 0b01u);  // map CCx to this channel's own TI input
	TIMx->CCER |= TIM_CCER_CC1E << (ch_idx * 4u);

	_cc_cb[ch_idx] = cb;
	// CC1IE..CC4IE are contiguous DIER bits — same shift trick HandleIRQ() uses for the SR flags.
	IRQ_en(static_cast<IRQ>(TIM_DIER_CC1IE << ch_idx), ENABLE);

	Start();
	return SysInitStatus::InitOK;
}

void TIM_InputCapture::HandleIRQ()
{
	uint32_t sr = TIMx->SR;
	TIMx->SR = ~sr;  // see TIM::HandleIRQ() for why not a blanket `= 0`
	uint32_t ch_idx = static_cast<uint32_t>(_input.GetChannel());
	if (sr & (TIM_SR_CC1IF << ch_idx)) {
		_capture = *ccr(ch_idx);
		if (_cc_cb[ch_idx]) _cc_cb[ch_idx]();
	}
}

// ---------------------------------------------------------------------------
// TIM_PulseMeasure
// ---------------------------------------------------------------------------

SysInitStatus TIM_PulseMeasure::SetUp(uint32_t max_freq)
{
	uint32_t ch_idx = static_cast<uint32_t>(_input.GetChannel());
	if (!_input.pin.IsValid() || ch_idx > 1 || !max_freq)
		return SysInitStatus::InitError;

	SysInitStatus s = Init();
	if (s != SysInitStatus::InitOK) return s;

	_input.pin.ToPin().SetUp(PIN::TYPE::AF_PushPull);

	TIMx->PSC = *_info->bus_clk / max_freq - 1;
	TIMx->ARR = 0xFFFF;

	// Direct mapping on the selected channel, indirect on the complementary one
	TIMx->CCMR1 = (0b01u << (ch_idx        * TIM_CCMR1_CC2S_Pos)) |
	              (0b10u << ((!ch_idx & 1u) * TIM_CCMR1_CC2S_Pos));
	TIMx->SMCR  = ((5u + ch_idx) << TIM_SMCR_TS_Pos) |
	              (0b100u         << TIM_SMCR_SMS_Pos);  // reset mode
	TIMx->CCER  = TIM_CCER_CC1P | TIM_CCER_CC1E | TIM_CCER_CC2E;

	Start();
	return SysInitStatus::InitOK;
}

// ---------------------------------------------------------------------------
// TIM_EncoderReader
// ---------------------------------------------------------------------------

SysInitStatus TIM_EncoderReader::SetUp(Mode mode)
{
	if (!_a.pin.IsValid() || !_b.pin.IsValid()) return SysInitStatus::InitError;

	SysInitStatus s = Init();
	if (s != SysInitStatus::InitOK) return s;

	_a.pin.ToPin().SetUp(PIN::TYPE::AF_PushPull);
	_b.pin.ToPin().SetUp(PIN::TYPE::AF_PushPull);

	// CC1S=01/CC2S=01: map CH1/CH2 to their own TI input, same encoding
	// TIM_InputCapture uses. Encoder Interface mode reads its edges off this
	// mapping internally — CCER (capture/output enable) is not needed since
	// nothing is captured into CCR1/CCR2 or driven onto a pin here, only
	// counted.
	TIMx->CCMR1 = (0b01u << TIM_CCMR1_CC1S_Pos) | (0b01u << TIM_CCMR1_CC2S_Pos);
	TIMx->CCER  = 0;
	TIMx->CNT   = 0;
	TIMx->ARR   = _info->arr_max;  // maximise range before wraparound
	TIMx->SMCR  = static_cast<uint32_t>(mode) << TIM_SMCR_SMS_Pos;

	Start();
	return SysInitStatus::InitOK;
}

// ---------------------------------------------------------------------------
// TIM_HWCounter
// ---------------------------------------------------------------------------

SysInitStatus TIM_HWCounter::SetUp(TIM& master, void (*on_complete)(void))
{
	SysInitStatus s = Init();
	if (s != SysInitStatus::InitOK) return s;

	uint8_t itr = FindITR(master.GetTIMx(), TIMx);
	if (itr == 0xFF) { DeInit(); return SysInitStatus::InitError; }

	master.EnableTriggerOutput();  // MMS = 010: TRGO on Update event
	_master      = &master;
	_on_complete = on_complete;

	TIMx->CNT  = 0;
	TIMx->SMCR = (itr << TIM_SMCR_TS_Pos) | (0b111u << TIM_SMCR_SMS_Pos);  // External clock mode 1
	TIMx->ARR  = _info->arr_max;  // maximise range before wraparound
	// Enabled unconditionally, not just when on_complete is non-null: the
	// master auto-stop from StopAfter()/StopAT() is useful on its own, same
	// as TIM_PeriodicIRQ::SetCompareIRQ()'s "nullptr = no callback, just the
	// auto-stop" convention.
	IRQ_en(IRQ::CC1, ENABLE);
	Start();
	return SysInitStatus::InitOK;
}

SysInitStatus TIM_HWCounter::StopAfter(uint32_t ticks)
{
	if (!_info || !ticks) return SysInitStatus::InitError;
	if (ticks > _info->arr_max) return SysInitStatus::InitError;  // move too long for this counter's width

	// 64-bit: arr_max can be 0xFFFFFFFF (32-bit-ARR timers), where period
	// itself overflows a uint32_t; the modulo below needs the real period.
	uint64_t period = static_cast<uint64_t>(_info->arr_max) + 1;
	bool     down   = IsCountingDown();
	uint32_t target = down
		? static_cast<uint32_t>((TIMx->CNT + period - ticks) % period)
		: static_cast<uint32_t>((TIMx->CNT + ticks) % period);

	*ccr(0) = target;  // CH1, used purely as a pin-free comparator
	IRQ_en(IRQ::CC1, ENABLE);  // re-arm in case CancelStopAfter() switched it off
	return SysInitStatus::InitOK;
}

SysInitStatus TIM_HWCounter::StopAT(uint32_t ticks)
{
	if (!_info || !ticks) return SysInitStatus::InitError;
	if (ticks > _info->arr_max) return SysInitStatus::InitError;  // doesn't fit this counter's width

	*ccr(0) = ticks;  // CH1, used purely as a pin-free comparator
	IRQ_en(IRQ::CC1, ENABLE);  // re-arm in case CancelStopAfter() switched it off
	return SysInitStatus::InitOK;
}

void TIM_HWCounter::HandleIRQ()
{
	uint32_t sr = TIMx->SR;
	TIMx->SR = ~sr;  // see TIM::HandleIRQ() for why not a blanket `= 0`
	if (sr & TIM_SR_CC1IF) {
		if (_master) _master->Stop();
		if (_on_complete) _on_complete();
	}
}
