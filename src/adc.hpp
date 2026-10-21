#ifndef ADC_HPP_
#define ADC_HPP_

#include "system.hpp"
#include "gpio.hpp"
#include "irq_registry.hpp"
#include "dma.hpp"
#include "tim.hpp"

// Change here and rebuild this driver and its consumers.
#ifndef ADC_USE_ATOMIC
#define ADC_USE_ATOMIC 0
#endif

#if ADC_USE_ATOMIC
#define ADC_SET_BIT ATOMIC_SET_BIT
#define ADC_CLEAR_BIT ATOMIC_CLEAR_BIT
#define ADC_MODIFY_REG ATOMIC_MODIFY_REG
#else
#define ADC_SET_BIT SET_BIT
#define ADC_CLEAR_BIT CLEAR_BIT
#define ADC_MODIFY_REG MODIFY_REG
#endif

/**
 * @brief Immutable GPIO binding with its ADC channel and peripheral ownership.
 *
 * Defined before ADC_N so its consteval constructor is available when the
 * nested pin tables are initialized. ADC_N::ADC_PIN remains the public alias.
 * GPIOx_BASE, pin and channel/peripheral ranges are checked at compile time.
 * Package bonding remains the caller's responsibility; ADC_PIN{} is absent.
 */
struct ADC_PIN : public PIN {
	const uint8_t channel{}; ///< ADC channel number; fixed at construction.

	ADC_PIN() = default;
	consteval ADC_PIN(uintptr_t port, uint32_t pin, uint32_t ch, uint32_t adc_base)
		: PIN(port, pin, 0, adc_base), channel(static_cast<uint8_t>(ch))
	{
		if (ch > 18) STM32_CONFIG_ERROR("ADC_PIN: channel must be in 0..18");
		switch (adc_base) {
		case ADC1_BASE:
#if defined(ADC2_BASE)
		case ADC2_BASE:
#endif
#if defined(ADC3_BASE)
		case ADC3_BASE:
#endif
			break;
		default:
			STM32_CONFIG_ERROR("ADC_PIN: ADC is not available on this device");
		}
	}
};

/**
 * @brief One ADC with fixed conversion mode, DMA and trigger bindings.
 *
 * Initialize System/RCC first. Setup, channel changes, Start,
 * Stop and DeInit are serialized main-loop operations. Callbacks run in IRQ
 * and notify the application; do not configure/restart the ADC from callbacks.
	 * The caller serializes control calls with ADC/DMA IRQs; these two handlers
	 * must not preempt one another. No internal interrupt masking is added.
 * No internal locks, thread-context checks or configuration reservation exist.
 * System waits require a live SysTick; do not mask it during control methods.
 *
 * Start() converts without DMA; Start(buf, len) uses constructor-bound DMA.
 * Single without a trigger captures one sequence. Continuous and triggered
 * DMA captures are circular until Stop. Continuous cannot use a trigger.
 * Without DMA, DR retains only the latest conversion, not a sequence buffer.
 * After a finite capture, call Start again. A running capture returns Busy.
 *
 * SetUp registers separate ADC and DMA handlers once, without changing the
 * registry API. Neither IRQ waits for hardware. IRQ_en changes sources only.
 * Stop retains configuration and constructor bindings;
 * DeInit removes this object's registrations and releases its DMA.
 *
 * F7 DMA uses System cache helpers. Buffers own all intersected 32-byte lines,
	 * including padding. Without HT, transfer length need not be a multiple of
	 * 32 bytes. With HT, cached buffer halves occupy separate complete cache lines.
 * The application must finish reading circular data before DMA overwrites it;
 * neither a callback nor invalidate freezes a circular buffer.
 *
 * @code
 *   class Samples : public ADC_N {
 *   public:
 *       using ADC_N::ADC_N;
 *       volatile bool ready = false;
 *       volatile uint32_t errors = 0;
 *   protected:
 *       void OnDmaComplete() override { ready = true; }
 *       void OnError(uint32_t causes) override { errors = causes; }
 *   };
 *
 *   alignas(32) static uint16_t values[16]; // Full cache line, two samples used.
 * #if defined(STM32G0)
 *   constexpr ADC_N::DMAReq dma = DMA1_Channel1_BASE;
 * #else
 *   constexpr ADC_N::DMAReq dma = ADC_N::_1::DMA::RX_DMA2_Stream0;
 * #endif
 *   constinit Samples adc(ADC_N::_1::instance, ADC_N::Conversion::Single, dma);
 *   if (adc.SetUp() == SysInitStatus::InitOK) {
 * #if defined(STM32G0)
 *       adc.AddChannel(ADC_N::IN::PA0_IN0);
 *       adc.AddChannel(ADC_N::IN::PB0_IN8);
 * #else
 *       adc.AddChannel(ADC_N::_1::PA0_IN0);
 *       adc.AddChannel(ADC_N::_1::PB0_IN8);
 * #endif
 *       adc.Start(values, 2); // Check each AddChannel/start result as well.
 *   }
 *   // Main: consume values after ready, clear ready, then Start(values, 2) again.
 * @endcode
 * See ADC.md for polling, circular capture, cache-line ownership and sampling.
 */
