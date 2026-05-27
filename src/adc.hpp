#ifndef ADC_HPP_
#define ADC_HPP_

#include "system.hpp"
#include "gpio.hpp"
#include "irq_registry.hpp"
#include "dma.hpp"

// ---------------------------------------------------------------------------
// ADC_PIN — compile-time descriptor for one analog input.
//
// Defined at file scope (not nested in ADC_N) so that it can be used as a
// default-argument type inside the ADC_N constructor without triggering
// GCC's "default member initializer required before end of enclosing class"
// restriction (which fires when a nested aggregate with DMIs is used as a
// default argument in the same enclosing class body).
// ---------------------------------------------------------------------------
struct ADC_PIN {
	uint32_t port     = 0;
	uint8_t  pin      = 0;
	uint8_t  channel  = 0;   ///< ADC channel number (0-18)
	uint32_t adc_base = 0;   ///< Owning ADC peripheral base (for validation)

	constexpr bool IsValid() const { return port != 0; }
};

// ---------------------------------------------------------------------------
// ADC_N — single ADC peripheral driver
// ---------------------------------------------------------------------------

class ADC_N : public IIRQHandler
{
public:
	// IN / _1 / _2 / _3 channel tables, TRIG enum, SMPL enum.
	// Access as: ADC_N::IN::PA1 (G0), ADC_N::_1::PA0 (F4 ADC1), etc.
#include "adc_defs.hpp"

	/**
	 * @brief Constructs an ADC driver with up to four analog input channels.
	 *
	 * Each ADC_PIN carries its port, pin number, channel index, and the base
	 * address of the ADC it belongs to.  The constructor validates that every
	 * supplied pin belongs to @p adcx and calls System::DebugTrap on mismatch.
	 *
	 * Pins can be given in any order — they are not positional.
	 * Unused slots default to ADC_PIN{} (IsValid() == false) and are ignored.
	 *
	 * Example (G0, single channel, software trigger):
	 * @code
	 *   ADC_N adc(ADC1, ADC_N::IN::PA1);
	 *   adc.SetUp();
	 *   adc.Start();
	 *   while (!adc.IsEOC()) {}
	 *   uint16_t val = adc.GetData();
	 * @endcode
	 *
	 * Example (G0, two-channel DMA scan triggered by TIM3):
	 * @code
	 *   DMA_Sx dma_adc(DMA1_Channel6, DMA_Sx::Req::Adc1::RX);
	 *   ADC_N  adc(ADC1, ADC_N::IN::PA0, ADC_N::IN::PB0);
	 *   adc.SetUp(ADC_N::TRIG::TIM3_TRGO, ADC_N::SMPL::CYC_39_5);
	 *   adc.SetDMACallback(on_done);
	 *   adc.AttachDMA(&dma_adc);
	 *   uint16_t buf[2];
	 *   adc.StartDMA(buf, 2);
	 * @endcode
	 *
	 * @param adcx   Pointer to the hardware ADC (ADC1, ADC2, ADC3).
	 * @param p0–p3  Analog input pins from ADC_N::IN (G0) or ADC_N::_N (F4).
	 */
	explicit ADC_N(ADC_TypeDef* adcx,
	               ADC_PIN p0 = {}, ADC_PIN p1 = {},
	               ADC_PIN p2 = {}, ADC_PIN p3 = {})
	    : ADCx(adcx), _ch{}, _ch_count(0)
	{
		for (const ADC_PIN& p : {p0, p1, p2, p3}) {
			if (!p.IsValid()) continue;
			if (p.adc_base && p.adc_base != reinterpret_cast<uint32_t>(adcx))
				System::DebugTrap("ADC_N: pin belongs to wrong ADC peripheral");
			_ch[_ch_count++] = p;
		}
	}

	ADC_N() = delete;
	ADC_N(const ADC_N&) = delete;
	ADC_N& operator=(const ADC_N&) = delete;

	/**
	 * @brief Enables the ADC clock, runs calibration, programs GPIO and
	 *        channel selection, then enables the ADC.
	 *
	 * Must be called before Start(), StartDMA() or IRQ_en().
	 * Does NOT call Start() — the caller does so explicitly.
	 *
	 * @param smpl  Sampling time applied to all channels.
	 */
	SysInitStatus SetUp(SMPL smpl = SMPL::CYC_12_5);

	/**
	 * @brief Overload that also configures an external hardware trigger.
	 *
	 * @param trig  Trigger source (rising-edge sensitivity).
	 * @param smpl  Sampling time applied to all channels.
	 */
	SysInitStatus SetUp(TRIG trig, SMPL smpl = SMPL::CYC_12_5);

	/**
	 * @brief Adds an extra analog input channel after construction.
	 *
	 * Applies the same peripheral validation as the constructor.
	 * Call before SetUp() so the channel is included in CHSELR / SQRx.
	 */
	void AddChannel(ADC_PIN pin);

