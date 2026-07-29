#ifndef TIM_HPP_
#define TIM_HPP_

#include "system.hpp"
#include "irq_registry.hpp"
#include "gpio.hpp"
#include "dma.hpp"

// ---------------------------------------------------------------------------
// TIM — base class
// ---------------------------------------------------------------------------

/**
 * @brief Shared register/IRQ substrate for every timer driver in this file —
 * not a usable driver by itself.
 *
 * TIM owns what every mode needs regardless of what a channel is *for*:
 * clock/peripheral lookup (Init()), PSC/ARR sizing (SetFrequency()),
 * start/stop, DIER/NVIC bookkeeping (IRQ_en()), and routing CCxIF/UIF to
 * user callbacks (HandleIRQ()). What a CCR/CCMR actually configures — PWM
 * output, input capture, PWM-input, toggle-output, hardware counting — is
 * mode-specific and deliberately lives in the subclasses, not here: there is
 * no one "correct" generic channel setup to provide.
 *
 * Because of that, TIM cannot be constructed directly (its constructor is
 * protected) — pick the subclass that matches the mode you want: the
 * pin/channel-free TIM_PeriodicIRQ, one of the generators (TIM_PWM,
 * TIM_EncoderGenerator, TIM_StepGenerator — produce an output signal) or one
 * of the measurers (TIM_InputCapture, TIM_PulseMeasure, TIM_EncoderReader,
 * TIM_HWCounter — read/decode an input signal). This also sidesteps a real
 * hardware trap: CMSIS gives every TIM_TypeDef the same register layout
 * (CCR1-4, CCMR1-2, CCER, BDTR, RCR, OR) even though most peripherals don't
 * implement all of it — TIM6/TIM7 (basic timers) have no CC channels at all
 * (Update event only), TIM9-14 have 1-2, only TIM1/2/3/4/5/8 (F4) or
 * TIM1/TIM3 (G0) have the full 4. Writes to a register a given instance
 * doesn't implement are silently ignored by hardware, not a fault, so a raw
 * `TIM t(TIM6); t.SetCCCallback(...)` would compile and just never fire —
 * going through a subclass avoids that class of mistake because the
 * per-timer channel pin tables in tim_defs.hpp only declare the channels
 * that actually exist (e.g. there is no TIM::_14::CH2).
 */
class TIM : public IIRQHandler
{
public:
	/** @brief 0-indexed output/capture channel selector. */
	enum class Channel { CH1 = 0, CH2 = 1, CH3 = 2, CH4 = 3 };

	// Channel pin tables + TIM_PIN struct (see tim_defs.hpp Section A).
	// Provides TIM::_1::CH1::PA8, TIM::_3::CH3::PB0, etc.
#include "tim_defs.hpp"

	/** @brief Maps directly to DIER bits — cast to uint32_t for register writes. */
	enum class IRQ {
		UE  = TIM_DIER_UIE,
		CC1 = TIM_DIER_CC1IE,
		CC2 = TIM_DIER_CC2IE,
		CC3 = TIM_DIER_CC3IE,
		CC4 = TIM_DIER_CC4IE,
	};

	/**
	 * @brief Pin + channel pair used by TIM_InputCapture, TIM_PulseMeasure,
	 * TIM_EncoderGenerator, TIM_EncoderReader and TIM_StepGenerator.
	 *
	 * Built from a single TIM::_N::CHx::Pyz constant. GetChannel() reads the
	 * channel straight off the wrapped TIM_PIN instead of storing a second
	 * copy, so it can never disagree with the pin's own table entry the way
	 * two separately-typed constructor arguments could.
	 */
	struct Line {
		TIM_PIN pin;
		constexpr Line(TIM_PIN p) : pin(p) {}

		/** @brief Channel this pin is wired to (read off pin.channel). */
		constexpr Channel GetChannel() const { return static_cast<Channel>(pin.channel); }
	};

	/**
	 * @brief Fully resets this timer peripheral in hardware and forgets this
	 * object's own configuration, so the same physical timer can be handed
	 * to a different SetUp() — on this object or a different TIM_* object
	 * wrapping the same TIMx — without a reboot.
	 *
	 * Stops the counter, disables and unregisters every DIER interrupt
	 * source this object had enabled (same cleanup IRQ_en(irq, DISABLE)
	 * does), then pulses this instance's RCC reset bit — which forces every
	 * register (CR1/CR2/SMCR/DIER/SR/EGR/CCMR1/CCMR2/CCER/CNT/PSC/ARR/RCR/
	 * CCR1-4/BDTR/DCR/DMAR/OR) back to power-on-reset state in one shot.
	 * That matters because each mode's SetUp() only clears the registers
	 * *it* cares about (e.g. TIM_PWM::SetUp() never touches SMCR or RCR) —
	 * leftover state from a previous, different mode would otherwise leak
	 * into the next one. Finally disables the peripheral clock (undoing
	 * Init()) and clears the update/CC callbacks.
	 *
	 * No-op if Init() was never called (nothing to undo).
	 *
	 * @code
	 *   TIM_PWM pwm(TIM3, ...);
	 *   TIM_StepGenerator step(TIM3, ...);   // same physical timer, unused so far
	 *
	 *   pwm.SetUp(...); pwm.Start();         // running as PWM
	 *   ...
	 *   pwm.Stop(); pwm.Deinit();            // full hardware reset of TIM3
	 *   step.SetUp(...); step.Start();       // now running as a step generator
	 * @endcode
	 */
	void Deinit();

	/**
	 * @brief Enables or disables one DIER interrupt source.
	 * On first enable: registers in IRQ_Registry and unmasks NVIC.
	 * On last disable: unregisters and masks NVIC.
	 */
	void IRQ_en(IRQ irq, FunctionalState en);

	/** @brief Start()/Stop() as one state, for callers that track "running" as a bool. */
	inline void SetEnable(bool on) { TIMx->CR1 = (TIMx->CR1 & ~ TIM_CR1_CEN) | (on << TIM_CR1_CEN_Pos); }
	/** @brief Enables the counter (sets CEN). */
	inline void Start() { SetEnable(true); }
	/** @brief Disables the counter (clears CEN). */
	inline void Stop()  { SetEnable(false); }

	/** @brief Routes the Update event to TRGO (useful for ADC/DAC triggering). */
	inline void EnableTriggerOutput() { TIMx->CR2 |= 2u << TIM_CR2_MMS_Pos; }

