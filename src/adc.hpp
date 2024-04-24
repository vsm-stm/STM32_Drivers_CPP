#ifndef ADC_HPP_
#define ADC_HPP_

#if defined(STM32F4)
#include <system_f4.hpp>
#elif defined(STM32F7)
#include <system_f7.hpp>
#endif
#include <rcc.hpp>
#include <gpio.hpp>

class ADC_N
{
public:
	ADC_TypeDef *ADCx;

	enum class MODE
	{
		Single,
		Single_continuous,
		Multi,
		Multi_continuous
	};

	enum class TRIG
	{
		TIM1_CH1 = 0,
		TIM1_CH2,
		TIM1_CH3,
		TIM2_CH2,
		TIM2_CH3,
		TIM2_CH4,
		TIM2_TRGO,
		TIM3_CH1,
		TIM3_TRGO,
		TIM4_CH4,
		TIM5_CH1,
		TIM5_CH2,
		TIM5_CH3,
		TIM8_CH1,
		TIM8_TRGO,
		EXTI_line11,
		Manual
	};

	enum class IN_GPIO
	{
		PA0 = 0,
		PA1,
		PA2,
		PA3,
		PA4,
		PA5,
		PA6,
		PA7,
		PB0,
		PB1,
		PC0,
		PC1,
		PC2,
		PC3,
		PC4,
		PC5
	};

	SYS_StatusTypeDef SetUp(MODE mode)
	{	return SetUp(mode, TRIG::Manual);};

	SYS_StatusTypeDef SetUp(MODE mode, TRIG trig);

	void Start()
	{	ADCx->CR2 |= ADC_CR2_SWSTART;};

	void Enable_DMA()
	{	ADCx->CR2 |= ADC_CR2_DMA | ADC_CR2_DDS;};

	uint32_t Get_data()
	{ return ADCx->DR;};

	void Add_Channel(IN_GPIO in)
	{	Add_Channel(static_cast<uint8_t>(in));};
	void Add_Channel(uint8_t ch);

	bool GetStartGlag()
	{	return ((ADCx->SR & ADC_SR_STRT) >> ADC_SR_STRT_Pos);}
	
	void ClearStartGlag()
	{	ADCx->SR &= ~ADC_SR_STRT;}

	bool GetEOCGlag()
	{	return ((ADCx->SR & ADC_SR_EOC) >> ADC_SR_EOC_Pos);}
	
	void ClearEOCGlag()
	{	ADCx->SR &= ~ADC_SR_EOC;}

	ADC_N(ADC_TypeDef *adcx): ADCx(adcx) {};
	~ADC_N(){};

private:
	IRQn_Type IRQ_vector;
	MODE _mode;

	struct IN_st
	{
		PIN pin;
		bool use;
	};

	IN_st IN[16] = {	{PIN(GPIOA, 0),0},
						{PIN(GPIOA, 1),0},
						{PIN(GPIOA, 2),0},
						{PIN(GPIOA, 3),0},
						{PIN(GPIOA, 4),0},
						{PIN(GPIOA, 5),0},
						{PIN(GPIOA, 6),0},
						{PIN(GPIOA, 7),0},
						{PIN(GPIOB, 0),0},
						{PIN(GPIOB, 1),0},
						{PIN(GPIOC, 0),0},
						{PIN(GPIOC, 1),0},
						{PIN(GPIOC, 2),0},
						{PIN(GPIOC, 3),0},
						{PIN(GPIOC, 4),0},
						{PIN(GPIOC, 5),0}
					};
};










#endif /* ADC_HPP_ */
