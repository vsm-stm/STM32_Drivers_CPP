#ifndef ADC_HPP_
#define ADC_HPP_

#include "system.hpp"
#include "gpio.hpp"
#include "irq_registry.hpp"
#include "dma.hpp"
#include "tim.hpp"

// ---------------------------------------------------------------------------
// ADC_PIN — compile-time descriptor for one analog input.
//
// Defined at file scope (not nested in ADC_N): the channel tables inside the
// class body (adc_defs.hpp) need it as a complete type.
// ---------------------------------------------------------------------------

/** @brief Internal input behind an ADC channel (no GPIO pin). */
enum class ADC_INTERNAL : uint8_t {
	None,      ///< external pin
	VREFINT,   ///< internal voltage reference
	TEMP,      ///< temperature sensor (VSENSE)
	VBAT,      ///< VBAT through the internal divider
};

struct ADC_PIN {
	uint32_t     port     = 0;
	uint8_t      pin      = 0;
	uint8_t      channel  = 0;   ///< ADC channel number (0-18)
	uint32_t     adc_base = 0;   ///< Owning ADC peripheral base (for validation)
	ADC_INTERNAL internal = ADC_INTERNAL::None;

	constexpr bool IsValid() const { return port != 0 || internal != ADC_INTERNAL::None; }
};

// ---------------------------------------------------------------------------
// Maximum sequence length — the hardware limit: G0 has 16 external channels
// (bitmask mode; 8 in sequencer mode), F4/F7 16 ranks (SQ1-SQ16). Each slot
// costs 8 bytes of RAM per ADC_N object; define a smaller value before
// including adc.hpp (or project-wide) to save RAM.
// ---------------------------------------------------------------------------
#ifndef ADC_N_MAX_CHANNELS
#  define ADC_N_MAX_CHANNELS 16
#endif
static_assert(ADC_N_MAX_CHANNELS >= 1 && ADC_N_MAX_CHANNELS <= 16,
              "ADC_N_MAX_CHANNELS: 1..16 (F4/F7 SQ1-SQ16, G0 16 external channels)");

// ---------------------------------------------------------------------------
// ADC_N — single ADC peripheral driver
// ---------------------------------------------------------------------------

/**
 * @brief One ADC peripheral: a sequence of channels, converted
 *  - once, polled or by interrupt          — Start();
 *  - once into a buffer by DMA             — StartDMA();
 *  - on every trigger, circular DMA        — AttachTrig() / AttachExtTrig(), StartTrig();
 *  - continuously, circular DMA            — StartCont(buf, len);
 *  - continuously, one channel, no DMA     — StartCont(), latest value in GetData().
 * Stop() stops whatever is running.
 *
 * @code
 *   TIM_TriggerGenerator trg(TIM3, TIM_TriggerGenerator::Req::Adc::TIM3_TRGO);
 *   trg.SetUp(1000);                                  // 1000 sequences/s
 *   DMA_Sx dma(DMA1_Channel1, DMA_Sx::Req::Adc1::RX);
 *
 *   ADC_N adc(ADC1);
 *   adc.AddChannel(ADC_N::IN::PA0);
 *   adc.AddChannel(ADC_N::IN::PB0);
 *   adc.SetUp(ADC_N::SMPL::CYC_39_5);
 *   adc.AttachDMA(&dma);
 *   adc.AttachTrig(&trg);
 *   adc.SetDMACallback(on_buffer_full);
 *   adc.IRQ_en(ADC_N::IRQ::DMA_TC, ENABLE);         // interrupts only on request
 *   adc.StartTrig(buf, 2 * 8);                        // TIM3 + circular DMA
 *   ...
 *   adc.Stop();
 * @endcode
 */
class ADC_N : public IIRQHandler
{
public:
	// IN / _1 / _2 / _3 channel tables, SMPL enum, EXTSEL_EXTI11.
	// Access as: ADC_N::IN::PA1 (G0), ADC_N::_1::PA0 (F4/F7 ADC1), etc.
#include "adc_defs.hpp"

	/** @param adcx  Hardware ADC (ADC1, ADC2, ADC3). Channels are added with AddChannel(). */
	explicit ADC_N(ADC_TypeDef* adcx) : ADCx(adcx) {}

	ADC_N() = delete;
	ADC_N(const ADC_N&) = delete;
	ADC_N& operator=(const ADC_N&) = delete;

	// -----------------------------------------------------------------------
	// Channels
	// -----------------------------------------------------------------------

