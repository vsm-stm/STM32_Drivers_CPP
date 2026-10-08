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
	for (const auto& e : tim_table)
		if (e.periph == TIMx) { _info = &e; break; }
	if (!_info) return SysInitStatus::InitError;
	*_info->clk_reg |= _info->clk_bit;
	return SysInitStatus::InitOK;
}

void TIM::Deinit()
{
	if (!_info) return;

	// Disable+unregister every possible DIER source; IRQ_en() no-ops on bits
	// that were never set, and only actually unregisters once DIER is fully
	// clear, so calling all 5 unconditionally is safe regardless of what
	// this object had enabled.
	IRQ_en(IRQ::UE,  DISABLE);
	IRQ_en(IRQ::CC1, DISABLE);
	IRQ_en(IRQ::CC2, DISABLE);
	IRQ_en(IRQ::CC3, DISABLE);
	IRQ_en(IRQ::CC4, DISABLE);

	Stop();

	// Pulse the reset bit: forces every register (CR1/CR2/SMCR/DIER/SR/EGR/
	// CCMR1/CCMR2/CCER/CNT/PSC/ARR/RCR/CCR1-4/BDTR/DCR/DMAR/OR) back to
	// power-on-reset state in one shot, regardless of which mode configured
	// any of them last.
	*_info->rst_reg |= _info->rst_bit;
	*_info->rst_reg &= ~_info->rst_bit;

	// Undo Init()'s clock enable.
	*_info->clk_reg &= ~_info->clk_bit;

	_update_cb = nullptr;
	_cc_cb[0] = _cc_cb[1] = _cc_cb[2] = _cc_cb[3] = nullptr;
}

uint8_t TIM::FindITR(TIM_TypeDef* master, TIM_TypeDef* slave)
{
	for (const auto& r : itr_table)
		if (r.master == master && r.slave == slave) return r.itr;
	return 0xFF;
}

SysInitStatus TIM::SetFrequency(uint32_t freq, uint32_t arr)
{
	if (!_info || !freq || arr > _info->arr_max) return SysInitStatus::InitError;

	// psc is kept in divisor form (1-based) by both branches below, so the
	// bounds check and register writes at the bottom can be shared: register
	// value is always psc-1, valid divisors are 1..0x10000 (register 0..0xFFFF).
	// psc itself always fits in 32 bits (it can never exceed *_info->bus_clk,
	// which is already a uint32_t) — only the operands being multiplied or
	// summed below need 64-bit width, to avoid overflowing before the
	// division that shrinks the result back down actually happens.
	uint32_t psc;

	if (!arr) {
		// arr not given: pick PSC and ARR together to maximise resolution.
		// 64-bit: arr_max+1 overflows uint32_t when arr_max is 0xFFFFFFFF (32-bit ARR).
		uint32_t ratio = *_info->bus_clk / freq;     // (PSC+1)*(ARR+1), fits in 32 bits
		psc = static_cast<uint32_t>(ratio / (static_cast<uint64_t>(_info->arr_max) + 1)) + 1;
		arr = ratio / psc;
	} else {
		// arr given: only PSC is ours to pick, for exactly this period.
		// Cast arr (not the product) to 64-bit first: arr can be up to
		// 0xFFFFFFFF on 32-bit-ARR timers, which would silently overflow a
		// plain uint32_t multiply against freq before a cast on the product
		// could do anything about it.
		psc = static_cast<uint32_t>(*_info->bus_clk / (static_cast<uint64_t>(arr) * freq));
	}
	// psc == 0 means the requested freq is unreachable at this arr/bus-clock
	// (would need PSC register = -1); psc > 0x10000 doesn't fit a 16-bit
	// PSC register (register = psc-1, so psc up to 0x10000 -> register 0xFFFF).
	if (!psc || psc > 0x10000 || arr > _info->arr_max) return SysInitStatus::InitError;

	TIMx->PSC = psc - 1u;
	TIMx->ARR = arr - 1u;
	return SysInitStatus::InitOK;
}