class ADC_N : public IIRQHandler
{
public:
	/// Immutable conversion policy; scan order follows AddChannel calls.
	enum class Conversion : uint8_t { Single, Continuous };
	using ADC_PIN = ::ADC_PIN;
    /** Fixed device metadata from adc_defs.hpp; no runtime lookup. */
    struct Instance {
        uintptr_t periph;
        uintptr_t clk_reg;
        uint32_t clk_bit;
        IRQn_Type irqn;
#if defined(STM32G0)
        uint32_t dma_request;
#endif
    };
#include "adc_defs.hpp"

#if defined(STM32G0)
	using DMAReq = uintptr_t;
#else
	using DMAReq = DMA_Sx::DMAReq;
#endif

	/**
	 * @brief Bind ADC, conversion policy, optional DMA and trigger once.
	 * Construction performs no hardware access. SetUp acquires/registers DMA.
	 * A timer source requires the matching, externally configured TIM; EXTI11
	 * uses no TIM pointer. ADC does not configure timer frequency, TRGO or CCx.
	 * Start arms ADC/DMA before starting this TIM; Stop stops it. Bindings survive
	 * DeInit. Continuous plus any trigger is rejected during constant initialization.
	 * DMA is owned internally: F4/F7 named Req; G0 physical channel, fixed ADC request.
	 * ADC_USE_ATOMIC: ordinary (0, default) or ST atomic (1) control updates.
	 */
	constexpr explicit ADC_N(const Instance& instance, Conversion conversion,
		DMAReq dma = {}, TIM* timer = nullptr, Trigger source = Trigger::None)
		: _instance(instance), _conversion(conversion),
#if defined(STM32G0)
		  _dma(dma, instance.dma_request),
#else
		  _dma(dma),
#endif
		  _timer(timer), _trigger(source), _dma_irq(*this)
    {
        if (!DMA_Sx::RouteMatches(dma, instance.periph, DMA_Sx::DIR::From_Per))
            STM32_CONFIG_ERROR("ADC: invalid RX DMA route");
        if (conversion != Conversion::Single && conversion != Conversion::Continuous)
            STM32_CONFIG_ERROR("ADC: invalid conversion mode");
        if (conversion == Conversion::Continuous && source != Trigger::None)
            STM32_CONFIG_ERROR("ADC: continuous conversion cannot use a trigger");
        if ((source != Trigger::None && source != Trigger::EXTI11) != (timer != nullptr))
            STM32_CONFIG_ERROR("ADC: timer pointer is required only for a timer trigger");
    }
	ADC_N() = delete;
	ADC_N(const ADC_N&) = delete;
	ADC_N& operator=(const ADC_N&) = delete;
	ADC_N(ADC_N&&) = delete;
	ADC_N& operator=(ADC_N&&) = delete;
	/// Normally static lifetime; call DeInit before destroying an initialized object.
	~ADC_N() = default;

	/**
	 * @brief Enable clock, initialize an empty sequence and register ADC/DMA IRQs.
	 * G0: PCLK/2, regulator/calibration, two common sampling times SMP1/SMP2.
	 * F4/F7: PCLK2/4; smp1/smp2 are unused, sampling is selected per channel.
	 *
	 * ADCPRE is shared by ADC1/2/3 even in independent mode. Set up all ADCs
	 * before starting conversion; no common/multimode manager is introduced.
	 * Initializes constructor-bound DMA as half-word Per->Mem with MINC and ADC DR.
	 * Do not initialize or release that DMA independently before DeInit.
	 * @return InitOK or InitError for an unconfigured trigger timer,
	 *         repeated setup, failed handshake, DMA acquisition or registration.
	 * Failed setup releases only its own acquisitions. Repeated setup needs DeInit.
	 */
	SysInitStatus SetUp(SMPL smp1 = SMPL::DEFAULT, SMPL smp2 = SMPL::DEFAULT);