	/** @brief Sets the callback invoked from HandleIRQ() on a Update event. */
	inline void SetUpdateCallback(void (*cb)(void))                { _update_cb = cb; }
	/** @brief Sets the callback invoked from HandleIRQ() on a given channel's CC event. */
	inline void SetCCCallback(Channel ch, void (*cb)(void))    { _cc_cb[static_cast<uint32_t>(ch)] = cb; }

	/**
	 * @brief Underlying peripheral register block.
	 *
	 * Read-only from outside the TIM hierarchy — TIMx itself stays protected
	 * (assignable only in the constructor) so it can never be repointed at a
	 * different peripheral after Init() has already matched _info to the
	 * original one, which would desync clock/IRQ/arr_max facts from the
	 * registers actually being touched. Needed e.g. by
	 * TIM_HWCounter::SetUp() to read a `TIM&` master's peripheral through the
	 * reference (its own derived-class methods reach their own TIMx directly
	 * via `this`, same as every other TIM_*::HandleIRQ()/SetUp()).
	 */
	inline TIM_TypeDef* GetTIMx() const { return TIMx; }

protected:
	/**
	 * @brief Only reachable from a subclass's initializer list — see the
	 * class comment for why TIM itself is never constructed directly.
	 */
	explicit TIM(TIM_TypeDef* timx) : TIMx(timx) {}
	TIM() = delete;
	TIM(const TIM&) = delete;
	TIM& operator=(const TIM&) = delete;

	/** @brief Underlying peripheral register block; set once, in the constructor. */
	TIM_TypeDef* TIMx;

	/**
	 * @brief Looks up the peripheral table, enables the APB clock.
	 * Must be called before any other method. Every subclass's own SetUp()
	 * calls this itself — never meant to be called from outside the TIM
	 * hierarchy, hence protected (public callers should have no reason to
	 * call this ahead of a subclass's SetUp(), and calling it wouldn't do
	 * anything useful on its own — Init() only enables the clock).
	 */
	SysInitStatus Init();

	/**
	 * @brief Computes PSC (and, unless @p arr is given, ARR) for the
	 * requested frequency.
	 *
	 * Protected: this is a raw building block for a subclass's own
	 * frequency-setting method, not something to call directly on an
	 * arbitrary TIM_* object — several subclasses need extra bookkeeping
	 * around it (e.g. TIM_StepGenerator::SetStepFrequency() also repositions
	 * CCR to keep the 50% duty cycle in sync with the new ARR; calling this
	 * instead would leave a stale CCR from before the change).
	 *
	 * @param freq  Target frequency in Hz.
	 * @param arr   Desired period, in ticks (the counter counts 0..arr-1;
	 *              register ARR ends up as arr-1) — same convention as
	 *              TIM_PWM::SetUp()'s @p arr and TIM_PeriodicIRQ::GetPeriod().
	 *              Pass 0 (the default) to have this function pick ARR
	 *              itself, maximising resolution while keeping both PSC and
	 *              ARR within this timer's widths — use an explicit value
	 *              instead whenever the period itself matters, not just the
	 *              resulting frequency (e.g. TIM_PWM needs a specific ARR so
	 *              SetDuty()'s percent-to-CCR scaling has known granularity,
	 *              or a periodic timer wants a round tick count for
	 *              TIM_PeriodicIRQ::SetCompareIRQ() to divide evenly).
	 * @return InitError if @p arr is nonzero and exceeds this timer's max
	 *         ARR width (PeriphInfo::arr_max), or if the resulting PSC
	 *         doesn't fit in 16 bits.
	 */
	SysInitStatus SetFrequency(uint32_t freq, uint32_t arr = 0);

	/**
	 * @brief One row of per-peripheral facts that can't be computed from
	 * TIMx alone — clock gating, IRQ vectors, and hardware geometry.
	 *
	 * One `const PeriphInfo` exists per timer instance the current MCU family
	 * supports, in the `tim_table[]` array defined per-family in
	 * tim_defs.hpp Section B. Init() finds "this" object's row by scanning
	 * `tim_table` for the entry whose `periph` matches `TIMx`, and every
	 * other TIM method reads facts through the resulting `_info` pointer
	 * instead of hard-coding them — that's the whole reason this struct
	 * exists: one lookup instead of a `switch(TIMx)` in every method.
	 *
	 * Example row (STM32F4, TIM3 — a 16-bit general-purpose timer with 4
	 * channels, on APB1, sharing one IRQ vector for Update and CC):
	 * @code
	 *   { TIM3,  &RCC->APB1ENR, RCC_APB1ENR_TIM3EN,  &RCC->APB1RSTR, RCC_APB1RSTR_TIM3RST,
	 *     &System::TIMxAPB1Clock, TIM3_IRQn,  TIM3_IRQn,  false, 4, 0xFFFF, 0 }
	 * @endcode
	 * i.e. `periph`=TIM3, `clk_reg`=&RCC->APB1ENR, `clk_bit`=RCC_APB1ENR_TIM3EN,
	 * `rst_reg`=&RCC->APB1RSTR, `rst_bit`=RCC_APB1RSTR_TIM3RST,
	 * `bus_clk`=&System::TIMxAPB1Clock, `irq_up`=`irq_cc`=TIM3_IRQn (one
	 * shared vector), `has_bdtr`=false (no dead-time/break, not an advanced
	 * timer), `channel_count`=4, `arr_max`=0xFFFF (16-bit counter),
	 * `dma_up_req`=0 (no DMAMUX request wired up for TIM3's Update event on F4).
	 */
	struct PeriphInfo {
		TIM_TypeDef*        periph;    ///< This row's timer, e.g. TIM3. Matched against TIMx in Init().
		volatile uint32_t*  clk_reg;   ///< RCC enable register for this timer's bus, e.g. &RCC->APB1ENR.
		uint32_t            clk_bit;   ///< Bit to OR into *clk_reg to enable the clock, e.g. RCC_APB1ENR_TIM3EN.
		volatile uint32_t*  rst_reg;   ///< RCC reset register for this timer's bus, e.g. &RCC->APB1RSTR.
		uint32_t            rst_bit;   ///< Bit to pulse (set then clear) in *rst_reg to force this instance back to power-on-reset state; used by Deinit(). E.g. RCC_APB1RSTR_TIM3RST.
		uint32_t const*     bus_clk;   ///< This timer's input clock in Hz (see System::TIMxAPBxClock); SetFrequency() divides this by the target frequency to get PSC/ARR.
		IRQn_Type           irq_up;    ///< Update (or shared) IRQ vector.
		IRQn_Type           irq_cc;    ///< CC IRQ vector; same as irq_up when shared.
		bool                has_bdtr;  ///< True for advanced/semi-advanced timers (TIM1, TIM16, TIM17) — enables BDTR/MOE and complementary-output/dead-time hardware.
		uint8_t             channel_count; ///< Number of CC channels this instance implements: 0 (TIM6/TIM7), 1, 2 or 4.
		uint32_t            arr_max;   ///< Max ARR value: 0xFFFF, or 0xFFFFFFFF for 32-bit TIM2/TIM5 (F4/F7).
		uint32_t            dma_up_req; ///< DMAMUX request ID for the Update event (used by TIM_PWM::AttachDMA); 0 where the family has no DMAMUX (F4) or this timer has no DMA request wired.
	};
	static const PeriphInfo tim_table[];

