#include "tim.hpp"

// ---------------------------------------------------------------------------
// Peripheral table
// ---------------------------------------------------------------------------

#if defined(STM32G0)
const TIM::PeriphInfo TIM::tim_table[] = {
	{ TIM1,  &RCC->APBENR2, RCC_APBENR2_TIM1EN,  &System::TIMxAPB1Clock,
	  TIM1_BRK_UP_TRG_COM_IRQn, TIM1_CC_IRQn,  true,  2, DMA_Sx::Req::Tim1::UP.ch  },
	{ TIM3,  &RCC->APBENR1, RCC_APBENR1_TIM3EN,  &System::TIMxAPB1Clock,
	  TIM3_IRQn,              TIM3_IRQn,         false, 1, DMA_Sx::Req::Tim3::UP.ch  },
	{ TIM14, &RCC->APBENR2, RCC_APBENR2_TIM14EN, &System::TIMxAPB1Clock,
	  TIM14_IRQn,             TIM14_IRQn,        false, 4, 0                          },
	{ TIM16, &RCC->APBENR2, RCC_APBENR2_TIM16EN, &System::TIMxAPB1Clock,
	  TIM16_IRQn,             TIM16_IRQn,        true,  2, DMA_Sx::Req::Tim16::UP.ch },
	{ TIM17, &RCC->APBENR2, RCC_APBENR2_TIM17EN, &System::TIMxAPB1Clock,
	  TIM17_IRQn,             TIM17_IRQn,        true,  2, DMA_Sx::Req::Tim17::UP.ch },
};
#elif defined(STM32F4)
const TIM::PeriphInfo TIM::tim_table[] = {
	{ TIM1,  &RCC->APB2ENR, RCC_APB2ENR_TIM1EN,  &System::TIMxAPB2Clock,
	  TIM1_UP_TIM10_IRQn,      TIM1_CC_IRQn,               true,  1, 0 },
	{ TIM2,  &RCC->APB1ENR, RCC_APB1ENR_TIM2EN,  &System::TIMxAPB1Clock,
	  TIM2_IRQn,               TIM2_IRQn,                  false, 1, 0 },
	{ TIM3,  &RCC->APB1ENR, RCC_APB1ENR_TIM3EN,  &System::TIMxAPB1Clock,
	  TIM3_IRQn,               TIM3_IRQn,                  false, 2, 0 },
	{ TIM4,  &RCC->APB1ENR, RCC_APB1ENR_TIM4EN,  &System::TIMxAPB1Clock,
	  TIM4_IRQn,               TIM4_IRQn,                  false, 2, 0 },
	{ TIM5,  &RCC->APB1ENR, RCC_APB1ENR_TIM5EN,  &System::TIMxAPB1Clock,
	  TIM5_IRQn,               TIM5_IRQn,                  false, 2, 0 },
	{ TIM6,  &RCC->APB1ENR, RCC_APB1ENR_TIM6EN,  &System::TIMxAPB1Clock,
	  TIM6_DAC_IRQn,           TIM6_DAC_IRQn,              false, 0, 0 },
	{ TIM7,  &RCC->APB1ENR, RCC_APB1ENR_TIM7EN,  &System::TIMxAPB1Clock,
	  TIM7_IRQn,               TIM7_IRQn,                  false, 0, 0 },
	{ TIM8,  &RCC->APB2ENR, RCC_APB2ENR_TIM8EN,  &System::TIMxAPB2Clock,
	  TIM8_UP_TIM13_IRQn,      TIM8_CC_IRQn,               true,  3, 0 },
	{ TIM9,  &RCC->APB2ENR, RCC_APB2ENR_TIM9EN,  &System::TIMxAPB2Clock,
	  TIM1_BRK_TIM9_IRQn,      TIM1_BRK_TIM9_IRQn,         false, 3, 0 },
	{ TIM10, &RCC->APB2ENR, RCC_APB2ENR_TIM10EN, &System::TIMxAPB2Clock,
	  TIM1_UP_TIM10_IRQn,      TIM1_UP_TIM10_IRQn,         false, 3, 0 },
	{ TIM11, &RCC->APB2ENR, RCC_APB2ENR_TIM11EN, &System::TIMxAPB2Clock,
	  TIM1_TRG_COM_TIM11_IRQn, TIM1_TRG_COM_TIM11_IRQn,   false, 3, 0 },
	{ TIM12, &RCC->APB1ENR, RCC_APB1ENR_TIM12EN, &System::TIMxAPB1Clock,
	  TIM8_BRK_TIM12_IRQn,     TIM8_BRK_TIM12_IRQn,        false, 9, 0 },
	{ TIM13, &RCC->APB1ENR, RCC_APB1ENR_TIM13EN, &System::TIMxAPB1Clock,
	  TIM8_UP_TIM13_IRQn,      TIM8_UP_TIM13_IRQn,         false, 9, 0 },
	{ TIM14, &RCC->APB1ENR, RCC_APB1ENR_TIM14EN, &System::TIMxAPB1Clock,
	  TIM8_TRG_COM_TIM14_IRQn, TIM8_TRG_COM_TIM14_IRQn,   false, 9, 0 },
};
#endif

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