	/**
	 * @brief Appends an analog input to the conversion sequence.
	 *
	 * The order of the calls is the conversion order, and so the order of
	 * the samples in the DMA buffer. G0 exception: with any channel 15-18, or
	 * more than 8 channels, the sequencer only works in ascending channel
	 * order — SetUp() then returns InitError for any other order.
	 *
	 * Internal inputs (ADC_N::IN::VREFINT/TEMP/VBAT on G0,
	 * ADC_N::_1::VREFINT/TEMP/VBAT on F4/F7) are added the same way; SetUp()
	 * switches them on. They need a long sampling time (datasheet: several
	 * microseconds).
	 *
	 * The sampling setting belongs to the channel number, not to the rank: a
	 * channel added twice must use the same value.
	 *
	 * Takes effect at the next SetUp(). Calls System::DebugTrap() if @p pin
	 * belongs to another ADC (e.g. ADC_N::_2::PA0 on ADC1), the sequence is
	 * already ADC_N_MAX_CHANNELS long, or the same channel was added with a
	 * different sampling setting. An invalid ADC_PIN{} is ignored.
	 *
	 * @param pin   Input from ADC_N::IN (G0) or ADC_N::_N (F4/F7).
	 * @param sel   G0: which of the two sampling times given to SetUp() this
	 *              channel uses (SMPSELx).
	 * @param smpl  F4/F7: sampling time of this channel (SMPRx).
	 */
#if defined(STM32G0)
	void AddChannel(ADC_PIN pin, SMP_SEL sel = SMP_SEL::SMP1);
#elif defined(STM32F4) || defined(STM32F7)
	void AddChannel(ADC_PIN pin, SMPL smpl = SMPL::DEFAULT);
#endif

	/** @brief Empties the sequence, e.g. to build a new one before the next SetUp(). */
	inline void ClearChannels() { _ch_count = 0; }

	/** @brief Number of channels in the sequence (= samples per sequence). */
	inline uint8_t GetChannelCount() const { return _ch_count; }

	// -----------------------------------------------------------------------
	// Set-up
	// -----------------------------------------------------------------------

	/**
	 * @brief Enables the ADC clock, calibrates (G0), programs the sampling
	 *        times, the channel sequence, the internal inputs and the GPIO,
	 *        then enables the ADC.
	 *
	 * ADC clock: G0 — synchronous PCLK/2 (CFGR2.CKMODE = 01);
	 *            F4/F7 — PCLK2/4 (ADC->CCR.ADCPRE = 01, common to all ADCs).
	 *
	 * May be called again to reconfigure; anything running is stopped first.
	 * An attached DMA / trigger stays attached.
	 *
	 * G0 sequencer: with up to 8 channels, all in 0-14, the fully
	 * configurable mode (CHSELRMOD = 1) converts in AddChannel() order, like
	 * SQRx on F4/F7. Any channel 15-18, or more than 8 channels, needs the
	 * bitmask mode (ascending channel order only).
	 *
	 * Internal inputs: the enable bits in ADC->CCR (G0: VREFEN/TSEN/VBATEN,
	 * F4/F7: TSVREFE/VBATE) are set for the inputs in the sequence and
	 * cleared for the others (VBAT loads the battery while enabled).
	 *
	 * Sampling time: G0 — the two values SMP1/SMP2 given here, each channel
	 * picks one in AddChannel(); F4/F7 — per channel from AddChannel(),
	 * SetUp() takes no sampling argument.
	 *
	 * @param smp1  G0: sampling time SMP1.
	 * @param smp2  G0: sampling time SMP2.
	 * @return InitError if no channel was added, (G0) the order is not
	 *         possible, or (F4/F7 where both share IN18) TEMP and VBAT are
	 *         both in the sequence — with VBATE set, IN18 reads VBAT.
	 */
#if defined(STM32G0)
	SysInitStatus SetUp(SMPL smp1 = SMPL::DEFAULT, SMPL smp2 = SMPL::DEFAULT);
#elif defined(STM32F4) || defined(STM32F7)
	SysInitStatus SetUp();
#endif

	/**
	 * @brief Attaches a DMA stream for ADC -> memory transfers.
	 *
	 * Configures the stream: Per->Mem, 16-bit, MINC, source = ADCx->DR,
	 * request from the peripheral table. Must be called after SetUp().
	 *
	 * @return InitError if SetUp() was not called, @p dma is null, or the
	 *         stream is unknown / already in use.
	 */
	SysInitStatus AttachDMA(DMA_Sx* dma);

