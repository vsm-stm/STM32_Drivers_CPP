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

	// Channel pin tables + TIM_PIN struct (see tim_defs.hpp Section A).
	// Provides TIM::_1::CH1::PA8, TIM::_3::CH3::PB0, etc.
#include "tim_defs.hpp"

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
	 * @brief Constructs a PWM driver with up to four output channels.
	 *
	 * Pins are passed as TIM::_N::CHx::Pyz constants from tim_defs.hpp.
	 * Each pin encodes its own channel index, so they can be given in any
	 * order and channels can be skipped without counting empty arguments.
	 *
	 * @param timx  Timer peripheral (TIM1, TIM3, …).
	 * @param p0–p3 Channel pins from the TIM::_N::CHx tables; omit unused ones.
	 *
	 * Example:
	 * @code
	 *   // WS2812B on TIM1 CH1+CH2+CH3:
	 *   TIM_PWM ws_tim(TIM1, TIM::_1::CH1::PA8,
	 *                        TIM::_1::CH2::PA9,
	 *                        TIM::_1::CH3::PA10);
	 *
	 *   // Anode dimmer on TIM3 CH3+CH4 only (no empty PIN{} needed):
	 *   TIM_PWM Anode_tim(TIM3, TIM::_3::CH3::PB0,
	 *                           TIM::_3::CH4::PB1);
	 * @endcode
	 */
	TIM_PWM(TIM_TypeDef* timx,
	        TIM_PIN p0 = {}, TIM_PIN p1 = {},
	        TIM_PIN p2 = {}, TIM_PIN p3 = {}) : TIM(timx), _ch{}
	{
		// Validate and route each pin to its declared channel slot.
		for (const TIM_PIN& p : {p0, p1, p2, p3}) {
			if (!p.IsValid()) continue;
			if (p.tim_base && p.tim_base != reinterpret_cast<uint32_t>(timx))
				System::DebugTrap("TIM_PWM: pin belongs to wrong timer");
			_ch[p.channel] = p;
		}
	}

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
	 * @brief Attaches a DMA channel to a PWM output channel.
	 *
	 * Configures the DMA stream to write 16-bit CCR values from a user buffer
	 * to the CCRx register of @p ch on every timer Update event.  Registers
	 * this object in IRQ_Registry for the DMA TC interrupt.
	 *
	 * Multiple channels can be attached independently; each gets its own DMA
	 * stream.  Must be called after SetUp().
	 *
	 * @param dma  DMA stream (pre-constructed, not yet SetUp'd).
	 * @param ch   Timer output channel whose CCR this DMA will drive.
	 */
	void AttachDMA(DMA_Sx* dma, TIM_Channel ch);

	/**
	 * @brief Starts a DMA transfer on one PWM channel.
	 *
	 * Each element of @p data is written to CCRx on the next timer Update
	 * event.  All attached channels share the same TIM_DIER_UDE trigger; call
	 * SendDMA() for every channel you want active before the next update fires.
	 * Returns Busy if this channel's transfer is already in progress.
	 *
	 * @param ch    Output channel to drive.
	 * @param data  Array of 16-bit CCR values (must stay valid until complete).
	 * @param len   Number of values (= PWM periods = WS2812B bits).
	 */
	SysStatus SendDMA(TIM_Channel ch, const uint16_t* data, uint32_t len);

	/** @brief True if the specified channel's DMA is still running. */
	inline bool IsDMABusy(TIM_Channel ch) const {
		return (_dma_busy >> static_cast<uint8_t>(ch)) & 1u;
	}
	/** @brief True if any channel's DMA is still running. */
	inline bool IsAnyDMABusy() const { return _dma_busy != 0; }

	inline void SetDMACallback(void (*cb)(void)) { _dma_cb = cb; }

private:
	TIM_PIN  _ch[4];
	DMA_Sx*  _dma[4]        = {};    ///< One DMA stream per channel (null = unused).
	uint8_t  _dma_busy      = 0;    ///< Bitmask: bit i set while _dma[i] is running.
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
