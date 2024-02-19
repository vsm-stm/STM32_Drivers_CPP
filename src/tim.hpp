#ifndef TIM_HPP_
#define TIM_HPP_

#include <system_f4.hpp>
#include <rcc.hpp>
#include <gpio.hpp>

class TIM
{
protected:
	IRQn_Type IRQ_vector;
	uint32_t bus_clk;
	uint32_t af;

	SYS_StatusTypeDef SetHard();
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

	inline void ClearFlags()
	{
		TIMx->SR &= ~TIM_SR_UIF;
	};	
};

class TIM_PeriodicIRQ : public TIM
{
public:
	TIM_PeriodicIRQ(TIM_TypeDef *timx) : TIM(timx){};

	SYS_StatusTypeDef SetUp(uint32_t freq);

	inline void Start()
	{
		TIMx->CR1 |= TIM_CR1_CEN;
	};

	inline void StopPeriodicIRQ()
	{
		TIMx->CR1 &= ~TIM_CR1_CEN;
	};
};

class TIM_EncoderGenerator : public TIM
{

public:
	TIM_EncoderGenerator(TIM_TypeDef *timx, struct line _A, struct line _B) :
		TIM(timx), 
		A(_A), 
		B(_B)
	{
		line_A_offset = static_cast<uint32_t>(A.channel);
		line_B_offset = static_cast<uint32_t>(B.channel);
	};

	SYS_StatusTypeDef SetUp(uint32_t freq, uint32_t period, uint32_t ch1_width, uint32_t ch2_width);

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
	SYS_StatusTypeDef SetUp(uint32_t max_freq);


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
	TIM_PWM(TIM_TypeDef *timx, struct line ch1, struct line ch2, struct line ch3, struct line ch4) :
		TIM(timx)
	{
		line_ch[0].line = ch1;
		line_ch[1].line = ch2;
		line_ch[2].line = ch3;
		line_ch[3].line = ch4;

		line_ch[0].offset = static_cast<uint32_t>(ch1.channel);
		line_ch[1].offset = static_cast<uint32_t>(ch2.channel);
		line_ch[2].offset = static_cast<uint32_t>(ch3.channel);
		line_ch[3].offset = static_cast<uint32_t>(ch4.channel);
	};
	~TIM_PWM(){};

	SYS_StatusTypeDef SetUp(uint32_t freq);

	void SetLine_Width(uint32_t line, uint32_t width);

private:
	struct _channel
	{
		struct line line;
		uint32_t offset;

		uint32_t* ccr;
		uint32_t* ccmr; 
	} line_ch[4];
	
	
};






#endif