	/**
	 * @brief Append an input; successful calls define conversion/buffer order.
	 * PIN port/number and channel/peripheral ranges are compile-time checked.
	 * Package bonding and the selected hardware route remain caller obligations.
	 *
	 * F4/F7: up to 16 ranks, arbitrary order/repeats. G0: eight ranks of channels
	 * 0..14 in arbitrary order; a channel above 14 or a ninth rank switches to
	 * bitmask mode, which requires strictly ascending, non-repeated channels.
	 * Repeated channels must use the same sampling setting.
	 *
	 * @return NotInit, Busy, Error for a wrong/absent pin or an invalid sequence,
	 *         Timeout on a G0 handshake, otherwise OK.
	 * A handshake timeout requires successful ClearChannels or DeInit/SetUp
	 * before capture is started again; no automatic recovery is performed.
	 */
	SysStatus AddChannel(ADC_PIN pin, CH_SMPL smpl = CH_SMPL::DEFAULT);

	/**
	 * @brief Append VREFINT, TEMP or VBAT (ADC1 only), including source startup.
	 * The caller chooses sampling from the datasheet and actual ADC clock.
	 * The external-input default can be too short for internal sources.
	 * Delay_ms(1) lets the source settle; it does not extend sample acquisition.
	 * No sampling override or additional timing validation is performed.
	 * TEMP and VBAT cannot share their common channel in one sequence.
	 * @return Same status contract as the pin overload.
	 */
	SysStatus AddChannel(INTERNAL in, CH_SMPL smpl = CH_SMPL::DEFAULT);

	/**
	 * @brief Empty the sequence and disable ADC1 internal sources while idle.
	 * @return NotInit, Busy, Timeout (G0), or OK.
	 * A successful call also clears a previous channel-configuration timeout.
	 */
	SysStatus ClearChannels();
	inline uint8_t GetChannelCount() const { return _ch_count; }

	/**
	 * @brief Start the constructor-selected capture; no run-time mode switching.
	 * Without DMA call Start(). With DMA pass an aligned, live uint16_t buffer
	 * and 1..65535 samples, a multiple of GetChannelCount. Single/software capture
	 * requires exactly one sequence; Continuous or a trigger uses circular DMA.
	 * With DMA_HT enabled, len must be even; otherwise Start returns Error.
	 * Timer triggering arms ADC/DMA first and then starts the configured TIM.
	 * EXTI setup/edges remain the caller's responsibility.
	 * Finite DMA always uses TC. DMA_HT is explicitly enabled through IRQ_en.
	 * At HT the first half is returned; circular TC returns the second half when
	 * HT is enabled, otherwise the entire buffer. Finite TC returns all samples.
	 * Circular halves must be processed before DMA overwrites them. On F7 their
	 * cache lines must not overlap the half DMA is writing; see ADC.md.
	 * @return NotInit, Busy, Error for arguments/sequence/DMA preparation, Timeout
	 *         on a G0 handshake, otherwise OK. Error data must not be read.
	 */
	SysStatus Start(uint16_t* buf = nullptr, uint32_t len = 0);

	/**
	 * @brief Stop timer, conversions and DMA; retain channels and fixed bindings.
	 * DMA EN=0 is confirmed before cache/buffer handover. G0 ADC handshakes
	 * use System waits in main, never a hidden deferred configuration phase.
	 * @return NotInit, Timeout on a hardware handshake, Error on DMA failure, or OK.
	 * No completion callbacks are generated for an explicit stop.
	 */
	SysStatus Stop();

	/// Pure queries; no DMA stop, cache operation, flag clear or callback occurs.
	bool IsBusy() const;
	SysStatus GetStatus() const;

	/**
	 * @brief Read DR; do not use while DMA owns the ADC data register.
	 * F4/F7 returns the last read value while a one-shot conversion is pending,
	 * so reading DR cannot erase an unseen EOC. An observed result completes Start.
	 */
	uint16_t GetData();

#if defined(STM32G0)
	inline bool IsEOC() const { return ADCx && (ADCx->ISR & ADC_ISR_EOC); }
#else
	inline bool IsEOC() const { return ADCx && (ADCx->SR & ADC_SR_EOC); }
#endif

