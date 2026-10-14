#ifndef ADC_HPP_
#define ADC_HPP_

#include "system.hpp"
#include "gpio.hpp"
#include "irq_registry.hpp"
#include "dma.hpp"
#include "tim.hpp"

// ---------------------------------------------------------------------------
// ADC_N — single ADC peripheral driver
// ---------------------------------------------------------------------------

/**
 * @brief One ADC peripheral.
 *
 * SetUp() does the basic set-up (clock, calibration, enable). Then the
 * sequence is built with AddChannel() — each call goes straight into the
 * registers — and converted
 *  - once, polled or by interrupt          — Start();
 *  - once into a buffer by DMA             — StartDMA();
 *  - on every trigger, circular DMA        — AttachTrig() / AttachExtTrig(), StartTrig();
 *  - continuously, circular DMA            — StartCont(buf, len);
 *  - continuously, one channel, no DMA     — StartCont(), latest value in GetData().
 * Stop() stops whatever is running. Configuration, StartXxx() and Stop()
 * run in thread mode with interrupts enabled. IRQ callbacks notify the
 * main loop; they do not call these control methods.
 * A hardware handshake failure requires a successful SetUp() before reuse.
 *
 * @code
 *   TIM_TriggerGenerator trg(TIM3, TIM_TriggerGenerator::Req::Adc::TIM3_TRGO);
 *   trg.SetUp(1000);                                  // 1000 sequences/s
 *   DMA_Sx dma(DMA1_Channel1, DMA_Sx::Req::Adc1::RX);
 *
 *   ADC_N adc(ADC1);
 *   adc.SetUp(ADC_N::SMPL::CYC_39_5);
 *   adc.AddChannel(ADC_N::IN::PA0);
 *   adc.AddChannel(ADC_N::IN::PB0);
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
	// ADC_PIN, IN / _1 / _2 / _3 channel tables, INTERNAL, SMPL / CH_SMPL,
	// EXTSEL_EXTI11. Access as: ADC_N::IN::PA1 (G0), ADC_N::_1::PA0 (F4/F7 ADC1).
#include "adc_defs.hpp"

	/** @param adcx  Hardware ADC (ADC1, ADC2, ADC3). */
	explicit ADC_N(ADC_TypeDef* adcx) : ADCx(adcx) {}

	ADC_N() = delete;
	ADC_N(const ADC_N&) = delete;
	ADC_N& operator=(const ADC_N&) = delete;

	// -----------------------------------------------------------------------
	// Set-up
	// -----------------------------------------------------------------------

	/**
	 * @brief Basic set-up: enables the ADC clock, calibrates (G0), enables
	 *        the ADC with an empty sequence. Channels are added afterwards.
	 *
	 * ADC clock: G0 — synchronous PCLK/2 (CFGR2.CKMODE = 01);
	 *            F4/F7 — PCLK2/4 (ADC->CCR.ADCPRE = 01, common to all ADCs).
	 *
	 * May be called again: anything running is stopped and the sequence is
	 * emptied (internal inputs switched off). An attached DMA / trigger stays
	 * attached.
	 *
	 * @param smp1  G0: sampling time SMP1 (channels pick SMP1/SMP2 in AddChannel()).
	 * @param smp2  G0: sampling time SMP2.
	 *              F4/F7: ignored — the sampling time is per channel.
	 * @return InitError if @p ADCx is not an ADC of this device, a
	 *         configuration is in progress, or (G0) the ADC does not answer a
	 *         handshake (ADDIS / ADCAL / ADRDY / CCRDY, about 1 ms each).
	 */
	SysInitStatus SetUp(SMPL smp1 = SMPL::DEFAULT, SMPL smp2 = SMPL::DEFAULT);

	// -----------------------------------------------------------------------
	// Channels — after SetUp(), while nothing runs
	// -----------------------------------------------------------------------

	/**
	 * @brief Appends an analog input to the conversion sequence and writes it
	 *        to the registers right away: sequence, sampling time, GPIO in
	 *        analog mode, internal input enable bit.
	 *
	 * The order of the calls is the conversion order, and so the order of
	 * the samples in the DMA buffer.
	 *
	 * F4/F7: up to 16 ranks (SQ1-SQ16), any order, a channel may repeat.
	 * G0: up to 8 channels in any order (fully configurable sequencer,
	 * channels 0-14). A channel 15-18 or a 9th channel switches the ADC to
	 * the bitmask sequencer (done here: ADC disabled, CHSELR rebuilt,
	 * re-enabled), which converts in ascending channel order only — so the
	 * channels must have been added in ascending order without repeats.
	 *
	 * The sampling setting belongs to the channel number, not to the rank: a
	 * channel added twice must use the same setting.
	 *
	 * Calls System::DebugTrap() if SetUp() was not called, something is
	 * running, @p pin belongs to another ADC (e.g. ADC_N::_2::PA0 on ADC1),
	 * the sequence is full, the same channel comes with a different sampling
	 * setting, or (G0) the bitmask sequencer is needed but the order is not
	 * ascending. An invalid ADC_PIN{} is ignored (Error).
	 *
	 * @param pin   Input from ADC_N::IN (G0) or ADC_N::_N (F4/F7).
	 * @param smpl  G0: which of the two sampling times given to SetUp() this
	 *              channel uses (SMPSELx); F4/F7: the channel's own sampling
	 *              time (SMPRx).
	 * @return OK; Timeout (G0) if the ADC did not answer CCRDY / ADRDY — the
	 *         channel is not added.
	 */
	SysStatus AddChannel(ADC_PIN pin, CH_SMPL smpl = CH_SMPL::DEFAULT);

	/**
	 * @brief Appends an internal input (VREFINT, TEMP, VBAT) to the sequence:
	 *        same rules as for a pin; instead of the GPIO its enable bit in
	 *        ADC->CCR is set (start-up delay included).
	 *
	 * F4/F7: ADC1 only. They need a long sampling time (datasheet: several
	 * microseconds). DebugTrap() as for a pin, plus: (F4/F7) not on ADC1, or
	 * TEMP and VBAT combined where both share IN18 — with VBATE set, IN18
	 * reads VBAT. Returns as for a pin.
	 */
	SysStatus AddChannel(INTERNAL in, CH_SMPL smpl = CH_SMPL::DEFAULT);

	/**
	 * @brief Empties the sequence in the registers and switches the internal
	 *        inputs off. DebugTrap() if something is running.
	 * @return OK; NotInit before SetUp(); Timeout (G0) if the ADC did not
	 *         answer (the channel count is 0 anyway: nothing can start).
	 */
	SysStatus ClearChannels();

	/** @brief Number of channels in the sequence (= samples per sequence). */
	inline uint8_t GetChannelCount() const { return _ch_count; }

	// -----------------------------------------------------------------------
	// DMA and triggers
	// -----------------------------------------------------------------------

	/**
	 * @brief Attaches a DMA stream for ADC -> memory transfers.
	 *
	 * Configures the stream: Per->Mem, 16-bit, MINC, source = ADCx->DR,
	 * request from the peripheral table. Must be called after SetUp(). No
	 * interrupt is enabled — IRQ_en(IRQ::DMA_TC, ENABLE) if wanted.
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
	 *        GetData(), or use the EOC/EOS callbacks). IsBusy() is true until
	 *        the sequence is done (G0: ADSTART cleared, F4/F7: EOC).
	 * Does nothing if called from IRQ, with IRQs masked, or while busy.
	 */
	void Start();

	/**
	 * @brief Converts the sequence once into @p buf by DMA, then stops by
	 *        itself: IsBusy() becomes false; the DMA callback fires if
	 *        IRQ::DMA_TC is enabled.
	 *
	 * @param buf  Buffer of GetChannelCount() samples.
	 * @return Error if no DMA is attached, no channel was added or @p buf is
	 *         null; Busy if something is running; Timeout (G0, all StartXxx
	 *         with a CFGR1 change) if the ADC did not answer ADDIS / ADRDY.
	 */
	SysStatus StartDMA(uint16_t* buf);

	/**
	 * @brief Triggered acquisition: DMA circular over @p buf, the ADC
	 *        converts the sequence on every trigger, the trigger timer is
	 *        started last. Runs until Stop(); the DMA callback fires each
	 *        time @p len samples have been written (IRQ::DMA_TC).
	 *
	 * @param buf  Destination buffer (must stay valid until Stop()).
	 * @param len  Buffer length in samples: a non-zero multiple of
	 *             GetChannelCount(), at most 65535 (DMA counter width).
	 * @return Error if no DMA or trigger is attached, no channel was added,
	 *         @p buf is null or @p len is invalid; Busy if something is running.
	 */
	SysStatus StartTrig(uint16_t* buf, uint32_t len);

	/**
	 * @brief Continuous acquisition: the ADC converts the sequence back to
	 *        back (CONT = 1), DMA circular over @p buf. Runs until Stop(); the
	 *        DMA callback fires each time @p len samples have been written
	 *        (IRQ::DMA_TC).
	 *
	 * @param buf  Destination buffer (must stay valid until Stop()).
	 * @param len  Buffer length in samples: a non-zero multiple of
	 *             GetChannelCount(), at most 65535 (DMA counter width).
	 * @return Error if no DMA is attached, no channel was added, @p buf is
	 *         null or @p len is invalid; Busy if something is running.
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
	 * @return Error if the sequence is not exactly one channel; Busy if
	 *         something is running.
	 */
	SysStatus StartCont();

	/**
	 * @brief Stops whatever is running: trigger timer, conversions, DMA.
	 * Returns with everything stopped (F4/F7: ADON cleared and set again,
	 * which aborts a conversion in progress; G0: ADSTP, then CFGR1 restored
	 * with the ADC disabled).
	 * @return Error from IRQ or with IRQs masked; OK on success;
	 *         Busy if a configuration is in progress (nothing stopped);
	 *         Timeout (G0) if the ADC did not answer ADSTP / ADDIS / ADRDY —
	 *         timer, DMA and the run state are stopped anyway.
	 */
	SysStatus Stop();

	/**
	 * @brief True while Start() / StartDMA() / StartTrig() / StartCont() runs.
	 *
	 * Start() and StartDMA() end by themselves. StartDMA() with IRQ::DMA_TC
	 * enabled is finished by the interrupt (and its callback); without it,
	 * IsBusy() sees the DMA transfer-complete flag and finishes the run.
	 * Hardware stop confirmation after IRQ/polling completion is deferred
	 * until the next control operation or explicit Stop() in main.
	 */
	bool IsBusy();

	/**
	 * @brief Returns the last converted value from ADCx->DR.
	 * F4/F7 during a Start() run: DR is read only once EOC is set (reading
	 * it clears EOC); before that the previous value is returned.
	 */
	uint16_t GetData();

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

	/**
	 * @brief True if the last DMA run ended on a DMA transfer error (the run is
	 *        stopped; the DMA callback is also called then).
	 */
	inline bool IsDMAError() const { return _dma_error; }

	ADC_TypeDef* ADCx;

