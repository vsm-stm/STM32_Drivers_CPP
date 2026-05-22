#ifndef TIM_HPP_
#define TIM_HPP_

#include "system.hpp"
#include "irq_registry.hpp"
#include "gpio.hpp"
#include "dma.hpp"

// ---------------------------------------------------------------------------
// TIM — base class
// ---------------------------------------------------------------------------

class TIM : public IIRQHandler
{
public:
	enum class TIM_Channel { CH1 = 0, CH2 = 1, CH3 = 2, CH4 = 3 };

	/** Maps directly to DIER bits — cast to uint32_t for register writes. */
	enum class IRQ {
		UE  = TIM_DIER_UIE,
		CC1 = TIM_DIER_CC1IE,
		CC2 = TIM_DIER_CC2IE,
		CC3 = TIM_DIER_CC3IE,
		CC4 = TIM_DIER_CC4IE,
	};

	/** Pin + channel pair used by TIM_PulseMeasure and TIM_EncoderGenerator. */
	struct Line { PIN pin; TIM_Channel channel; };

	explicit TIM(TIM_TypeDef* timx) : TIMx(timx) {}
	TIM() = delete;
	TIM(const TIM&) = delete;
	TIM& operator=(const TIM&) = delete;

	/**
	 * @brief Looks up the peripheral table, enables the APB clock.
	 * Must be called before any other method.
	 */
	SysInitStatus Init();

	/**
	 * @brief Computes PSC and ARR for the requested frequency.
	 * Maximises ARR (best resolution) while keeping both values ≤ 0xFFFF.
	 */
	SysInitStatus SetFrequency(uint32_t freq);

	/**
	 * @brief Enables or disables one DIER interrupt source.
	 * On first enable: registers in IRQ_Registry and unmasks NVIC.
	 * On last disable: unregisters and masks NVIC.
	 */
	void IRQ_en(IRQ irq, FunctionalState en);

	inline void Start() { TIMx->CR1 |=  TIM_CR1_CEN; }
	inline void Stop()  { TIMx->CR1 &= ~TIM_CR1_CEN; }

	/** Routes the Update event to TRGO (useful for ADC/DAC triggering). */
	inline void EnableTriggerOutput() { TIMx->CR2 |= 2u << TIM_CR2_MMS_Pos; }

	inline void SetUpdateCallback(void (*cb)(void))                { _update_cb = cb; }
	inline void SetCCCallback(TIM_Channel ch, void (*cb)(void))    { _cc_cb[static_cast<uint32_t>(ch)] = cb; }

	TIM_TypeDef* TIMx;

protected:
	struct PeriphInfo {
		TIM_TypeDef*        periph;
		volatile uint32_t*  clk_reg;
		uint32_t            clk_bit;
		uint32_t const*     bus_clk;
		IRQn_Type           irq_up;    ///< Update (or shared) IRQ vector.
		IRQn_Type           irq_cc;    ///< CC IRQ vector; same as irq_up when shared.
		bool                has_bdtr;  ///< True for advanced/semi-advanced timers (TIM1, TIM16, TIM17).
		uint8_t             af;        ///< GPIO alternate-function index for this timer.
		uint32_t            dma_up_req; ///< DMAMUX request ID for the Update event (used by TIM_PWM::AttachDMA).
	};
	static const PeriphInfo tim_table[];

	const PeriphInfo* _info = nullptr;
	void (*_update_cb)(void)  = nullptr;
	void (*_cc_cb[4])(void)   = {};

	/** Returns a pointer to CCRx (0-indexed channel). */
	inline volatile uint32_t* ccr(uint32_t ch) {
		return &TIMx->CCR1 + (&TIMx->CCR2 - &TIMx->CCR1) * ch;
	}

	void HandleIRQ() override;
};

// ---------------------------------------------------------------------------
// TIM_PeriodicIRQ — periodic update interrupt
// ---------------------------------------------------------------------------

class TIM_PeriodicIRQ : public TIM
{
public:
	explicit TIM_PeriodicIRQ(TIM_TypeDef* timx) : TIM(timx) {}

	/**
	 * @brief Configures the timer for periodic update interrupts.
	 * Does NOT call Start() — the caller must do so explicitly.
	 *
	 * @param freq  Interrupt rate in Hz.
	 * @param cb    Callback invoked from the update ISR (nullptr = no callback).
	 */
	SysInitStatus SetUp(uint32_t freq, void (*cb)(void) = nullptr);
};

// ---------------------------------------------------------------------------
// TIM_PWM — PWM output on 1–4 channels
// ---------------------------------------------------------------------------

class TIM_PWM : public TIM
{
public:
	/**
	 * @param timx    Timer peripheral (e.g. TIM1).
	 * @param ch1–ch4 Output pins; pass PIN{} to leave a channel unused.
	 */
	TIM_PWM(TIM_TypeDef* timx,
	        PIN ch1 = PIN{}, PIN ch2 = PIN{},
	        PIN ch3 = PIN{}, PIN ch4 = PIN{}) :
		TIM(timx), _ch{ch1, ch2, ch3, ch4} {}

	/**
	 * @brief Configures PWM on all valid pins and starts the timer.
	 *
	 * @param freq  PWM frequency in Hz.
	 * @param arr   Auto-reload value; PSC is derived automatically.
	 *              Default 999 gives 0.1 % duty resolution via SetDuty().
	 *              For protocols like WS2812B pass an explicit value
	 *              (e.g. arr = 79 at 64 MHz / 800 kHz) and use SetCCR().
	 */
	SysInitStatus SetUp(uint32_t freq, uint32_t arr = 999);