SysInitStatus TIM::SetFrequency(uint32_t freq)
{
	if (!_info || !freq) return SysInitStatus::InitError;
	uint32_t ratio = *_info->bus_clk / freq;     // (PSC+1)*(ARR+1)
	uint32_t psc   = ratio / 0x10000;
	uint32_t arr   = ratio / (psc + 1) - 1;
	if (psc > 0xFFFF || arr > 0xFFFF) return SysInitStatus::InitError;
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
	*ccr(static_cast<uint32_t>(ch)) = percent * 10u;
}

void TIM_PWM::SetCCR(TIM_Channel ch, uint32_t val)
{
	*ccr(static_cast<uint32_t>(ch)) = val;
}

SysInitStatus TIM_PWM::SetUp(uint32_t freq, uint32_t arr)
{
	bool any = false;
	for (int i = 0; i < 4; i++) if (_ch[i].IsValid()) { any = true; break; }
	if (!freq || !arr || arr > 0xFFFF || !any) return SysInitStatus::InitError;

	SysInitStatus s = Init();
	if (s != SysInitStatus::InitOK) return s;

	uint32_t psc = *_info->bus_clk / ((arr + 1u) * freq) - 1u;
	if (psc > 0xFFFF) return SysInitStatus::InitError;
	TIMx->PSC = psc;
	TIMx->ARR = arr;

	TIMx->CCMR1 = 0;
	TIMx->CCMR2 = 0;
	TIMx->CCER  = 0;

	for (uint32_t i = 0; i < 4; i++) {
		if (!_ch[i].IsValid()) continue;
		_ch[i].SetUp(PIN::TYPE::AF_PushPull, _info->af);
		uint32_t shift = TIM_CCMR1_OC1M_Pos + (i & 1u) * 8u;
		if (i < 2) TIMx->CCMR1 |= 6u << shift;
		else       TIMx->CCMR2 |= 6u << shift;
		TIMx->CCER |= TIM_CCER_CC1E << (i * 4u);
	}

	if (_info->has_bdtr) TIMx->BDTR = TIM_BDTR_MOE;
	Start();
	return SysInitStatus::InitOK;
}

void TIM_PWM::AttachDMA(DMA_Sx* dma, TIM_Channel ch)
{
	if (!_info || !dma) return;
	_dma    = dma;
	_dma_ch = ch;

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

SysStatus TIM_PWM::SendDMA(const uint16_t* data, uint32_t len)
{
	if (!_dma || !data || !len) return SysStatus::Error;
	if (_dma_busy)              return SysStatus::Busy;

	_dma_busy = true;
	_dma->ClearFlags();
	_dma->SetMemAddr(reinterpret_cast<uint32_t>(data));
	_dma->SetCount(len);
	TIMx->DIER |= TIM_DIER_UDE;
	_dma->Stream_EN(ENABLE);
	return SysStatus::OK;
}

void TIM_PWM::HandleIRQ()
{
	if (_dma && _dma->GetTC_Flag()) {
		_dma->Stream_EN(DISABLE);
		_dma->ClearFlags();
		TIMx->DIER &= ~TIM_DIER_UDE;
		_dma_busy = false;
		if (_dma_cb) _dma_cb();
	} else {
		TIM::HandleIRQ();
	}
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