	// -----------------------------------------------------------------------
	// Callbacks — invoked from HandleIRQ
	// -----------------------------------------------------------------------

	/** @brief Called at end of each single conversion (EOC). */
	inline void SetEOCCallback(void (*cb)(void)) { _eoc_cb = cb; }

	/** @brief Called at end of the full scan sequence (EOS). */
	inline void SetEOSCallback(void (*cb)(void)) { _eos_cb = cb; }

	/** @brief Called when the DMA transfer completes. */
	inline void SetDMACallback(void (*cb)(void)) { _dma_cb = cb; }

	// -----------------------------------------------------------------------
	// IRQ enable / disable
	// -----------------------------------------------------------------------

	/**
	 * @brief Maps directly to IER (G0) / CR1 (F4) interrupt-enable bits.
	 * Cast to uint32_t for direct register writes.
	 */
	enum class IRQ : uint32_t {
#if defined(STM32G0)
		EOC = ADC_IER_EOCIE,
		EOS = ADC_IER_EOSIE,
		OVR = ADC_IER_OVRIE,
#elif defined(STM32F4)
		EOC = ADC_CR1_EOCIE,
		OVR = ADC_CR1_OVRIE,
#endif
	};

	/**
	 * @brief Enables or disables one ADC interrupt source.
	 *
	 * On first enable: registers in IRQ_Registry and unmasks NVIC.
	 * On last disable: unregisters and masks NVIC.
	 */
	void IRQ_en(IRQ irq, FunctionalState en);

	// -----------------------------------------------------------------------
	// DMA
	// -----------------------------------------------------------------------

	/**
	 * @brief Attaches a DMA channel for ADC→memory transfers.
	 *
	 * Configures the stream: Per→Mem direction, 16-bit HalfWord, MINC,
	 * source = ADCx->DR, DMAMUX request from the peripheral table.
	 * Registers this object in IRQ_Registry for the DMA TC interrupt.
	 *
	 * Must be called after SetUp().
	 *
	 * @param dma  DMA stream (pre-constructed, not yet SetUp'd).
	 */
	void AttachDMA(DMA_Sx* dma);

	/**
	 * @brief Starts a one-shot DMA transfer: ADC → @p buf.
	 *
	 * Enables ADC_CFGR1_DMAEN, arms the DMA stream, then calls Start().
	 * Returns Busy if a transfer is already in progress.
	 *
	 * @param buf  Destination buffer (must stay valid until the callback fires).
	 * @param len  Number of 16-bit samples (= number of active channels per trigger).
	 */
	SysStatus StartDMA(uint16_t* buf, uint32_t len);

	/** @brief True while a DMA transfer is in progress. */
	inline bool IsDMABusy() const { return _dma_busy; }

	// -----------------------------------------------------------------------
	// Control
	// -----------------------------------------------------------------------

	/** @brief Starts a conversion (software trigger). */
	void Start();

	/** @brief Stops an ongoing conversion. */
	void Stop();

	/** @brief Returns the last converted value from ADCx->DR. */
	inline uint16_t GetData()  const { return static_cast<uint16_t>(ADCx->DR); }

	/** @brief True if the EOC flag is set (conversion complete). */
#if defined(STM32G0)
	inline bool IsEOC() const { return (ADCx->ISR & ADC_ISR_EOC) != 0u; }
#elif defined(STM32F4)
	inline bool IsEOC() const { return (ADCx->SR & ADC_SR_EOC) != 0u; }
#endif

	ADC_TypeDef* ADCx;

protected:
	/** @brief Run-time descriptor for one ADC peripheral. Defined in adc_defs.hpp Section B. */
	struct PeriphInfo {
		ADC_TypeDef*        periph;
		volatile uint32_t*  clk_reg;
		uint32_t            clk_bit;
		IRQn_Type           irqn;
		uint32_t            dma_req;   ///< DMAMUX request ID (G0) / DMA channel select (F4)
	};
	static const PeriphInfo adc_table[];

	const PeriphInfo* _info   = nullptr;

	void (*_eoc_cb)(void)     = nullptr;
	void (*_eos_cb)(void)     = nullptr;
	void (*_dma_cb)(void)     = nullptr;

	DMA_Sx*  _dma             = nullptr;
	bool     _dma_busy        = false;

	ADC_PIN  _ch[4]           = {};
	uint8_t  _ch_count        = 0;

	/** @brief Performs the table lookup and enables the peripheral clock. */
	SysInitStatus Init();

	/** @brief Internal SetUp implementation after Init(). */
	SysInitStatus SetUpInternal(TRIG trig, SMPL smpl);

	void HandleIRQ() override;
};

#endif // ADC_HPP_
