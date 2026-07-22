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

uint8_t TIM::FindITR(TIM_TypeDef* master, TIM_TypeDef* slave)
{
	for (const auto& r : itr_table)
		if (r.master == master && r.slave == slave) return r.itr;
	return 0xFF;
}

SysInitStatus TIM::SetFrequency(uint32_t freq)
{
	if (!_info || !freq) return SysInitStatus::InitError;
	uint32_t ratio = *_info->bus_clk / freq;     // (PSC+1)*(ARR+1), fits in 32 bits
	// arr_max+1 alone needs 64 bits: it overflows uint32_t when arr_max is 0xFFFFFFFF (32-bit ARR).
	uint32_t psc   = static_cast<uint32_t>(ratio / (static_cast<uint64_t>(_info->arr_max) + 1));
	uint32_t arr   = ratio / (psc + 1) - 1;
	if (psc > 0xFFFF || arr > _info->arr_max) return SysInitStatus::InitError;
	TIMx->PSC = psc;
	TIMx->ARR = arr;
	return SysInitStatus::InitOK;
}

void TIM::IRQ_en(IRQ irq, FunctionalState en)
{
	if (!_info) return;
	if (en) TIMx->DIER |=  static_cast<uint32_t>(irq);
	else    TIMx->DIER &= ~static_cast<uint32_t>(irq);

	constexpr uint32_t all = TIM_DIER_UIE | TIM_DIER_CC1IE | TIM_DIER_CC2IE |
	                         TIM_DIER_CC3IE | TIM_DIER_CC4IE;
	if (en && !NVIC_GetEnableIRQ(_info->irq_up)) {
		IRQ_Registry::Register(_info->irq_up, this);
		NVIC_EnableIRQ(_info->irq_up);
		if (_info->irq_cc != _info->irq_up) {
			IRQ_Registry::Register(_info->irq_cc, this);
			NVIC_EnableIRQ(_info->irq_cc);
		}
	} else if (!en && !(TIMx->DIER & all)) {
		IRQ_Registry::Unregister(_info->irq_up);
		NVIC_DisableIRQ(_info->irq_up);
		if (_info->irq_cc != _info->irq_up) {
			IRQ_Registry::Unregister(_info->irq_cc);
			NVIC_DisableIRQ(_info->irq_cc);
		}
	}
}

void TIM::HandleIRQ()
{
	uint32_t sr = TIMx->SR;
	TIMx->SR = 0;
	if ((sr & TIM_SR_UIF)   && _update_cb) _update_cb();
	if ((sr & TIM_SR_CC1IF) && _cc_cb[0])  _cc_cb[0]();
	if ((sr & TIM_SR_CC2IF) && _cc_cb[1])  _cc_cb[1]();
	if ((sr & TIM_SR_CC3IF) && _cc_cb[2])  _cc_cb[2]();
	if ((sr & TIM_SR_CC4IF) && _cc_cb[3])  _cc_cb[3]();
}

// ---------------------------------------------------------------------------
// TIM_PeriodicIRQ
// ---------------------------------------------------------------------------

SysInitStatus TIM_PeriodicIRQ::SetUp(uint32_t freq, void (*cb)(void))
{
	_update_cb = cb;
	SysInitStatus s = Init();
	if (s != SysInitStatus::InitOK) return s;
	s = SetFrequency(freq);
	if (s != SysInitStatus::InitOK) return s;
	TIMx->SR = 0;
	IRQ_en(IRQ::UE, ENABLE);
	return SysInitStatus::InitOK;
}

// ---------------------------------------------------------------------------
// TIM_PWM
// ---------------------------------------------------------------------------

void TIM_PWM::SetDuty(TIM_Channel ch, uint32_t percent)
{
	if (percent > 100) percent = 100;
	// (ARR+1) is the PWM period in ticks; scale by percent instead of assuming
	// the default arr=999 from SetUp() (which only holds for 0.1% resolution).
	// SetDuty targets percentage-scale periods; for ARR near the 32-bit limit
	// (TIM2/TIM5 without a prescaler) use SetCCR() instead.
	*ccr(static_cast<uint32_t>(ch)) = (TIMx->ARR + 1) * percent / 100;
}

void TIM_PWM::SetCCR(TIM_Channel ch, uint32_t val)
{
	*ccr(static_cast<uint32_t>(ch)) = val;
}