	/**
	 * @brief Attaches a trigger timer for StartTrig().
	 *
	 * @p trg must have been created with a Req::Adc request
	 * (TIM_TriggerGenerator trg(TIM3, TIM_TriggerGenerator::Req::Adc::TIM3_TRGO))
	 * and set up with its period (and compare point for a CCx request).
	 * StartTrig() starts it, Stop() stops it.
	 *
	 * @return InitError if @p trg is null, its request is not for the ADC,
	 *         or acquisition is running.
	 */
	SysInitStatus AttachTrig(TIM_TriggerGenerator* trg);

	/**
	 * @brief Attaches the external trigger EXTI line 11 for StartTrig()
	 *        (rising edge). Replaces an attached timer.
	 *
	 * The EXTI line itself (pin, edge) is configured by the application.
	 *
	 * @return InitError if acquisition is running.
	 */
	SysInitStatus AttachExtTrig();

	// -----------------------------------------------------------------------
	// Run
	// -----------------------------------------------------------------------

	/**
	 * @brief Converts the sequence once, without DMA (poll IsEOC() /
	 *        GetData(), or use the EOC/EOS callbacks).
	 */
	void Start();

	/**
	 * @brief Converts the sequence once into @p buf by DMA, then stops by
	 *        itself: IsBusy() becomes false; the DMA callback fires if
	 *        IRQ::DMA_TC is enabled.
	 *
	 * @param buf  Buffer of GetChannelCount() samples.
	 * @return Error if no DMA is attached or @p buf is null; Busy if
	 *         something is running.
	 */
	SysStatus StartDMA(uint16_t* buf);

	/**
	 * @brief Triggered acquisition: DMA circular over @p buf, the ADC
	 *        converts the sequence on every trigger, the trigger timer is
	 *        started last. Runs until Stop(); the DMA callback fires each
	 *        time @p len samples have been written.
	 *
	 * @param buf  Destination buffer (must stay valid until Stop()).
	 * @param len  Buffer length in samples: a non-zero multiple of
	 *             GetChannelCount(), at most 65535 (DMA counter width).
	 * @return Error if no DMA or trigger is attached, @p buf is null or
	 *         @p len is invalid; Busy if something is running.
	 */
	SysStatus StartTrig(uint16_t* buf, uint32_t len);

	/**
	 * @brief Continuous acquisition: the ADC converts the sequence back to
	 *        back (CONT = 1), DMA circular over @p buf. Runs until Stop(); the
	 *        DMA callback fires each time @p len samples have been written.
	 *
	 * @param buf  Destination buffer (must stay valid until Stop()).
	 * @param len  Buffer length in samples: a non-zero multiple of
	 *             GetChannelCount(), at most 65535 (DMA counter width).
	 * @return Error if no DMA is attached, @p buf is null or @p len is
	 *         invalid; Busy if something is running.
	 */
	SysStatus StartCont(uint16_t* buf, uint32_t len);

	/**
	 * @brief Continuous conversion of a single channel without DMA: GetData()
	 *        always returns the latest value. Runs until Stop().
	 *
	 * Unread results are simply overwritten — G0: OVRMOD = 1; F4/F7: without
	 * DMA and with EOCS = 0 an overrun is not detected at all. The OVR flag
	 * (G0) does not stop this mode.
	 *
	 * @return Error if the sequence is not exactly one channel or SetUp()
	 *         was not called; Busy if something is running.
	 */
	SysStatus StartCont();

	/**
	 * @brief Stops whatever is running: trigger timer, conversions, DMA.
	 *
	 * F4/F7 cannot abort a conversion in progress (no ADSTP); it completes.
	 */
	void Stop();

	/**
	 * @brief True while StartDMA() / StartTrig() / StartCont() runs.
	 *
	 * Without IRQ::DMA_TC a StartDMA() run is finished here: the hardware
	 * stops by itself, IsBusy() sees the DMA transfer-complete flag and
	 * returns to idle.
	 */
	bool IsBusy();

	/** @brief Returns the last converted value from ADCx->DR. */
	inline uint16_t GetData() const { return static_cast<uint16_t>(ADCx->DR); }

	/** @brief True if the EOC flag is set (conversion complete). */
#if defined(STM32G0)
	inline bool IsEOC() const { return (ADCx->ISR & ADC_ISR_EOC) != 0u; }
#elif defined(STM32F4) || defined(STM32F7)
	inline bool IsEOC() const { return (ADCx->SR & ADC_SR_EOC) != 0u; }
#endif

	// -----------------------------------------------------------------------
	// Interrupts and callbacks — callbacks run from HandleIRQ
	// -----------------------------------------------------------------------