	/**
	 * @brief One "slave can count master's TRGO via this ITR index" fact.
	 * @note itr == 0xFF marks a pair that is not yet verified against the
	 *       Reference Manual's "internal trigger connection" table for this
	 *       family — treat as unsupported until confirmed (see tim_defs.hpp).
	 */
	struct ITR_Route {
		TIM_TypeDef* master;
		TIM_TypeDef* slave;
		uint8_t      itr;   ///< TS field value (0-3 = ITR0-ITR3), or 0xFF = unverified/unsupported.
	};
	static const ITR_Route itr_table[];

	/** @brief Looks up the ITR index connecting master's TRGO to slave's TS. 0xFF if absent/unverified. */
	static uint8_t FindITR(TIM_TypeDef* master, TIM_TypeDef* slave);

	const PeriphInfo* _info = nullptr;      ///< Set by Init(); nullptr until then.
	void (*_update_cb)(void)  = nullptr;    ///< Called from HandleIRQ() on a Update event.
	void (*_cc_cb[4])(void)   = {};         ///< Per-channel callbacks, called from HandleIRQ() on CCxIF.

	/** @brief Returns a pointer to CCRx (0-indexed channel). */
	inline volatile uint32_t* ccr(uint32_t ch) {
		return &TIMx->CCR1 + (&TIMx->CCR2 - &TIMx->CCR1) * ch;
	}

	/**
	 * @brief ORs @p value into a CCMR1/CCMR2 sub-field for the given 0-indexed
	 * channel — e.g. CCxS (input mapping) or OCxM+OCxPE (output mode/preload).
	 *
	 * CH1/CH3 live in the low byte of CCMR1/CCMR2, CH2/CH4 in the high byte
	 * (+8 bit shift); CH1/CH2 live in CCMR1, CH3/CH4 in CCMR2. @p pos is the
	 * field's bit position within CH1's/CH3's byte (e.g. TIM_CCMR1_OC1M_Pos).
	 */
	inline void SetCCMRField(uint32_t pos, uint32_t ch, uint32_t value) {
		uint32_t shift = pos + (ch & 1u) * 8u;
		if (ch < 2) TIMx->CCMR1 |= value << shift;
		else        TIMx->CCMR2 |= value << shift;
	}

	void HandleIRQ() override;
};

// ---------------------------------------------------------------------------
// TIM_PeriodicIRQ — periodic update interrupt
// ---------------------------------------------------------------------------

/**
 * @brief Fires a callback at a fixed rate via the timer's Update interrupt —
 * plus, on timers that have compare channels, any number of additional
 * pin-free callbacks at specific counts within that same period.
 *
 * The base SetUp() needs nothing but PSC/ARR sized for @p freq and the
 * Update IRQ wired to a callback — no GPIO pins, no channels required.
 * SetCompareIRQ() is optional extra scheduling on top of that, for timers
 * that implement at least one CC channel (see PeriphInfo::channel_count —
 * TIM6/TIM7 on F4 are pure "basic" timers with none at all).
 *
 * @code
 *   void Tick() { ... }        // called from the Update ISR
 *   void HalfTick() { ... }    // called from the CC1 ISR, mid-period
 *
 *   TIM_PeriodicIRQ tick(TIM3);
 *   tick.SetUp(1000, &Tick);        // 1 kHz Update — SetUp() picks PSC/ARR itself
 *   tick.SetCompareIRQ(TIM::Channel::CH1, tick.GetPeriod() / 2, &HalfTick);
 *   tick.Start();                   // SetUp() does not start it
 * @endcode
 */
class TIM_PeriodicIRQ : public TIM
{
public:
	explicit TIM_PeriodicIRQ(TIM_TypeDef* timx) : TIM(timx) {}

	/**
	 * @brief Configures the timer for periodic update interrupts.
	 * Does NOT call Start() — the caller must do so explicitly.
	 *
	 * @param freq  Interrupt rate in Hz.
	 * @param arr   Desired period, in ticks (the counter counts 0..arr-1;
	 *              register ARR ends up as arr-1) — same convention as
	 *              GetPeriod().
	 * @param cb    Callback invoked from the update ISR (nullptr = no callback).
	 */
	SysInitStatus SetUp(uint32_t freq, void (*cb)(void) = nullptr, uint32_t arr = 0);

	/**
	 * @brief Changes the interrupt rate, at rest or while running — same
	 * PSC/ARR computation as SetUp(), without touching DIER/callbacks.
	 *
	 * Re-exposed as-is from the (protected) base: unlike TIM_StepGenerator
	 * or TIM_PWM, plain periodic mode has no extra state (no CCR, no OPM)
	 * that a frequency change could leave stale, so there's nothing for a
	 * wrapper here to do beyond what TIM::SetFrequency() already does.
	 *
	 * @note If you've called SetCompareIRQ() on any channel, its @p compare
	 * value keeps its old raw-tick meaning against the new period — check
	 * it's still < GetPeriod() after changing frequency, same caveat as
	 * SetCompareIRQ() has against the period from SetUp().
	 */
	using TIM::SetFrequency;

	/**
	 * @brief Timer period in ticks (ARR+1), as SetUp() actually configured it.
	 *
	 * SetUp() picks PSC/ARR itself to maximise resolution for the requested
	 * frequency, so there is no way to predict ARR from @p freq alone — read
	 * it back through here (e.g. to compute a SetCompareIRQ() value as a
	 * fraction of the period) instead of guessing or reaching into TIMx->ARR
	 * directly.
	 */
	inline uint32_t GetPeriod() const { return TIMx->ARR + 1; }