protected:
	/** @brief Run-time descriptor for one ADC peripheral. Defined in adc_defs.hpp Section B. */
	struct PeriphInfo {
		ADC_TypeDef*        periph;
		volatile uint32_t*  clk_reg;
		uint32_t            clk_bit;
		IRQn_Type           irqn;
		DMA_Sx::RequestSet  dma_req;
	};
	static const PeriphInfo adc_table[];

	const PeriphInfo* _info   = nullptr;
	uint8_t  _ch_count        = 0;           ///< Channels in the sequence

	/** @brief Common part of both AddChannel(): next rank, sampling setting (caller holds Mode::Config). */
	bool AddRank(uint8_t ch, CH_SMPL smpl);   // false: G0 handshake timeout

	/** @brief Reserves Mode::Config and completes an IRQ stop with interrupts enabled. */
	SysStatus BeginConfig();
	/** @brief IsBusy() without the lock: notices a finished Start()/StartDMA(). Caller holds IrqLock. */
	bool RunActive();
	/** @brief Cancels the run and requests hardware stop without ADC waits. Caller holds IrqLock. */
	void RequestStop();
	/** @brief Confirms stop and prepares the idle ADC. Caller holds Mode::Config, IRQs enabled. */
	bool FinishStop();

	/** @brief True if @p ch is already in the sequence (G0: CHSELR, F4/F7: SQRx). */
	bool InSequence(uint8_t ch) const;

