#ifndef TIM_HPP_
#define TIM_HPP_

#include "system.hpp"
#include "rcc.hpp"
#include "gpio.hpp"

class TIM
{
protected:
	IRQn_Type IRQ_vector;
	uint32_t bus_clk;
	uint32_t af;

	
public:
	TIM_TypeDef *TIMx;

	TIM(TIM_TypeDef *timx):	TIMx(timx){};

	~TIM(){};

	enum class TIM_Channel
	{
		CHANNEL_1 = 0,
		CHANNEL_2,
		CHANNEL_3,
		CHANNEL_4
	};

	struct line
	{
		PIN pin;
		TIM_Channel channel;
	};

	enum class IRQ
	{
		UE = TIM_DIER_UIE,		
		CC1E = TIM_DIER_CC1IE,	
		CC2E = TIM_DIER_CC2IE,	
		CC3E = TIM_DIER_CC3IE,	
		CC4E = TIM_DIER_CC4IE
	};

	SysInitStatus SetHard();
	SysInitStatus SetFreq(uint32_t freq);

	void Enable_IRQ(IRQ irq)
	{
		TIMx->DIER |= static_cast<uint32_t>(irq);

		if (!(NVIC_GetEnableIRQ(IRQ_vector)))
		{
			NVIC_EnableIRQ(IRQ_vector);
		}
	}

	void Disable_IRQ(IRQ irq)
	{
		TIMx->DIER &= ~(static_cast<uint32_t>(irq));
		if (!(TIMx->DIER & (TIM_DIER_UIE | TIM_DIER_CC1IE | TIM_DIER_CC2IE | TIM_DIER_CC3IE | TIM_DIER_CC4IE)))
		{
			NVIC_DisableIRQ(IRQ_vector);
		}
	}

	inline void ClearFlags()
	{
		TIMx->SR &= ~(TIM_SR_UIF | TIM_SR_CC1IF | TIM_SR_CC2IF | TIM_SR_CC3IF | TIM_SR_CC4IF);
	};

	inline void Enable_TRIG(void)
	{
		TIMx->CR2 |= 2 << TIM_CR2_MMS_Pos;
	}

	inline void Start()
	{
		TIMx->CR1 |= TIM_CR1_CEN;
	};

	inline void StopPeriodicIRQ()
	{
		TIMx->CR1 &= ~TIM_CR1_CEN;
	};
};

class TIM_PeriodicIRQ : public TIM
{
public:
	TIM_PeriodicIRQ(TIM_TypeDef *timx) : TIM(timx){};

	SysInitStatus SetUp(uint32_t freq);
};

class TIM_EncoderGenerator : public TIM
{

public:
	TIM_EncoderGenerator(TIM_TypeDef *timx, struct line _a, struct line _b) :
		TIM(timx), 
		A(_a), 
		B(_b)
	{
		line_A_offset = static_cast<uint32_t>(A.channel);
		line_B_offset = static_cast<uint32_t>(B.channel);
	};

	SysInitStatus SetUp(uint32_t freq, uint32_t period, uint32_t ch1_width, uint32_t ch2_width);

	void GenPulse(int32_t pulses)
	{
		if(pulses > 0)
		{
			TIMx->CCER &= ~TIM_CCER_CC1P;
			_pulses += pulses;
		}
		else
		{
			TIMx->CCER |= TIM_CCER_CC1P;
			_pulses += -pulses;
		}
		TIMx->CR1 |= TIM_CR1_CEN;
	}
	
	void GenPulses_IRQ();

private:
	uint32_t _pulses;
	TIM_Channel Channel_Line_A;
	TIM_Channel Channel_Line_B;
	struct line A{};
	struct line B{};

	uint32_t line_A_offset;
	uint32_t line_B_offset;
};

class TIM_PulseMeasure : public TIM
{
public:
	SysInitStatus SetUp(uint32_t max_freq);


	TIM_PulseMeasure(TIM_TypeDef *timx, struct line in) :
		TIM(timx), 
		Input(in)
	{
		line_offset = static_cast<uint32_t>(Input.channel);
	};
	~TIM_PulseMeasure(){};

	uint32_t GetWidth()
	{	return TIMx->CCR1;}
	
	uint32_t GetPeriod()
	{	return TIMx->CCR2;}

	void Clear()
	{ 
		TIMx->CCR1 = 0;
		TIMx->CCR2 = 0;
	}

private:
	struct line Input{};
	uint32_t line_offset;
};

class TIM_PWM : public TIM
{
public:
	TIM_PWM(TIM_TypeDef *timx, PIN _ch1, PIN _ch2, PIN _ch3, PIN _ch4) :
		TIM(timx),
		CH1(_ch1),
		CH2(_ch2),
		CH3(_ch3),
		CH4(_ch4)
	{};
	~TIM_PWM(){};

	SysInitStatus SetUp(uint32_t freq);
	void SetCCR(TIM_Channel ch, uint32_t width);

private:
	PIN CH1{};
	PIN CH2{};
	PIN CH3{};
	PIN CH4{};
	
	int32_t arr_off = 1;
};






#endif