	/**
	 * @brief Schedules an additional callback at a specific counter value on
	 * one of this timer's compare channels, alongside the Update callback —
	 * without touching any GPIO pin.
	 *
	 * The channel's output compare is left in Frozen mode (OCxM stays at its
	 * reset value of 0): CCxIF still sets purely from the CNT-vs-CCRx match,
	 * independently of OCxM/CCxE, so no pin ever needs to be configured.
	 * Several channels can each carry their own compare value/callback,
	 * letting one timer raise multiple independent scheduled events per
	 * period instead of needing one timer each.
	 *
	 * @param ch       Channel to use for this interrupt.
	 * @param compare  CNT value to trigger at; must be < GetPeriod() (i.e.
	 *                 <= the current ARR) or the callback will never fire —
	 *                 see GetPeriod() for why this can't be predicted from
	 *                 the frequency passed to SetUp().
	 * @param cb       Called from the channel's CC ISR (nullptr = disables
	 *                 the callback but leaves the interrupt/compare set —
	 *                 pass IRQ_en(<that channel's IRQ>, DISABLE) to fully
	 *                 turn it off).
	 * @return InitError if SetUp() hasn't been called yet, if @p compare is
	 *         out of range, or if this timer doesn't implement channel
	 *         @p ch at all (see PeriphInfo::channel_count).
	 */
	SysInitStatus SetCompareIRQ(Channel ch, uint32_t compare, void (*cb)(void));
};

// ===========================================================================
// Generators — produce an output signal
// ===========================================================================

// ---------------------------------------------------------------------------
// TIM_PWM — PWM output on 1–4 channels
// ---------------------------------------------------------------------------

/** @brief PWM output driver, up to four channels with optional DMA-driven CCR updates. */
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
	 *              SetDuty() scales percent by the actual arr, so any value
	 *              works; default 1000 gives 0.1 % resolution.
	 *              For protocols like WS2812B pass an explicit value
	 *              (e.g. arr = 79 at 64 MHz / 800 kHz) and use SetCCR().
	 */
	SysInitStatus SetUp(uint32_t freq, uint32_t arr = 1000);

	/**
	 * @brief Changes the PWM frequency, at rest or while running — without
	 * touching channel mode, pins or BDTR the way calling SetUp() again would.
	 *
	 * @param freq  Target frequency in Hz.
	 * @param arr   Desired period in ticks; 0 (the default) keeps whatever
	 *              period is currently configured (from SetUp() or the last
	 *              call here) and only recomputes PSC — pass an explicit
	 *              value to also change the period/resolution.
	 * @return InitError if SetUp() hasn't been called yet, or if the
	 *         requested freq/arr combination doesn't fit.
	 *
	 * @note Existing CCR values keep their raw-tick meaning: if you drive
	 * duty via SetDuty() (percent of the period), call it again after this
	 * to get the same percentage against the new period — SetCCR() users
	 * already work in raw ticks and aren't otherwise affected.
	 * @note PSC's shadow register only takes effect on the next Update
	 * event, while ARR (no preload enabled here) applies immediately — so
	 * for up to one period after calling this while running, PSC and ARR
	 * can briefly be mismatched. Harmless for most uses; avoid this if a
	 * glitch-free change mid-run matters.
	 */
	inline SysInitStatus SetFrequency(uint32_t freq, uint32_t arr = 0) {
		return TIM::SetFrequency(freq, arr ? arr : (TIMx->ARR + 1));
	}

	/** @brief Sets duty cycle in percent (0–100). Clamps at 100. */
	void SetDuty(Channel ch, uint32_t percent);

	/** @brief Writes CCRx directly (raw value). */
	void SetCCR(Channel ch, uint32_t val);

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
	void AttachDMA(DMA_Sx* dma, Channel ch);

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
	SysStatus SendDMA(Channel ch, const uint16_t* data, uint32_t len);

	/** @brief True if the specified channel's DMA is still running. */
	inline bool IsDMABusy(Channel ch) const {
		return (_dma_busy >> static_cast<uint8_t>(ch)) & 1u;
	}
	/** @brief True if any channel's DMA is still running. */
	inline bool IsAnyDMABusy() const { return _dma_busy != 0; }

	/** @brief Sets the callback invoked once all in-flight DMA channels finish. */
	inline void SetDMACallback(void (*cb)(void)) { _dma_cb = cb; }

	/**
	 * @brief Handle bound to one specific output channel of the TIM_PWM that
	 * created it — remembers both which channel and which TIM_PWM instance,
	 * so (unlike passing a bare Channel around) it can never end up applied
	 * to the wrong PWM object. Get one via GetOutput(); trivially copyable
	 * and cheap to keep around (just a pointer + an enum), so it's fine to
	 * store one per channel for as long as you need to keep calling SetDuty().
	 */
	class Output
	{
	public:
		/** @brief Sets duty cycle in percent (0–100) on this channel. Clamps at 100. */
		inline void SetDuty(uint32_t percent) { _pwm->SetDuty(_ch, percent); }
		/** @brief Writes CCRx directly (raw value) on this channel. */
		inline void SetCCR(uint32_t val) { _pwm->SetCCR(_ch, val); }
		/** @brief True if this channel's DMA is still running. */
		inline bool IsDMABusy() const { return _pwm->IsDMABusy(_ch); }
		/** @brief Attaches a DMA channel to this output. */
		inline void AttachDMA(DMA_Sx* dma) { _pwm->AttachDMA(dma, _ch); }
		/** @brief Starts a DMA transfer on this channel. */
		inline SysStatus SendDMA(const uint16_t* data, uint32_t len) { return _pwm->SendDMA(_ch, data, len); }

	private:
		friend class TIM_PWM;
		Output(TIM_PWM* pwm, Channel ch) : _pwm(pwm), _ch(ch) {}

		TIM_PWM* _pwm;
		Channel  _ch;
	};

	/**
	 * @brief Returns a handle bound to one of this TIM_PWM's output channels,
	 * so calling code doesn't have to keep track of a bare Channel value (and
	 * risk applying it to the wrong TIM_PWM instance) to call
	 * SetDuty()/SetCCR()/etc. on it repeatedly.
	 *
	 * @code
	 *   TIM_PWM pwm(TIM3, TIM::_3::CH1::PA6, TIM::_3::CH2::PA7);
	 *   pwm.SetUp(1000);
	 *
	 *   TIM_PWM::Output red = pwm.GetOutput(TIM::_3::CH2::PA7);
	 *   red.SetDuty(50);              // no channel to mix up from here on
	 *
	 *   pwm.GetOutput(TIM::_3::CH1::PA6).SetDuty(10);  // one-shot, no need to store it
	 * @endcode
	 *
	 * @param pin  The same TIM::_N::CHx::Pyz constant passed to the
	 *             constructor for this channel — reused rather than a bare
	 *             Channel so the channel is never re-typed by hand.
	 * @return A handle for @p pin's channel. Traps if that channel wasn't
	 *         actually configured in the constructor (nothing valid at
	 *         _ch[pin.channel]).
	 */
	inline Output GetOutput(TIM_PIN pin) {
		if (pin.channel >= 4 || !_ch[pin.channel].IsValid())
			System::DebugTrap("TIM_PWM::GetOutput: channel was not configured in the constructor");
		return Output(this, static_cast<Channel>(pin.channel));
	}