void TIM::IRQ_en(IRQ irq, FunctionalState en)
{
	if (!_info) return;
	if (en) TIMx->DIER |=  static_cast<uint32_t>(irq);
	else    TIMx->DIER &= ~static_cast<uint32_t>(irq);

	constexpr uint32_t all = TIM_DIER_UIE | TIM_DIER_CC1IE | TIM_DIER_CC2IE |
	                         TIM_DIER_CC3IE | TIM_DIER_CC4IE;
	const bool any = (TIMx->DIER & all) != 0u;

	// Vectors are often shared (TIM1_UP_TIM10, TIM3_TIM4, TIM6_DAC_LPTIM1, ...),
	// so track this object's own registration instead of the NVIC state, remove
	// only this object, and mask the NVIC only when nobody is left on the line.
	if (any && !_irq_registered) {
		IRQ_Registry::Register(_info->irq_up, this);
		NVIC_EnableIRQ(_info->irq_up);
		if (_info->irq_cc != _info->irq_up) {
			IRQ_Registry::Register(_info->irq_cc, this);
			NVIC_EnableIRQ(_info->irq_cc);
		}
		_irq_registered = true;
	} else if (!any && _irq_registered) {
		IRQ_Registry::Unregister(_info->irq_up, this);
		if (!IRQ_Registry::HasHandlers(_info->irq_up)) NVIC_DisableIRQ(_info->irq_up);
		if (_info->irq_cc != _info->irq_up) {
			IRQ_Registry::Unregister(_info->irq_cc, this);
			if (!IRQ_Registry::HasHandlers(_info->irq_cc)) NVIC_DisableIRQ(_info->irq_cc);
		}
		_irq_registered = false;
	}
}

void TIM::HandleIRQ()
{
	// Only sources enabled in DIER are handled (DIER UIE/CCxIE sit at the same
	// bit positions as SR UIF/CCxIF): the vector may be shared with another
	// timer, and UIF/CCxIF are set by hardware whether or not their interrupt
	// is enabled — without the mask a foreign interrupt would fire our
	// callbacks and clear flags the application polls.
	uint32_t sr = TIMx->SR & TIMx->DIER &
	              (TIM_SR_UIF | TIM_SR_CC1IF | TIM_SR_CC2IF | TIM_SR_CC3IF | TIM_SR_CC4IF);
	// SR is write-0-to-clear/write-1-is-no-op, so `~sr` clears exactly the
	// flags handled here and leaves everything else untouched. A blanket
	// `SR = 0` would also wipe out any flag hardware sets in the gap between
	// the read and the write (e.g. CC2 firing while CC1's callback below is
	// still running) — silently dropping that event instead of just leaving
	// it set for the next entry (re-triggered via tail-chaining, not lost).
	if (sr) TIMx->SR = ~sr;
	// Always checking all 4 CC flags (rather than looping to _info->channel_count)
	// is intentional: on timers with fewer channels the missing SR bits are
	// hardwired to 0 per the reference manual, so the extra checks are both
	// correct and cheap (one AND+branch each, dwarfed by ISR entry/exit) —
	// not a per-timer special case worth complicating this for.
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
	_update_cb = cb;
	SysInitStatus s = Init();
	if (s != SysInitStatus::InitOK) return s;
	s = SetFrequency(freq, arr);  // arr == 0 lets SetFrequency() pick ARR itself
	if (s != SysInitStatus::InitOK) return s;
	TIMx->SR = 0;
	IRQ_en(IRQ::UE, ENABLE);
	return SysInitStatus::InitOK;
}

SysInitStatus TIM_PeriodicIRQ::SetCompareIRQ(Channel ch, uint32_t compare, void (*cb)(void))
{
	uint32_t ch_idx = static_cast<uint32_t>(ch);
	if (!_info || ch_idx >= _info->channel_count || compare > TIMx->ARR)
		return SysInitStatus::InitError;

	*ccr(ch_idx) = compare;
	SetCCCallback(ch, cb);
	// CC1IE..CC4IE are contiguous DIER bits — same shift trick used elsewhere in this file.
	IRQ_en(static_cast<IRQ>(TIM_DIER_CC1IE << ch_idx), ENABLE);
	return SysInitStatus::InitOK;
}