SysInitStatus TIM_PWM::SetUp(uint32_t freq, uint32_t arr)
{
	bool any = false;
	for (int i = 0; i < 4; i++) if (_ch[i].IsValid()) { any = true; break; }
	if (!freq || !arr || arr > _info->arr_max || !any) return SysInitStatus::InitError;

	SysInitStatus s = Init();
	if (s != SysInitStatus::InitOK) return s;

	uint32_t psc = *_info->bus_clk / ((arr) * freq);
	if (psc > 0xFFFF) return SysInitStatus::InitError;
	TIMx->PSC = psc - 1u;
	TIMx->ARR = arr - 1u;

	TIMx->CCMR1 = 0;
	TIMx->CCMR2 = 0;
	TIMx->CCER  = 0;
	TIMx->CCR1  = 0;
	TIMx->CCR2  = 0;
	TIMx->CCR3  = 0;
	TIMx->CCR4  = 0;

	for (uint32_t i = 0; i < 4; i++) {
		if (!_ch[i].IsValid()) continue;
		PIN(reinterpret_cast<GPIO_TypeDef*>(_ch[i].port), _ch[i].pin)
		    .SetUp(PIN::TYPE::AF_PushPull, PIN::OUTPUT_SPEED::High, _ch[i].af);
		// OC1M=6 (PWM mode 1) | OC1PE=1 (CCR preload enable).
		// Preload is required for DMA-driven output: without it the DMA write
		// arrives ~3 cycles after the UPDATE event (AHB latency) and overwrites
		// CCR mid-period, truncating the leading edge of the first bit whenever
		// the CCR value changes.  With preload, DMA writes go to the shadow
		// register and are applied atomically at the next UPDATE.
		uint32_t shift    = TIM_CCMR1_OC1M_Pos  + (i & 1u) * 8u;
		uint32_t pe_shift = TIM_CCMR1_OC1PE_Pos + (i & 1u) * 8u;
		uint32_t val = (6u << shift) | (1u << pe_shift);
		if (i < 2) TIMx->CCMR1 |= val;
		else       TIMx->CCMR2 |= val;
		TIMx->CCER |= TIM_CCER_CC1E << (i * 4u);
	}

	if (_info->has_bdtr) TIMx->BDTR = TIM_BDTR_MOE;
	Start();
	return SysInitStatus::InitOK;
}

void TIM_PWM::AttachDMA(DMA_Sx* dma, TIM_Channel ch)
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

SysStatus TIM_PWM::SendDMA(TIM_Channel ch, const uint16_t* data, uint32_t len)
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
// TIM_InputCapture
// ---------------------------------------------------------------------------

SysInitStatus TIM_InputCapture::SetUp(uint32_t max_freq, void (*cb)(void))
{
	if (!_input.IsValid() || !max_freq) return SysInitStatus::InitError;

	SysInitStatus s = Init();
	if (s != SysInitStatus::InitOK) return s;

	_input.SetUp(PIN::TYPE::AF_PushPull, _info->af);

	TIMx->PSC = *_info->bus_clk / max_freq - 1;
	TIMx->ARR = 0xFFFF;

	uint32_t ch_idx = static_cast<uint32_t>(_ch);
	uint32_t cc1s_shift = (ch_idx & 1u) * 8u;  // CC1S/CC2S position within CCMR
	if (ch_idx < 2) TIMx->CCMR1 |= 0b01u << cc1s_shift;
	else            TIMx->CCMR2 |= 0b01u << cc1s_shift;
	TIMx->CCER |= TIM_CCER_CC1E << (ch_idx * 4u);

	_cc_cb[ch_idx] = cb;
	static const IRQ irq_map[] = { IRQ::CC1, IRQ::CC2, IRQ::CC3, IRQ::CC4 };
	IRQ_en(irq_map[ch_idx], ENABLE);

	Start();
	return SysInitStatus::InitOK;
}