private:
	TIM_PIN  _ch[4];
	DMA_Sx*  _dma[4]        = {};    ///< One DMA stream per channel (null = unused).
	uint8_t  _dma_busy      = 0;    ///< Bitmask: bit i set while _dma[i] is running.
	void   (*_dma_cb)(void) = nullptr;

	void HandleIRQ() override;
};

// ---------------------------------------------------------------------------
// TIM_EncoderGenerator — quadrature step-pulse generator for stepper motors
// ---------------------------------------------------------------------------

/** @brief Generates a fixed-count quadrature (A/B) pulse burst for stepper motors. */
class TIM_EncoderGenerator : public TIM
{
public:
	/**
	 * @brief Constructs a quadrature step-pulse generator on two channels.
	 * @param a  Step pulse channel (A phase).
	 * @param b  Direction channel  (B phase, 90° shifted).
	 */
	TIM_EncoderGenerator(TIM_TypeDef* timx, Line a, Line b) :
		TIM(timx), _a(a), _b(b)
	{
		if ((a.pin.tim_base && a.pin.tim_base != reinterpret_cast<uint32_t>(timx)) ||
		    (b.pin.tim_base && b.pin.tim_base != reinterpret_cast<uint32_t>(timx)))
			System::DebugTrap("TIM_EncoderGenerator: pin belongs to wrong timer");
	}

	/**
	 * @brief Configures both channels for toggle-mode quadrature output.
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
	Line             _a, _b;                       ///< A/B phase pins + channels.
	volatile int32_t _pulses     = 0;               ///< Pulses remaining in the current GenPulse() burst.
	void           (*_on_complete)(void) = nullptr; ///< Called from HandleIRQ() when _pulses reaches 0.

	void HandleIRQ() override;
};

// ---------------------------------------------------------------------------
// TIM_StepGenerator — continuous 50%-duty STEP pulse train (STEP/DIR drivers)
// ---------------------------------------------------------------------------

/**
 * @brief Generates the STEP pulse train for a STEP/DIR stepper driver.
 *
 * DIR is deliberately not handled here — it is a plain GPIO toggled by the
 * caller (e.g. Motor::Driver::set_direction), independent of this timer.
 *
 * Uses PWM mode 1 with CCR fixed at ARR/2 (not toggle mode): one full ARR
 * period is exactly one STEP pulse (HIGH first half, LOW second half), so
 * the counter's own Update-event rate is 1:1 with the step rate — no
 * doubling/halving anywhere (SetFrequency(freq) directly, RCR = steps-1,
 * one Update IRQ per step). A pin-toggle scheme would need 2 Update events
 * per pulse instead, complicating every one of those without giving up
 * anything here: PWM mode's 50% duty comes from ARR/2 exactly, same
 * precision toggle mode would have given.
 *
 * Two ways to know how many steps have gone out, chosen via SetUp()'s
 * @p count_in_isr:
 *  - Software (count_in_isr = true): the Update IRQ fires once per step,
 *    GetStepCount() reads a RAM counter. Simplest, costs one ISR per output
 *    pulse — fine up to a few tens of kHz of step rate.
 *  - Hardware (count_in_isr = false): no IRQ at all. Pair this timer with a
 *    TIM_HWCounter chained to its TRGO (see that class) and read steps
 *    straight from TIM_HWCounter::Count() (1:1, no /2 needed) instead —
 *    needed at step rates where an ISR per pulse would load the CPU
 *    noticeably.
 *
 * @code
 *   // Software-counted, up to ~20 kHz steps:
 *   TIM_StepGenerator step(TIM3, TIM::_3::CH1::PA6);
 *   step.SetUp(1000, true);   // 1 kHz, counted via IRQ
 *   step.Start();
 *   uint32_t done = step.GetStepCount();
 *
 *   // Hardware-counted, high step rates, zero ISR overhead:
 *   TIM_StepGenerator step(TIM1, TIM::_1::CH1::PA8);
 *   step.SetUp(200000, false);       // 200 kHz, no IRQ
 *   TIM_HWCounter counter(TIM3);
 *   counter.SetUp(step);              // TIM3 counts TIM1's TRGO via ITR
 *   step.Start();
 *   uint32_t done = counter.Count();
 * @endcode
 *
 * @note Position moves: for a fixed target at constant or ramped speed, use
 * RunSteps(N) instead of Start() — it stops itself in hardware after exactly
 * N pulses (needs an advanced/semi-advanced timer, see RunSteps() docs) and
 * tolerates SetStepFrequency() ramps mid-move. Deciding *what* frequency to
 * ramp through at each moment (accel/decel profile) is not this class's job —
 * that belongs to Motor/orchestration, which knows the motion limits.
 */
class TIM_StepGenerator : public TIM
{
public:
	/**
	 * @brief Constructs a STEP-pulse generator on one channel.
	 * @param step  STEP pulse pin + channel. DIR is a plain GPIO, not owned here.
	 */
	TIM_StepGenerator(TIM_TypeDef* timx, Line step) : TIM(timx), _step(step)
	{
		if (step.pin.tim_base && step.pin.tim_base != reinterpret_cast<uint32_t>(timx))
			System::DebugTrap("TIM_StepGenerator: pin belongs to wrong timer");
	}