#if defined(STM32G0)
	bool     _seq_mode        = true;        ///< Fully configurable sequencer (CHSELRMOD = 1)
	bool     _ascending       = true;        ///< Channels added so far are strictly ascending
	uint8_t  _last_ch         = 0;           ///< Last channel added

	/** @brief Switches the sequencer mode (CHSELRMOD may only change with ADEN = 0); keeps the channels. */
	bool SetSeqMode(bool seq);

	// G0 handshakes use GetTick() with a 1 ms timeout. Called only from
	// thread mode with interrupts enabled; false means no hardware answer.

	/** @brief Sets ADEN until ADRDY (ADEN is ignored for a few ADC clocks after ADCAL / ADDIS). */
	bool Enable();

	/** @brief Stops conversions and clears ADEN through ADDIS. */
	bool Disable();

	/** @brief Writes CHSELR and waits for CCRDY. */
	bool WriteChselr(uint32_t chsel);

	/** @brief CFGR1 = (CFGR1 & ~clear) | set with the ADC disabled (RM0444: ADEN = 0 required). */
	bool WriteCfgr1(uint32_t clear, uint32_t set);
#endif

	/**
	 * @brief Start(): the hardware ends a single run by itself — notices it.
	 * @param sr F4/F7: the SR snapshot the caller goes on to process (an EOC
	 *           must not be cleared unseen); G0: unused, ADSTART is checked.
	 */
	void CheckSingleDone(uint32_t sr);