	/** @brief Sets duty cycle in percent (0–100). Clamps at 100. */
	void SetDuty(TIM_Channel ch, uint32_t percent);

	/** @brief Writes CCRx directly (raw value, max = ARR = 999). */
	void SetCCR(TIM_Channel ch, uint32_t val);

	/**
	 * @brief Attaches a DMA channel for PWM waveform output via timer Update event.
	 *
	 * Configures the DMA stream to write 16-bit CCR values from a user buffer
	 * to the CCRx register of @p ch on every timer update.  Also registers
	 * this object in IRQ_Registry for the DMA TC interrupt so SendDMA()
	 * can signal completion automatically.
	 *
	 * Must be called after SetUp().
	 *
	 * @param dma  DMA channel to use (pre-constructed, not yet SetUp'd).
	 * @param ch   Timer output channel whose CCR will be updated by DMA.
	 */
	void AttachDMA(DMA_Sx* dma, TIM_Channel ch);

	/**
	 * @brief Starts a DMA transfer of CCR values, one per timer period.
	 *
	 * Each element of @p data is written to CCRx on the next Update event.
	 * Returns Busy if a transfer is already in progress.
	 *
	 * @param data  Array of 16-bit CCR values (must stay valid until complete).
	 * @param len   Number of values (= number of PWM periods / bits).
	 */
	SysStatus SendDMA(const uint16_t* data, uint32_t len);

	inline bool    IsDMABusy()  const { return _dma_busy; }
	inline void    SetDMACallback(void (*cb)(void)) { _dma_cb = cb; }

private:
	PIN      _ch[4];
	DMA_Sx*  _dma      = nullptr;
	TIM_Channel _dma_ch{TIM_Channel::CH1};
	bool     _dma_busy = false;
	void   (*_dma_cb)(void) = nullptr;

	void HandleIRQ() override;
};

// ---------------------------------------------------------------------------
// TIM_InputCapture — single-channel input capture with callback
// ---------------------------------------------------------------------------

class TIM_InputCapture : public TIM
{
public:
	TIM_InputCapture(TIM_TypeDef* timx, PIN input, TIM_Channel ch) :
		TIM(timx), _input(input), _ch(ch) {}

	/**
	 * @brief Configures the capture channel and enables its CCx interrupt.
	 *
	 * @param max_freq  Maximum expected input frequency (sets PSC).
	 * @param cb        Called after each capture (nullptr = poll via GetCapture).
	 */
	SysInitStatus SetUp(uint32_t max_freq, void (*cb)(void) = nullptr);

	/** @brief Returns the last captured CCR value. */
	inline uint32_t GetCapture() const { return _capture; }

private:
	PIN         _input;
	TIM_Channel _ch;
	uint32_t    _capture = 0;

	void HandleIRQ() override;
};

// ---------------------------------------------------------------------------
// TIM_PulseMeasure — PWM input (period + width) via reset mode
// ---------------------------------------------------------------------------

class TIM_PulseMeasure : public TIM
{
public:
	/**
	 * @param input  Pin + channel; channel must be CH1 or CH2.
	 */
	TIM_PulseMeasure(TIM_TypeDef* timx, Line input) :
		TIM(timx), _input(input),
		_ch_idx(static_cast<uint32_t>(input.channel)) {}

	/** @brief Configures PWM-input mode and starts the timer. */
	SysInitStatus SetUp(uint32_t max_freq);

	inline uint32_t GetWidth()  const { return TIMx->CCR1; }
	inline uint32_t GetPeriod() const { return TIMx->CCR2; }
	inline void     Clear()           { TIMx->CCR1 = 0; TIMx->CCR2 = 0; }

private:
	Line     _input;
	uint32_t _ch_idx;
};

// ---------------------------------------------------------------------------
// TIM_EncoderGenerator — quadrature step-pulse generator for stepper motors
// ---------------------------------------------------------------------------

class TIM_EncoderGenerator : public TIM
{
public:
	/**
	 * @param a  Step pulse channel (A phase).
	 * @param b  Direction channel  (B phase, 90° shifted).
	 */
	TIM_EncoderGenerator(TIM_TypeDef* timx, Line a, Line b) :
		TIM(timx), _a(a), _b(b) {}

	/**
	 * @param freq       Pulse frequency in Hz.
	 * @param period     ARR value (determines CCR resolution).
	 * @param ch1_width  CCR value for channel A.
	 * @param ch2_width  CCR value for channel B.
	 */
	SysInitStatus SetUp(uint32_t freq, uint32_t period,
	                    uint32_t ch1_width, uint32_t ch2_width);

	/**
	 * @brief Generates @p pulses steps.  Positive = forward, negative = reverse.
	 * Starts the timer automatically; stops after the last pulse.
	 */
	void GenPulse(int32_t pulses);

	/** @brief Callback invoked when the last pulse has been generated. */
	inline void SetCompleteCallback(void (*cb)(void)) { _on_complete = cb; }

private:
	Line             _a, _b;
	volatile int32_t _pulses     = 0;
	void           (*_on_complete)(void) = nullptr;

	void HandleIRQ() override;
};

#endif // TIM_HPP_