// ---------------------------------------------------------------------------
// TIM_TriggerGenerator
// ---------------------------------------------------------------------------

SysInitStatus TIM_TriggerGenerator::SetUpBase(uint32_t freq, uint32_t period)
{
	SysInitStatus s = Init();
	if (s != SysInitStatus::InitOK) return s;

	Stop();
	s = SetFrequency(freq, period);
	if (s != SysInitStatus::InitOK) return s;

	// No trigger routed yet: the forced update below (loads the preloaded
	// PSC/ARR) must not reach the consumer.
	TIMx->CR2 &= ~TIM_CR2_MMS;
#if defined(TIM_CR2_MMS2)
	TIMx->CR2 &= ~TIM_CR2_MMS2;
#endif
	TIMx->CR1 |= TIM_CR1_ARPE;
	TIMx->EGR  = TIM_EGR_UG;
	TIMx->SR   = 0;
	return SysInitStatus::InitOK;
}

SysInitStatus TIM_TriggerGenerator::SetUp(uint32_t freq)
{
	if (_req.source != TrigSource::TRGO && _req.source != TrigSource::TRGO2)
		return SysInitStatus::InitError;

	SysInitStatus s = SetUpBase(freq, 0);
	if (s != SysInitStatus::InitOK) return s;

	if (_req.source == TrigSource::TRGO)
		TIMx->CR2 |= 2u << TIM_CR2_MMS_Pos;            // MMS = 010: Update -> TRGO
#if defined(TIM_CR2_MMS2)
	else
		TIMx->CR2 |= 2u << TIM_CR2_MMS2_Pos;           // MMS2 = 0010: Update -> TRGO2
#endif
	return SysInitStatus::InitOK;
}

SysInitStatus TIM_TriggerGenerator::SetUp(uint32_t freq, uint32_t period, uint32_t compare)
{
	if (_req.source == TrigSource::TRGO || _req.source == TrigSource::TRGO2)
		return SysInitStatus::InitError;
	if (!period || compare >= period) return SysInitStatus::InitError;

	SysInitStatus s = SetUpBase(freq, period);
	if (s != SysInitStatus::InitOK) return s;

	// Channel as output compare, PWM mode 2 (OCxREF rises at the compare
	// match) with CCR preload; the pin is not used.
	const uint32_t ch    = static_cast<uint32_t>(_req.source) - static_cast<uint32_t>(TrigSource::CC1);
	const uint32_t shift = (ch & 1u) * 8u;
	volatile uint32_t& ccmr = (ch < 2) ? TIMx->CCMR1 : TIMx->CCMR2;
	ccmr = (ccmr & ~(0xFFu << shift))
	     | (7u << (TIM_CCMR1_OC1M_Pos + shift))
	     | (1u << (TIM_CCMR1_OC1PE_Pos + shift));
	*ccr(ch) = compare;
	TIMx->EGR = TIM_EGR_UG;   // load the preloaded CCR
	TIMx->SR  = 0;
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
	if (s != SysInitStatus::InitOK) return s;

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
	// The vector may be shared with another timer: count only our own Update
	if (!(TIMx->SR & TIMx->DIER & TIM_SR_UIF)) return;
	TIMx->SR = ~TIM_SR_UIF;
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
	// The vector may be shared with another timer: count only our own Update
	if (!(TIMx->SR & TIMx->DIER & TIM_SR_UIF)) return;
	TIMx->SR = ~TIM_SR_UIF;
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
	uint32_t ch_idx = static_cast<uint32_t>(_input.GetChannel());
	uint32_t sr = TIMx->SR & TIMx->DIER & (TIM_SR_CC1IF << ch_idx);   // see TIM::HandleIRQ()
	if (sr) {
		TIMx->SR = ~sr;
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
	if (itr == 0xFF) return SysInitStatus::InitError;

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
	uint32_t sr = TIMx->SR & TIMx->DIER & TIM_SR_CC1IF;   // see TIM::HandleIRQ()
	if (sr) {
		TIMx->SR = ~sr;
		if (_master) _master->Stop();
		if (_on_complete) _on_complete();
	}
}