	/**
	 * @brief Interrupt sources. All are off until enabled with IRQ_en().
	 *
	 * EOC/EOS/OVR map directly to IER (G0) / CR1 (F4/F7) interrupt-enable
	 * bits. F4/F7 has no separate EOS interrupt: EOS aliases EOCIE (EOCS = 0
	 * makes EOC an end-of-sequence event), so enabling/disabling either
	 * affects both. DMA_TC is the transfer-complete interrupt of the attached
	 * DMA stream (needs AttachDMA()).
	 */
	enum class IRQ : uint32_t {
#if defined(STM32G0)
		EOC = ADC_IER_EOCIE,
		EOS = ADC_IER_EOSIE,
		OVR = ADC_IER_OVRIE,
#elif defined(STM32F4) || defined(STM32F7)
		EOC = ADC_CR1_EOCIE,
		EOS = ADC_CR1_EOCIE,
		OVR = ADC_CR1_OVRIE,
#endif
		DMA_TC = 0x80000000u,   ///< not an ADC register bit (bit 31 is reserved in IER/CR1)
	};

	/**
	 * @brief Enables or disables one interrupt source.
	 *
	 * On first enable on a line: registers this object in IRQ_Registry and
	 * unmasks NVIC. On last disable: unregisters only this object; NVIC is
	 * masked only if no other handler remains on the line (F4/F7: ADC1/2/3
	 * share ADC_IRQn; G0 with COMP: shared with COMP; G0 DMA channels share
	 * lines). IRQ::DMA_TC is ignored while no DMA is attached.
	 */
	void IRQ_en(IRQ irq, FunctionalState en);

	/**
	 * @brief Called at end of conversion (EOC).
	 * F4/F7: EOCS = 0, so EOC fires once per sequence — same event as EOS.
	 */
	inline void SetEOCCallback(void (*cb)(void)) { _eoc_cb = cb; }

	/** @brief Called at end of the sequence (EOS). */
	inline void SetEOSCallback(void (*cb)(void)) { _eos_cb = cb; }

	/**
	 * @brief Called on overrun (IRQ::OVR). A running DMA acquisition is
	 *        stopped first; StartCont() without DMA keeps running.
	 */
	inline void SetOVRCallback(void (*cb)(void)) { _ovr_cb = cb; }

	/**
	 * @brief Called when the DMA has filled the buffer (IRQ::DMA_TC):
	 *        StartDMA — once, StartTrig / StartCont(buf, len) — every pass.
	 */
	inline void SetDMACallback(void (*cb)(void)) { _dma_cb = cb; }

	ADC_TypeDef* ADCx;

protected:
	/** @brief Run-time descriptor for one ADC peripheral. Defined in adc_defs.hpp Section B. */
	struct PeriphInfo {
		ADC_TypeDef*        periph;
		volatile uint32_t*  clk_reg;
		uint32_t            clk_bit;
		IRQn_Type           irqn;
		uint32_t            dma_req;   ///< DMAMUX request ID (G0) / DMA channel select (F4/F7)
	};
	static const PeriphInfo adc_table[];

	/** @brief One sequence entry; the owning ADC is checked in AddChannel() and not kept. */
	struct Channel {
		uint32_t     port;       ///< 0 for an internal input
		uint8_t      pin;
		uint8_t      channel;
		uint8_t      smp;        ///< G0: SMP_SEL; F4/F7: SMPL
		ADC_INTERNAL internal;
	};

	const PeriphInfo* _info   = nullptr;

	Channel  _ch[ADC_N_MAX_CHANNELS] = {};
	uint8_t  _ch_count        = 0;

	DMA_Sx*               _dma = nullptr;
	TIM_TriggerGenerator* _trg = nullptr;    ///< Attached trigger timer (AttachTrig)
	bool     _trig_set        = false;       ///< A trigger is attached (timer or EXTI11)
	uint8_t  _extsel          = 0;           ///< EXTSEL of the attached trigger

	/** @brief What is running; set by StartXxx(), back to Idle by Stop() (also from ISR). */
	enum class Mode : uint8_t {
		Idle,
		Dma,        ///< StartDMA: one sequence, one DMA pass
		Trig,       ///< StartTrig: circular DMA, on every trigger
		ContDma,    ///< StartCont(buf, len): circular DMA, continuous
		Cont,       ///< StartCont(): one channel, no DMA
	};
	volatile Mode _mode       = Mode::Idle;
	bool     _irq_registered  = false;       ///< Registered on the ADC IRQ line
	bool     _dma_registered  = false;       ///< Registered on the DMA IRQ line (IRQ::DMA_TC)

	void (*_eoc_cb)(void)     = nullptr;
	void (*_eos_cb)(void)     = nullptr;
	void (*_ovr_cb)(void)     = nullptr;
	void (*_dma_cb)(void)     = nullptr;

	void HandleIRQ() override;
};

#endif // ADC_HPP_