#if defined(STM32F4) || defined(STM32F7)
	uint16_t _last_data       = 0;           ///< Last DR value read by GetData()
#endif

	DMA_Sx*               _dma = nullptr;
	TIM_TriggerGenerator* _trg = nullptr;    ///< Attached trigger timer (AttachTrig)
	bool     _trig_set        = false;       ///< A trigger is attached (timer or EXTI11)
	uint8_t  _extsel          = 0;           ///< EXTSEL of the attached trigger

	/** @brief What is running; set by StartXxx(), back to Idle by Stop() (also from ISR). */
	enum class Mode : uint8_t {
		NotInit,    ///< SetUp has not completed, or a hardware handshake failed
		Idle,
		Config,     ///< A configuration / the set-up part of a start is in progress
		Single,     ///< Start: one sequence, no DMA
		Dma,        ///< StartDMA: one sequence, one DMA pass
		Trig,       ///< StartTrig: circular DMA, on every trigger
		ContDma,    ///< StartCont(buf, len): circular DMA, continuous
		Cont,       ///< StartCont(): one channel, no DMA
	};
	volatile Mode _mode       = Mode::NotInit;
	bool     _cancelled       = true;        ///< Suppresses late ADC events of a stopped/configured run.
	bool     _irq_registered  = false;       ///< Registered on the ADC IRQ line
	bool     _dma_registered  = false;       ///< Registered on the DMA IRQ line (IRQ::DMA_TC)
	uint16_t* _buf            = nullptr;     ///< DMA buffer of the running run (F7 cache maintenance)
	uint32_t _buf_bytes       = 0;
	volatile bool _dma_error  = false;       ///< Last DMA run ended on a transfer error

	/** @brief Remembers the DMA buffer, clears the error flag, invalidates it (F7). */
	void SetDmaBuffer(uint16_t* buf, uint32_t bytes);

	void (*_eoc_cb)(void)     = nullptr;
	void (*_eos_cb)(void)     = nullptr;
	void (*_ovr_cb)(void)     = nullptr;
	void (*_dma_cb)(void)     = nullptr;

	void HandleIRQ() override;
};

#endif // ADC_HPP_
