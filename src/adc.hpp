#ifndef ADC_HPP_
#define ADC_HPP_

#include "system.hpp"
#include "gpio.hpp"
#include "irq_registry.hpp"
#include "dma.hpp"
#include "tim.hpp"

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
		if (ch > 18) __builtin_unreachable();
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
			__builtin_unreachable();
		}
	}
};

// Implemented in the separate TIM work item; no substitute lives in ADC.
class TIM_TriggerGenerator;

/**
 * @brief One ADC: SetUp -> AddChannel -> optional DMA/trigger -> Start.
 *
 * Initialize System/RCC first. Setup, channel changes, attachment, Start,
 * Stop and DeInit are serialized main-loop operations. Callbacks run in IRQ
 * and notify the application; do not configure/restart the ADC from callbacks.
 * No internal locks, thread-context checks or configuration reservation exist.
 * System waits require a live SysTick; do not mask it during control methods.
 *
 * Start converts once; Start_DMA captures one sequence; StartTrig_DMA captures
 * on external events; StartCont_DMA captures continuously into circular DMA.
 * StartCont without DMA returns the latest single-channel value through GetData.
 * After a finite capture, call Start again. A running capture returns Busy.
 *
 * SetUp registers the ADC IRQ once; AttachDMA registers its DMA IRQ once.
 * IRQ_en changes sources only. Stop retains configuration and attachments;
 * DeInit removes this object's registrations and releases its DMA.
 *
 * F7 DMA uses System cache helpers. Buffers own all intersected 32-byte lines,
 * including padding. Transfer length need not be a multiple of 32 bytes.
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
 *   Samples adc(ADC1);
 *   alignas(32) static uint16_t values[16]; // Full cache line, two samples used.
 * #if defined(STM32G0)
 *   DMA_Sx dma(DMA1_Channel1, DMA_Sx::Req::Adc1::RX);
 * #else
 *   DMA_Sx dma(DMA_Sx::Req::Adc1::RX_DMA2_Stream0);
 * #endif
 *   if (adc.SetUp() == SysInitStatus::InitOK) {
 * #if defined(STM32G0)
 *       adc.AddChannel(ADC_N::IN::PA0_IN0);
 *       adc.AddChannel(ADC_N::IN::PB0_IN8);
 * #else
 *       adc.AddChannel(ADC_N::_1::PA0_IN0);
 *       adc.AddChannel(ADC_N::_1::PB0_IN8);
 * #endif
 *       if (adc.AttachDMA(&dma) == SysInitStatus::InitOK)
 *           adc.Start_DMA(values); // Check each AddChannel/start result as well.
 *   }
 *   // Main: consume values after ready, clear ready, then Start_DMA(values) again.
 * @endcode
 * See ADC.md for polling, circular capture, sampling and pending TIM/DMA work.
 */
class ADC_N : public IIRQHandler
{
public:
	using ADC_PIN = ::ADC_PIN;
#include "adc_defs.hpp"

	/// Bind ADC1/2/3 once; construction does not access hardware.
	explicit ADC_N(ADC_TypeDef* adcx) : ADCx(adcx) {}
	ADC_N() = delete;
	ADC_N(const ADC_N&) = delete;
	ADC_N& operator=(const ADC_N&) = delete;
	ADC_N(ADC_N&&) = delete;
	ADC_N& operator=(ADC_N&&) = delete;
	/// Normally static lifetime; call DeInit before destroying an initialized object.
	~ADC_N() = default;

	/**
	 * @brief Enable clock, initialize an empty sequence and register the ADC IRQ.
	 * G0: PCLK/2, regulator/calibration, two common sampling times SMP1/SMP2.
	 * F4/F7: PCLK2/4; smp1/smp2 are unused, sampling is selected per channel.
	 *
	 * ADCPRE is shared by ADC1/2/3 even in independent mode. Set up all ADCs
	 * before starting conversion; no common/multimode manager is introduced.
	 * @return InitOK or InitError for an unknown ADC, repeated setup, failed
	 *         handshake or IRQ registration. Repeated setup requires DeInit.
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
	 * @brief Configure/attach one DMA resource and register its IRQ.
	 * Validates the complete route using existing DMAReq/CanUseRequest.
	 * Configures half-word Per->Mem, MINC, ADC DR; do not SetUp DMA separately.
	 * TC/TE and F4/F7 DME sources are enabled, as in USART.
	 * @return InitError before SetUp, for null/wrong/owned DMA, repeated
	 *         attachment, Busy acquisition or a setup/registration failure.
	 * The DMA object outlives the attachment; only DeInit releases it.
	 */
	SysInitStatus AttachDMA(DMA_Sx* dma);

	/**
	 * @brief Attach an already configured TIM_TriggerGenerator.
	 * StartTrig_DMA arms ADC/DMA first, then starts this timer; Stop stops it.
	 * @return InitError before SetUp, for null or Busy acquisition.
	 * TIM_TriggerGenerator is a pending TIM dependency, not implemented in ADC.
	 */
	SysInitStatus AttachTrig(TIM_TriggerGenerator* trg);

	/// Select rising-edge EXTI11 instead of a timer; the application configures EXTI.
	SysInitStatus AttachExtTrig();

	/**
	 * @brief Convert the sequence once, without DMA.
	 * Poll IsBusy/IsEOC and read GetData, or enable EOC/EOS notifications.
	 * Only the latest DR sample is retained without DMA.
	 * @return NotInit, Busy, Error for an empty sequence, Timeout (G0), or OK.
	 */
	SysStatus Start();