	/// ADC peripheral sources plus the constructor-bound DMA's HT/TC sources.
	enum class IRQ : uint32_t {
#if defined(STM32G0)
		EOC = ADC_IER_EOCIE, EOS = ADC_IER_EOSIE, OVR = ADC_IER_OVRIE,
#else
		EOC = ADC_CR1_EOCIE, EOS = ADC_CR1_EOCIE, OVR = ADC_CR1_OVRIE,
#endif
		DMA_HT = 0x40000000u, DMA_TC = 0x80000000u
	};
	/**
	 * @brief Change source bits only; registrations persist until DeInit.
	 * Usable for disabling a source in callbacks. F4/F7 EOC and EOS alias
	 * the same source and invoke OnEOS only; G0 has separate EOC/EOS events.
	 * DMA_HT is off initially. DMA_TC cannot be disabled for a finite capture.
	 * Configure HT while idle, before Start; preserve half-buffer layout until Stop.
	 * This changes no NVIC state. Serialize control calls and this object's ADC/DMA
	 * IRQs externally; the two handlers must not preempt each other.
	 */
	void IRQ_en(IRQ irq, FunctionalState en);

	/// Cause mask delivered to OnError; several causes can occur together.
	enum class Error : uint32_t { Overrun = 1u, Dma = 2u };

	/**
	 * @brief Explicitly stop and release ADC/DMA IRQ registrations and DMA ownership.
	 * Call from main. Keeps other handlers and shared NVIC state intact.
	 * ADC clock is disabled; shared GPIO/DMA clocks and GPIO analog settings remain.
	 * Stops the externally configured TIM but does not destroy or reconfigure it.
	 * Constructor bindings survive; a later SetUp uses the same DMA/TIM and policy.
	 * No automatic destructor cleanup. The same object may subsequently SetUp again.
	 */
	void DeInit();

protected:
	// Notifications follow source/status/cache handling; they do not finish it.
	// G0 OnError follows a stop command, not a confirmed successful DMA packet.
	// Do not call control methods or change IRQ Registry from these callbacks.
#if defined(STM32G0)
	/// G0 only: one conversion completed. F4/F7 notify via OnEOS instead.
	virtual void OnEOC() {}
#endif
	virtual void OnEOS() {}
	/// First half is ready; enabled explicitly through IRQ::DMA_HT before Start.
	virtual void OnDmaHalf() {}
	/// One-shot: stopped stable buffer. Circular: caller must meet its read deadline.
	virtual void OnDmaComplete() {}
	/// Error data is invalid. F4/F7 notify after DMA stop; G0 after its stop command.
	virtual void OnError(uint32_t errors) { (void)errors; }

private:
	const Instance& _instance;
	ADC_TypeDef* ADCx = nullptr; ///< Register cache and setup state.
	const Conversion _conversion;
	DMA_Sx _dma; ///< Empty constructor selection means capture without DMA.
	TIM* const _timer;
	const Trigger _trigger;
	uint16_t* _buf = nullptr;
	uint32_t _buf_bytes = 0;
	uint8_t _ch_count = 0;
	bool _dma_ht_irq = false;
	bool _dma_tc_irq = false;

	volatile SysStatus _status = SysStatus::NotInit;
	volatile uint32_t _errors = 0; // Retained until DMA confirms an error stop.
	class DmaIRQ final : public IIRQHandler {
	public:
		constexpr explicit DmaIRQ(ADC_N& adc) : _adc(adc) {}
		void HandleIRQ() override { _adc.HandleDmaIRQ(); }
	private:
		ADC_N& _adc;
	};
	DmaIRQ _dma_irq;
	void HandleDmaIRQ();
	/// Request an error stop without waiting; DMA IRQ finishes F4/F7 notification.
	void StopOnError(uint32_t errors);
	SysInitStatus SetUpDMA();
	bool IsRepeating() const {
		return _conversion == Conversion::Continuous || _trigger != Trigger::None;
	}

	bool InSequence(uint8_t ch) const;
	SysStatus AddRank(uint8_t ch, CH_SMPL smpl);
	/// Replace only the hardware run fields (G0 requires disabled ADC).
	bool SetRunMode(uint32_t bits);
	/// Main Stop/DeInit: stop, read errors once, clear; hand over only valid data.
	uint32_t StopDMA();

#if defined(STM32G0)
	bool _seq_mode = true;
	bool _ascending = true;
	uint8_t _last_ch = 0;
	bool Enable();
	bool Disable();
	bool WriteChselr(uint32_t chsel);
	bool WriteCfgr1(uint32_t clear, uint32_t set);
	bool SetSeqMode(bool seq);
#else
	uint16_t _last_data = 0;
#endif
	void HandleIRQ() override final;
};

#endif // ADC_HPP_