	/**
	 * @brief Configures the STEP channel for a free-running square wave.
	 * Does NOT start the timer — call Start()/Stop() (inherited) explicitly.
	 *
	 * @param freq          Initial step frequency in Hz (pulses/sec); 0 is invalid.
	 * @param count_in_isr  true: enable the Update IRQ and maintain GetStepCount()
	 *                      in software. false: no IRQ is enabled at all — use a
	 *                      TIM_HWCounter chained to this timer's TRGO instead
	 *                      (GetStepCount() then stays at 0, it isn't updated).
	 */
	SysInitStatus SetUp(bool count_in_isr = false);// SysInitStatus SetUp(uint32_t freq, bool count_in_isr = true);

	/**
	 * @brief Changes the step frequency, at rest or while running.
	 * Recomputes PSC/ARR from scratch — call at ramp-update rate, not per-pulse.
	 * Also cancels any pending RunSteps() one-shot, returning to free-running mode.
	 */
	SysInitStatus SetStepFrequency(uint32_t freq);

	/**
	 * @brief Generates exactly @p steps pulses, then stops itself in hardware
	 * (Repetition Counter + One Pulse Mode) — no ISR needed to detect
	 * completion, and correct even if SetStepFrequency() changes the speed
	 * mid-run (RCR counts elapsed periods, not elapsed time, so a ramp between
	 * calls doesn't throw off the count).
	 *
	 * Requires an advanced/semi-advanced timer with a Repetition Counter
	 * (TIM1/TIM8, TIM16/TIM17 — wherever PeriphInfo::has_bdtr is true);
	 * returns InitError otherwise, or if @p steps doesn't fit this timer's
	 * RCR width.
	 *
	 * @note The RCR arithmetic below (steps-1 Update events) has not been
	 * confirmed on real hardware — verify the actual pulse count on a scope
	 * on first bring-up before relying on it for position accuracy.
	 *
	 * @param steps  Number of STEP pulses to generate; 0 is invalid.
	 *
	 * @note No RCR on this timer (e.g. STEP generated on TIM3)? Use
	 * TIM_HWCounter::StopAfter() instead — pair a second timer chained via ITR
	 * and let its own down-count-to-zero stop this one. One IRQ for the whole move
	 * (on completion), not zero, but still nowhere near an IRQ per step.
	 */
	SysInitStatus RunSteps(uint32_t steps);

	/** @brief True while the timer is generating pulses (continuous or a RunSteps() burst). */
	inline bool IsRunning() const { return (TIMx->CR1 & TIM_CR1_CEN) != 0; }

	/** @brief Steps (rising edges) generated since SetUp() or the last ResetStepCount(). Only updates when SetUp() was called with count_in_isr = true; else stays 0 — see TIM_HWCounter. */
	inline uint32_t GetStepCount() const { return _step_count; }
	/** @brief Resets GetStepCount() back to 0. */
	inline void     ResetStepCount()     { _step_count = 0; }

private:
	Line              _step;
	volatile uint32_t _step_count = 0;  ///< One Update event = one step. Unused when count_in_isr = false.

	void HandleIRQ() override;
};

// ===========================================================================
// Measurers — read/decode an input signal
// ===========================================================================

// ---------------------------------------------------------------------------
// TIM_InputCapture — single-channel input capture with callback
// ---------------------------------------------------------------------------

/**
 * @brief Captures CCRx on a single channel's input edge, optionally via callback.
 *
 * @code
 *   // Poll-based: read the last capture whenever convenient.
 *   TIM_InputCapture cap(TIM3, TIM::_3::CH1::PA6);
 *   cap.SetUp(1000000);                 // max expected input frequency, no callback
 *   uint32_t ticks = cap.GetCapture();
 *
 *   // Callback-based: get notified from the CCx ISR on every edge instead.
 *   TIM_InputCapture cap(TIM3, TIM::_3::CH1::PA6);
 *   cap.SetUp(1000000, &OnEdgeCaptured);
 * @endcode
 */
class TIM_InputCapture : public TIM
{
public:
	/**
	 * @brief Constructs an input-capture driver on one channel.
	 * @param input  Pin + channel to capture on.
	 */
	TIM_InputCapture(TIM_TypeDef* timx, Line input) : TIM(timx), _input(input)
	{
		if (input.pin.tim_base && input.pin.tim_base != reinterpret_cast<uint32_t>(timx))
			System::DebugTrap("TIM_InputCapture: pin belongs to wrong timer");
	}

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
	Line     _input;
	uint32_t _capture = 0;

	void HandleIRQ() override;
};

// ---------------------------------------------------------------------------
// TIM_PulseMeasure — PWM input (period + width) via reset mode
// ---------------------------------------------------------------------------

/** @brief Measures an input PWM signal's period and pulse width via hardware reset mode. */
class TIM_PulseMeasure : public TIM
{
public:
	/**
	 * @brief Constructs a PWM-input (period + width) measurement driver.
	 * @param input  Pin + channel; channel must be CH1 or CH2.
	 */
	TIM_PulseMeasure(TIM_TypeDef* timx, Line input) :
		TIM(timx), _input(input)
	{
		if (input.pin.tim_base && input.pin.tim_base != reinterpret_cast<uint32_t>(timx))
			System::DebugTrap("TIM_PulseMeasure: pin belongs to wrong timer");
	}

	/** @brief Configures PWM-input mode and starts the timer. */
	SysInitStatus SetUp(uint32_t max_freq);