void TIM_InputCapture::HandleIRQ()
{
	uint32_t sr = TIMx->SR;
	TIMx->SR = 0;
	uint32_t ch_idx = static_cast<uint32_t>(_ch);
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
	if (!_input.pin.IsValid() || _ch_idx > 1 || !max_freq)
		return SysInitStatus::InitError;

	SysInitStatus s = Init();
	if (s != SysInitStatus::InitOK) return s;

	_input.pin.SetUp(PIN::TYPE::AF_PushPull, _info->af);

	TIMx->PSC = *_info->bus_clk / max_freq - 1;
	TIMx->ARR = 0xFFFF;

	// Direct mapping on the selected channel, indirect on the complementary one
	TIMx->CCMR1 = (0b01u << (_ch_idx       * TIM_CCMR1_CC2S_Pos)) |
	              (0b10u << ((!_ch_idx & 1u) * TIM_CCMR1_CC2S_Pos));
	TIMx->SMCR  = ((5u + _ch_idx) << TIM_SMCR_TS_Pos) |
	              (0b100u          << TIM_SMCR_SMS_Pos);  // reset mode
	TIMx->CCER  = TIM_CCER_CC1P | TIM_CCER_CC1E | TIM_CCER_CC2E;

	Start();
	return SysInitStatus::InitOK;
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

	_a.pin.SetUp(PIN::TYPE::AF_PushPull, _info->af);
	_b.pin.SetUp(PIN::TYPE::AF_PushPull, _info->af);

	uint32_t ai = static_cast<uint32_t>(_a.channel);
	uint32_t bi = static_cast<uint32_t>(_b.channel);

	TIMx->ARR = period;
	TIMx->PSC = *_info->bus_clk / ((period + 1u) * freq * 2u) - 1u;

	*ccr(ai) = ch1_width;
	*ccr(bi) = ch2_width;

	TIMx->CCMR1 = 0;
	TIMx->CCMR2 = 0;

	// Toggle mode = 0b011 on both channels
	auto set_toggle = [this](uint32_t ch) {
		uint32_t shift = TIM_CCMR1_OC1M_Pos + (ch & 1u) * 8u;
		if (ch < 2) TIMx->CCMR1 |= 3u << shift;
		else        TIMx->CCMR2 |= 3u << shift;
	};
	set_toggle(ai);
	set_toggle(bi);

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

SysInitStatus TIM_StepGenerator::SetUp(uint32_t freq, bool count_in_isr)
{
	if (!_step.pin.IsValid() || !freq) return SysInitStatus::InitError;

	SysInitStatus s = Init();
	if (s != SysInitStatus::InitOK) return s;

	_step.pin.SetUp(PIN::TYPE::AF_PushPull, _info->af);

	uint32_t idx = static_cast<uint32_t>(_step.channel);
	TIMx->CCMR1 = 0;
	TIMx->CCMR2 = 0;
	TIMx->CCER  = 0;

	// Toggle mode (OCxM = 0b011): output flips on every compare match, giving
	// an exact 50% duty cycle regardless of the CCR value (0 <= CCR <= ARR).
	uint32_t shift = TIM_CCMR1_OC1M_Pos + (idx & 1u) * 8u;
	if (idx < 2) TIMx->CCMR1 |= 3u << shift;
	else         TIMx->CCMR2 |= 3u << shift;
	TIMx->CCER |= TIM_CCER_CC1E << (idx * 4u);

	if (_info->has_bdtr) TIMx->BDTR = TIM_BDTR_MOE;

	// Only pay for the Update IRQ when software step counting was asked for;
	// with a TIM_HWCounter attached instead, no IRQ is needed at all.
	if (count_in_isr) IRQ_en(IRQ::UE, ENABLE);
	return SetStepFrequency(freq);
}

SysInitStatus TIM_StepGenerator::SetStepFrequency(uint32_t freq)
{
	TIMx->CR1 &= ~TIM_CR1_OPM;  // continuous mode: undo any earlier RunSteps() one-shot
	// Toggle mode needs two counter overflows per output pulse.
	SysInitStatus s = SetFrequency(freq * 2);
	if (s != SysInitStatus::InitOK) return s;
	*ccr(static_cast<uint32_t>(_step.channel)) = TIMx->ARR >> 1;
	return SysInitStatus::InitOK;
}

SysInitStatus TIM_StepGenerator::RunSteps(uint32_t steps)
{
	if (!_info || !steps) return SysInitStatus::InitError;
	if (!_info->has_bdtr) return SysInitStatus::InitError;  // no RCR without BDTR

	// Toggle mode: 2 Update events (periods) per output step.
	uint32_t events = steps * 2u - 1u;
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
// TIM_HWCounter
// ---------------------------------------------------------------------------

SysInitStatus TIM_HWCounter::ChainToMaster(TIM& master)
{
	SysInitStatus s = Init();
	if (s != SysInitStatus::InitOK) return s;

	uint8_t itr = FindITR(master.TIMx, TIMx);
	if (itr == 0xFF) return SysInitStatus::InitError;

	master.EnableTriggerOutput();  // MMS = 010: TRGO on Update event

	TIMx->CNT  = 0;
	TIMx->SMCR = (itr << TIM_SMCR_TS_Pos) | (0b111u << TIM_SMCR_SMS_Pos);  // External clock mode 1
	return SysInitStatus::InitOK;
}

SysInitStatus TIM_HWCounter::SetUp(TIM& master)
{
	SysInitStatus s = ChainToMaster(master);
	if (s != SysInitStatus::InitOK) return s;

	TIMx->ARR = _info->arr_max;  // maximise range before wraparound
	Start();
	return SysInitStatus::InitOK;
}

SysInitStatus TIM_HWCounter::RunUntil(TIM& master, uint32_t steps, void (*on_complete)(void))
{
	if (!steps) return SysInitStatus::InitError;

	SysInitStatus s = ChainToMaster(master);
	if (s != SysInitStatus::InitOK) return s;

	// Toggle mode: 2 master Update events per output step; ARR+1 events cause
	// the overflow that fires HandleIRQ().
	uint32_t arr_value = steps * 2u - 1u;
	if (arr_value > _info->arr_max) return SysInitStatus::InitError;  // move too long for this counter's width

	_master      = &master;
	_on_complete = on_complete;

	TIMx->ARR = arr_value;
	IRQ_en(IRQ::UE, ENABLE);
	Start();
	return SysInitStatus::InitOK;
}

void TIM_HWCounter::HandleIRQ()
{
	TIMx->SR = 0;
	Stop();
	if (_master) _master->Stop();
	if (_on_complete) _on_complete();
}