	/**
	 * @brief Capture one sequence, then stop DMA and hand the buffer to CPU.
	 * @param buf At least GetChannelCount half-words, DMA-accessible and kept alive.
	 * TC is enabled automatically for completion; do not disable it during this run.
	 * @return NotInit, Busy, Error for buffer/DMA/count/preparation, Timeout, or OK.
	 * OnDmaComplete runs only after the DMA stop and cache invalidate.
	 * OnError discards the buffer without invalidate; partial data must not be read.
	 */
	SysStatus Start_DMA(uint16_t* buf);

	/**
	 * @brief Circular DMA capture, one sequence per trigger, until Stop.
	 * @param len Samples, 1..65535 and a multiple of GetChannelCount.
	 * The application ensures conversion and processing fit the trigger interval.
	 * At TC the buffer is invalidated and OnDmaComplete is called; DMA keeps running.
	 * @return As Start_DMA; Error also when no trigger is attached.
	 * Requires the separately planned TIM_TriggerGenerator for timer triggering.
	 */
	SysStatus StartTrig_DMA(uint16_t* buf, uint32_t len);

	/**
	 * @brief Continuous conversion with circular DMA until Stop.
	 * @param len Samples, 1..65535 and a multiple of GetChannelCount.
	 * Callback data is valid only until DMA overwrites it. Buffer length, sampling
	 * time and CPU processing determine the available time; the caller owns this.
	 * No buffering, copying, automatic pause or overtake detection is added.
	 * HT integration awaits the separate DMA flag-interface work; see ADC.md.
	 * @return Same status contract as Start_DMA.
	 */
	SysStatus StartCont_DMA(uint16_t* buf, uint32_t len);

	/// Continuous single-channel conversion without DMA; unread data is overwritten.
	SysStatus StartCont();

	/**
	 * @brief Stop timer, conversions and DMA; retain channels and attachments.
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
	inline bool IsEOC() const { return _info && (ADCx->ISR & ADC_ISR_EOC); }
#else
	inline bool IsEOC() const { return _info && (ADCx->SR & ADC_SR_EOC); }
#endif

	/// ADC peripheral sources plus the attached DMA's TC source.
	enum class IRQ : uint32_t {
#if defined(STM32G0)
		EOC = ADC_IER_EOCIE, EOS = ADC_IER_EOSIE, OVR = ADC_IER_OVRIE,
#else
		EOC = ADC_CR1_EOCIE, EOS = ADC_CR1_EOCIE, OVR = ADC_CR1_OVRIE,
#endif
		DMA_TC = 0x80000000u
	};
	/**
	 * @brief Change source bits only; registrations persist until DeInit.
	 * Usable for disabling a source in callbacks. F4/F7 EOC and EOS alias
	 * the same source and invoke OnEOS only; G0 has separate EOC/EOS events.
	 * DMA HT selection awaits the separate DMA API decision.
	 */
	void IRQ_en(IRQ irq, FunctionalState en);

	/// Cause mask delivered to OnError; several causes can occur together.
	enum class Error : uint32_t { Overrun = 1u, Dma = 2u };

	/**
	 * @brief Explicitly stop and release ADC/DMA IRQ registrations and DMA ownership.
	 * Call from main. Keeps other handlers and shared NVIC state intact.
	 * ADC clock is disabled; shared GPIO/DMA clocks and GPIO analog settings remain.
	 * Stops/detaches the externally configured trigger but does not destroy it.
	 * No automatic destructor cleanup. The same object may subsequently SetUp again.
	 */
	void DeInit();

protected:
	// Notifications only: hardware/status/cache handling is complete before entry.
	// Do not call control methods or change IRQ Registry from these callbacks.
#if defined(STM32G0)
	/// G0 only: one conversion completed. F4/F7 notify via OnEOS instead.
	virtual void OnEOC() {}
#endif
	virtual void OnEOS() {}
	/// Reserved for the agreed HT integration after the separate DMA API update.
	virtual void OnDmaHalf() {}
	/// One-shot: stopped stable buffer. Circular: caller must meet its read deadline.
	virtual void OnDmaComplete() {}
	/// Capture is stopped; partial DMA data is invalid and must not be read.
	virtual void OnError(uint32_t errors) { (void)errors; }

private:
	ADC_TypeDef* const ADCx;

	struct PeriphInfo {
		ADC_TypeDef* periph;
		volatile uint32_t* clk_reg;
		uint32_t clk_bit;
		IRQn_Type irqn;
#if defined(STM32G0)
		DMA_Sx::DMAReq dma_req[1];
#else
		DMA_Sx::DMAReq dma_req[2];
#endif
	};
	static const PeriphInfo adc_table[];
	const PeriphInfo* _info = nullptr;
	DMA_Sx* _dma = nullptr;
	TIM_TriggerGenerator* _trg = nullptr;
	uint16_t* _buf = nullptr;
	uint32_t _buf_bytes = 0;
	uint8_t _ch_count = 0;
	uint8_t _extsel = 0;
	bool _trig_set = false;
	bool _dma_tc_irq = false;

	enum class Mode : uint8_t { Single, Dma, Trig, ContDma, Cont };
	Mode _mode = Mode::Single;
	volatile SysStatus _status = SysStatus::NotInit;

	bool InSequence(uint8_t ch) const;
	SysStatus AddRank(uint8_t ch, CH_SMPL smpl);
	/// Replace only the hardware run fields (G0 requires disabled ADC).
	bool SetRunMode(uint32_t bits);
	/// Shared hardware start for the three public DMA capture modes.
	SysStatus StartDma(uint16_t* buf, uint32_t len, Mode mode);
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
	void HandleIRQ() override;
};

#endif // ADC_HPP_