	/** @brief Returns the last measured pulse width (ticks). */
	inline uint32_t GetWidth()  const { return TIMx->CCR1; }
	/** @brief Returns the last measured signal period (ticks). */
	inline uint32_t GetPeriod() const { return TIMx->CCR2; }
	/** @brief Clears the last captured width/period. */
	inline void     Clear()           { TIMx->CCR1 = 0; TIMx->CCR2 = 0; }

private:
	Line _input;   ///< Capture input pin + channel.
};

// ---------------------------------------------------------------------------
// TIM_EncoderReader — hardware quadrature decoder for reading an encoder
// ---------------------------------------------------------------------------

/**
 * @brief Reads an incremental (quadrature) encoder entirely in hardware —
 * counterpart to TIM_EncoderGenerator, which fakes one for testing.
 *
 * Puts the timer in Encoder Interface mode (SMCR.SMS = 001/010/011): CNT
 * increments/decrements on CH1/CH2 edges according to the real quadrature
 * relationship between the two phases, with direction (CR1.DIR) set by
 * hardware from the actual signal — no ISR, no CPU cycles, at any speed the
 * encoder can produce.
 *
 * Requires CH1 and CH2 specifically (phase A/B) — Encoder Interface mode is
 * hardwired to TI1/TI2 in every STM32 timer that has it, the same way
 * External Clock Mode 1 (see TIM_HWCounter) is hardwired to ITRx; CH3/CH4
 * can't be used for this regardless of which pins are passed in, so the
 * constructor traps rather than silently mapping the wrong channels.
 *
 * @code
 *   TIM_EncoderReader enc(TIM3, TIM::_3::CH1::PA6, TIM::_3::CH2::PA7);
 *   enc.SetUp();                          // Mode::X4 by default: count every edge
 *   enc.Start();
 *
 *   uint32_t pos = enc.Count();
 *   bool     rev = enc.IsCountingDown();  // hardware-determined, read-only
 * @endcode
 */
class TIM_EncoderReader : public TIM
{
public:
	/** @brief Selects how many quadrature edges count per detent — matches SMCR.SMS's encoder submodes. */
	enum class Mode : uint32_t {
		X2_CH1 = 0b01,  ///< Counts edges on CH1 only; direction read from CH2's level.
		X2_CH2 = 0b10,  ///< Counts edges on CH2 only; direction read from CH1's level.
		X4     = 0b11,  ///< Counts edges on both channels — 4x resolution, the common default.
	};

	/**
	 * @brief Constructs an encoder reader on the CH1/CH2 pair.
	 * @param a  Phase A pin — must be on CH1 (Encoder Interface mode is
	 *           hardwired to TI1/TI2, i.e. CH1/CH2 — traps otherwise).
	 * @param b  Phase B pin — must be on CH2.
	 */
	TIM_EncoderReader(TIM_TypeDef* timx, Line a, Line b) :
		TIM(timx), _a(a), _b(b)
	{
		if ((a.pin.tim_base && a.pin.tim_base != reinterpret_cast<uint32_t>(timx)) ||
		    (b.pin.tim_base && b.pin.tim_base != reinterpret_cast<uint32_t>(timx)))
			System::DebugTrap("TIM_EncoderReader: pin belongs to wrong timer");
		if (a.GetChannel() != Channel::CH1 || b.GetChannel() != Channel::CH2)
			System::DebugTrap("TIM_EncoderReader: Encoder Interface mode requires CH1 (phase A) and CH2 (phase B) specifically");
	}

	/**
	 * @brief Configures Encoder Interface mode and starts counting.
	 * @param mode  Which edges to count; Mode::X4 (both channels) by default.
	 */
	SysInitStatus SetUp(Mode mode = Mode::X4);

	/**
	 * @brief True if hardware is currently counting down — the real
	 * quadrature direction as decoded from the two phases. Read-only: unlike
	 * TIM_HWCounter::SetDirection(), nothing here is a software command,
	 * hardware alone decides it in this mode.
	 */
	inline bool IsCountingDown() const { return (TIMx->CR1 & TIM_CR1_DIR) != 0; }

	/** @brief Raw position — increments/decrements per Mode, direction set by hardware. */
	inline uint32_t Count() const { return TIMx->CNT; }
	/** @brief Resets the position (CNT) back to 0. */
	inline void     Reset()       { TIMx->CNT = 0; }

private:
	Line _a, _b;   ///< Phase A (CH1) / Phase B (CH2) pins.
};

// ---------------------------------------------------------------------------
// TIM_HWCounter — free-running hardware pulse counter, no interrupts/CPU
// ---------------------------------------------------------------------------

/**
 * @brief Counts another timer's pulses entirely in hardware.
 *
 * Chains this timer's counter to a "master" timer's TRGO (Update event) via
 * the internal trigger (ITR) bus, in External Clock Mode 1: CNT ticks once
 * per master Update event with no ISR and no CPU cycles spent, at any pulse
 * rate the master can generate. Use this instead of TIM_StepGenerator's
 * built-in software counter when the step rate is high enough that an
 * interrupt per toggle would be a real CPU cost.
 *
 * SetUp() chains once, binds the completion callback for this counter's
 * whole lifetime, and enables the CC1 auto-stop unconditionally (works even
 * with @p on_complete = nullptr — same "nullptr = no callback, still
 * functions" convention as TIM_PeriodicIRQ::SetCompareIRQ()). Everything
 * after that is retargetable on the fly, without redoing the ITR chain or
 * touching the callback:
 *  - SetDirection() is a plain, independent command (e.g. reflecting which
 *    way a motion command is asking to move) — never touches any target.
 *  - StopAfter(ticks) arms a CC1 compare-match at "current CNT + ticks" *in
 *    whatever direction is currently set*; StopAT(ticks) arms one at the
 *    absolute value @p ticks instead, regardless of current CNT/direction.
 *    Both can be called at any time (running or not) to retarget an
 *    already-moving counter, e.g. when a new move command arrives.
 *  - CancelStopAfter() disarms the interrupt; a later StopAfter()/StopAT()
 *    call re-arms it, no need to call SetUp() again.
 *
 * Two ways to know when a move is done:
 *  - Free-running: just SetUp() + Start(), poll Count() yourself — the CC1
 *    auto-stop only fires once a StopAfter()/StopAT() target is armed; pair
 *    with TIM_StepGenerator::RunSteps() on the master if it has an RCR, or
 *    just Stop() the master yourself once Count() reaches your target.
 *  - StopAfter()/StopAT(): one IRQ for the whole move (on completion, via
 *    CH1's compare match — CH1 drives no pin, see TIM_PeriodicIRQ::
 *    SetCompareIRQ() for the same pin-free technique), not one per step.
 *    Use this for a master timer without a Repetition Counter, where
 *    TIM_StepGenerator::RunSteps() isn't available.
 *
 * @p ticks/Count() are in raw master-Update-event units — for a
 * TIM_StepGenerator master (PWM mode, one Update event per step), that's
 * already 1:1 with steps, no conversion needed at the call site. A
 * toggle-mode master would need one (2 Update events per pulse), but
 * TIM_HWCounter itself makes no assumption either way about what the
 * master's pulses represent.
 *
 * Requires a verified (master, slave) route in TIM::itr_table — currently
 * only STM32F4/F7 (TIM1/2/3/4/5/8) are populated; on STM32G0 both methods
 * return InitError until the table entries are confirmed against RM0444
 * (see the TODO in tim_defs.hpp).
 *
 * @code
 *   // Free-running, poll-based:
 *   TIM_StepGenerator step(TIM1, TIM::_1::CH1::PA8);
 *   step.SetUp(200000, false);   // no IRQ — see TIM_StepGenerator docs above
 *
 *   TIM_HWCounter counter(TIM3); // TIM3 free-run, unrelated to STEP's own pins
 *   if (counter.SetUp(step) != SysInitStatus::InitOK) return; // no ITR route for this pair
 *
 *   step.Start();
 *   uint32_t steps_done = counter.Count();  // 1:1 with steps (PWM-mode master)
 *
 *   // Auto-stop on a master without RCR (e.g. STEP on TIM3):
 *   TIM_StepGenerator step(TIM3, TIM::_3::CH1::PA6);
 *   step.SetUp(5000, false);         // no IRQ on the STEP timer itself
 *
 *   TIM_HWCounter counter(TIM1);     // any timer with a free ITR route to TIM3
 *   counter.SetUp(step, &OnMoveDone);   // chain once, bind the callback once
 *   counter.SetDirection(false);        // forward move — a separate, explicit command
 *   counter.StopAfter(800);             // 800 steps (PWM-mode master: 1:1 with ticks)
 *   step.Start();
 *
 *   // A new move command arrives mid-motion — just retarget, no re-chaining
 *   // and no need to pass the callback again:
 *   counter.StopAfter(300);             // 300 more steps from wherever CNT is now
 *   counter.StopAT(5000);               // ...or go to absolute position 5000 instead
 * @endcode
 */
class TIM_HWCounter : public TIM
{
public:
	explicit TIM_HWCounter(TIM_TypeDef* timx) : TIM(timx) {}

	/**
	 * @brief Chains this timer's counter to another timer's Update event via
	 * the internal trigger (ITR) bus, in External Clock Mode 1 — CNT then
	 * ticks once per master Update event with zero CPU involvement. Starts
	 * free-running immediately (poll Count()); SetDirection()/StopAfter()/
	 * StopAT() can be called at any point afterwards without redoing this
	 * chain or re-passing @p on_complete.
	 *
	 * Also enables the CC1 auto-stop unconditionally (regardless of whether
	 * @p on_complete is null), so StopAfter()/StopAT() work right away —
	 * CancelStopAfter() is the only thing that turns it back off.
	 *
	 * @param master        Timer whose TRGO drives this counter. Its
	 *                      EnableTriggerOutput() is called here.
	 * @param on_complete   Called from this counter's IRQ whenever a
	 *                      StopAfter()/StopAT() target is reached (nullptr =
	 *                      no callback, just the auto-stop of this counter
	 *                      and @p master). Bound once here for this object's
	 *                      whole lifetime — every future StopAfter()/StopAT()
	 *                      call reuses it, it can't be changed per-call.
	 * @return InitError if this (master, slave) pair has no verified ITR route
	 *         for the current MCU family (see TIM::itr_table in tim_defs.hpp).
	 */
	SysInitStatus SetUp(TIM& master, void (*on_complete)(void) = nullptr);

	/**
	 * @brief Sets which way CNT counts — up (each master Update event adds
	 * 1) or down (subtracts 1). A plain, explicit command: independent of
	 * StopAfter(), which just uses whatever direction is set at the time
	 * it's called to compute its target.
	 */
	inline void SetDirection(bool down) { TIMx->CR1 = (TIMx->CR1 & ~TIM_CR1_DIR) | (down << TIM_CR1_DIR_Pos); }
	/** @brief True if currently set to count down (see SetDirection()). */
	inline bool IsCountingDown() const { return (TIMx->CR1 & TIM_CR1_DIR) != 0; }

	/**
	 * @brief Arms a CC1 compare-match interrupt that stops both this counter
	 * and the master, and calls the @p on_complete bound in SetUp(), once
	 * CNT has moved exactly @p ticks further in whatever direction
	 * SetDirection() currently has set. CH1 is used purely as a pin-free
	 * comparator (same technique as TIM_PeriodicIRQ::SetCompareIRQ()) — no
	 * GPIO is touched.
	 *
	 * Requires SetUp() to have already chained this counter to its master —
	 * this only arms the target, it never redoes the ITR chain and never
	 * touches the callback, so it can be called again at any time (running
	 * or not) to retarget an already-moving counter, e.g. when a new move
	 * command arrives. Also re-enables CC1 if CancelStopAfter() had turned
	 * it off.
	 *
	 * @param ticks  Master Update events to wait for from the current CNT,
	 *               in whatever direction is currently set; 0 is invalid.
	 *               Raw ticks, same units as Count() — see the class doc for
	 *               converting from "steps".
	 * @return InitError if SetUp() hasn't been called yet, or if @p ticks
	 *         doesn't fit this counter's width (see PeriphInfo::arr_max).
	 */
	SysInitStatus StopAfter(uint32_t ticks);

	/**
	 * @brief Like StopAfter(), but arms the target at the absolute value
	 * @p ticks instead of "current CNT + ticks" — ignores both the current
	 * CNT and the current SetDirection(). Useful when tracking an absolute
	 * position (e.g. "go to 5000") rather than a relative move.
	 *
	 * @param ticks  Absolute CNT value to stop at; 0 is invalid. Raw ticks,
	 *               same units as Count().
	 * @return InitError if SetUp() hasn't been called yet, or if @p ticks
	 *         doesn't fit this counter's width (see PeriphInfo::arr_max).
	 */
	SysInitStatus StopAT(uint32_t ticks);

	/**
	 * @brief Disarms the CC1 auto-stop armed by StopAfter()/StopAT() (or by
	 * SetUp() itself). Direction (SetDirection()) and the bound callback are
	 * untouched — a later StopAfter()/StopAT() call re-enables CC1, no need
	 * to call SetUp() again.
	 */
	inline void CancelStopAfter() { IRQ_en(IRQ::CC1, DISABLE); }

	/** @brief Raw counter value — ticks once per master Update event, up or down per SetDirection(). */
	inline uint32_t Count() const { return TIMx->CNT; }
	/** @brief Resets the counter (CNT) back to 0. */
	inline void     Reset()       { TIMx->CNT = 0; }

private:
	TIM*  _master      = nullptr;             ///< Master timer set by SetUp(); auto-stopped from HandleIRQ() once a StopAfter()/StopAT() target is reached.
	void (*_on_complete)(void) = nullptr;      ///< Bound once in SetUp(); called from HandleIRQ() once a StopAfter()/StopAT() target is reached.

	void HandleIRQ() override;
};

#endif // TIM_HPP_
