/**
 * @file    dma.hpp
 * @brief   DMA driver for STM32 (F4/F7 stream-based, G0 channel-based).
 *
 * The two families use fundamentally different DMA hardware:
 *  - F4/F7: DMA_Stream_TypeDef, registers CR/NDTR/PAR/M0AR, LISR/HISR flag pairs
 *  - G0:    DMA_Channel_TypeDef, registers CCR/CNDTR/CPAR/CMAR, single ISR/IFCR
 *
 * All register differences are hidden behind private inline accessors
 * (_CR, _NDTR, _PAR, _MAR) and per-family flag constants (FLAG_TC, FLAG_ALL).
 * Public API is identical on both families.
 *
 * Nested types (DIR, SIZE, IRQ, StreamSettings, DMAReq, Req) live inside
 * the DMA_Sx class — access them as DMA_Sx::DIR::To_Per, DMA_Sx::Req::Usart1::TX, etc.
 *
 * ============================================================================
 * Usage — standalone DMA transfer
 * ============================================================================
 *
 * 1. Construct DMA_Sx — choose one of the available constructor overloads.
 *
 *    STM32G0 — three overloads (DMAMUX1 allows any channel for any peripheral):
 *
 *    a) Channel + DMAReq from Req:: table  [recommended]
 *       Request ID is taken from the table and written to DMAMUX1 automatically.
 *       @code
 *         DMA_Sx dma_tx(DMA1_Channel4, DMA_Sx::Req::Usart1::TX);
 *         DMA_Sx dma_rx(DMA1_Channel5, DMA_Sx::Req::Usart1::RX);
 *       @endcode
 *
 *    b) Channel + raw DMAMUX1 request ID  [when Req:: table has no entry]
 *       @code
 *         DMA_Sx dma(DMA1_Channel3, 51u);  // USART1_TX request
 *       @endcode
 *
 *    c) Channel only  [request ID supplied later via StreamSettings::channel]
 *       @code
 *         DMA_Sx dma(DMA1_Channel3);
 *         // StreamSettings::channel must contain the DMAMUX1 request ID
 *       @endcode
 *
 *    STM32F4/F7 — two overloads (stream + CHSEL are hardware-fixed per peripheral):
 *
 *    a) DMAReq from Req:: table  [recommended — encodes stream + CHSEL]
 *       @code
 *         DMA_Sx dma_tx(DMA_Sx::Req::Usart1::TX);      // DMA2_Stream7, ch4
 *         DMA_Sx dma_tx(DMA_Sx::Req::Usart1::TX_alt);  // DMA2_Stream5, ch4
 *         DMA_Sx dma_rx(DMA_Sx::Req::Usart1::RX);      // DMA2_Stream2, ch4
 *       @endcode
 *
 *    b) Raw stream pointer  [CHSEL supplied via StreamSettings::channel]
 *       @code
 *         DMA_Sx dma_tx(DMA2_Stream7);
 *         // StreamSettings::channel must be set to 4 (CHSEL for USART1)
 *       @endcode
 *
 * 2. Call SetUp() with a StreamSettings descriptor.
 *    Per-address and count can be omitted here if they will be set dynamically.
 *    @code
 *      DMA_Sx::StreamSettings cfg;
 *      cfg.direction   = DMA_Sx::DIR::To_Per;
 *      cfg.data_size   = DMA_Sx::SIZE::Byte;
 *      cfg.minc        = true;   // increment memory address after each transfer
 *      cfg.pinc        = false;  // peripheral address stays fixed
 *      cfg.per_address = reinterpret_cast<uint32_t>(&USARTx->TDR);
 *      dma_tx.SetUp(cfg);
 *    @endcode
 *
 * 3. Start a transfer manually (when not managed by a peripheral driver):
 *    @code
 *      dma_tx.ClearFlags();
 *      dma_tx.SetMemAddr(reinterpret_cast<uint32_t>(buf));
 *      dma_tx.SetCount(len);
 *      dma_tx.Stream_EN(ENABLE);
 *    @endcode
 *
 * ============================================================================
 * Usage — DMA attached to a peripheral driver (e.g. USART)
 * ============================================================================
 *
 * The DMA_Sx object owns the hardware channel after successful SetUp().
 * A peripheral driver retains the object through AttachDMA(); do not share
 * one configured object between different peripherals.
 * The driver registers itself as the IRQ handler for the DMA interrupt line
 * and calls Stream_EN / ClearFlags / GetTC_Flag internally.
 *
 *    @code
 *      // 1. Declare driver objects (file or class scope)
 *      USART  uart(USART1, 115200, USART::TX::PB6, USART::RX::PB7);
 *      DMA_Sx dma_tx(DMA1_Channel4, DMA_Sx::Req::Usart1::TX);
 *      DMA_Sx dma_rx(DMA1_Channel5, DMA_Sx::Req::Usart1::RX);
 *
 *      // 2. Init UART, then attach DMA — AttachDMA configures the channels internally.
 *      uart.SetUp();
 *      uart.AttachDMA(&dma_tx, &dma_rx);
 *      uart.EnableNVIC_IRQ();
 *
 *      // 4. Transfer — the driver manages DMA internally
 *      uart.SendDMA(tx_buf, sizeof(tx_buf));
 *      uart.ReceiveDMA(rx_buf, sizeof(rx_buf));
 *    @endcode
 *
 * Notes
 * -----
 *  - For standalone transfers, call DMA SetUp() once. AttachDMA() in current
 *    peripheral drivers calls it internally; do not call it beforehand.
 *  - Repeated SetUp() is InitError until Release(), including on the same object.
 *  - SetUp() permits count=0 and address=0 for preparation without a start.
 *    SetCount() accepts 1..65535 elements; check its returned status.
 *  - SetMemAddr/SetPerAddr/SetCount require an owned, stopped channel. Stop
 *    through Stream_EN(DISABLE), which waits for EN=0 without a timeout.
 *  - On G0, DMA1 has 5 or 7 channels; some models also have DMA2 with 5 channels.
 *    Shared IRQ lines require enough IRQ_Registry slots for their consumers.
 *  - On F4/F7, stream + CHSEL are hardware-fixed; use the Req:: table to pick
 *    the correct combination. Alternate streams are named ::TX_alt / ::RX_alt.
 *  - The peripheral driver checks the route; DMA validates register fields.
 *  - DMA does not perform cache maintenance. The consumer calls System's cache
 *    operations at buffer handover; direct DMA callers do this themselves.
 *  - SetUp/Release and changes of ownership are performed in the main loop.
 *    Serialize accesses to the same resource externally; no internal locks.
 */

#ifndef DMA_H_
#define DMA_H_

#include "system.hpp"
#include "rcc.hpp"

/** @brief Per-stream/channel static descriptor stored in dma.cpp. */
struct dma_sets_typedef {
	DMA_TypeDef*        ctrl;      ///< DMA1 or DMA2
	volatile uint32_t*  isr;       ///< Status register (LISR/HISR on F4, ISR on G0)
	volatile uint32_t*  ifcr;      ///< Clear register  (LIFCR/HIFCR on F4, IFCR on G0)
	uint8_t             if_offset; ///< Bit offset of this stream's flag group
	IRQn_Type           irqn;      ///< NVIC IRQ number
	bool                used;      ///< Owned after SetUp(); cleared by the owner's Release()
};

// ---------------------------------------------------------------------------

class DMA_Sx {
private:
	// Register-name abstraction layer.
	// F4/F7 use DMA_Stream_TypeDef (CR/NDTR/PAR/M0AR).
	// G0    uses DMA_Channel_TypeDef (CCR/CNDTR/CPAR/CMAR).
#if defined(STM32F4) || defined(STM32F7)
	DMA_Stream_TypeDef* _stream;
	volatile uint32_t& _CR()   const { return _stream->CR;   }
	volatile uint32_t& _NDTR() const { return _stream->NDTR; }
	volatile uint32_t& _PAR()  const { return _stream->PAR;  }
	volatile uint32_t& _MAR()  const { return _stream->M0AR; }
	static constexpr uint32_t CR_EN   = DMA_SxCR_EN;
	static constexpr uint32_t CR_MINC = DMA_SxCR_MINC;
	static constexpr uint32_t CR_PINC = DMA_SxCR_PINC;
	static constexpr uint32_t CR_CIRC = DMA_SxCR_CIRC;
	static constexpr uint32_t CR_MSIZE_Msk = DMA_SxCR_MSIZE_Msk;
	static constexpr uint32_t CR_MSIZE_Pos = DMA_SxCR_MSIZE_Pos;
	static constexpr uint32_t CR_PSIZE_Msk = DMA_SxCR_PSIZE_Msk;
	static constexpr uint32_t CR_PSIZE_Pos = DMA_SxCR_PSIZE_Pos;
	// TCIF0 sits at bit 5 within each 6-bit group in LISR/HISR
	static constexpr uint32_t FLAG_TC  = DMA_LISR_TCIF0;
	static constexpr uint32_t FLAG_ALL = DMA_LIFCR_CTCIF0 | DMA_LIFCR_CHTIF0 |
	                                     DMA_LIFCR_CTEIF0 | DMA_LIFCR_CDMEIF0 |
	                                     DMA_LIFCR_CFEIF0;
#elif defined(STM32G0)
	DMA_Channel_TypeDef* _stream;
	volatile uint32_t& _CR()   const { return _stream->CCR;   }
	volatile uint32_t& _NDTR() const { return _stream->CNDTR; }
	volatile uint32_t& _PAR()  const { return _stream->CPAR;  }
	volatile uint32_t& _MAR()  const { return _stream->CMAR;  }
	static constexpr uint32_t CR_EN   = DMA_CCR_EN;
	static constexpr uint32_t CR_MINC = DMA_CCR_MINC;
	static constexpr uint32_t CR_PINC = DMA_CCR_PINC;
	static constexpr uint32_t CR_CIRC = DMA_CCR_CIRC;
	static constexpr uint32_t CR_MSIZE_Msk = DMA_CCR_MSIZE_Msk;
	static constexpr uint32_t CR_MSIZE_Pos = DMA_CCR_MSIZE_Pos;
	static constexpr uint32_t CR_PSIZE_Msk = DMA_CCR_PSIZE_Msk;
	static constexpr uint32_t CR_PSIZE_Pos = DMA_CCR_PSIZE_Pos;
	// TCIF1 sits at bit 1 within each 4-bit group in ISR
	static constexpr uint32_t FLAG_TC  = DMA_ISR_TCIF1;
	static constexpr uint32_t FLAG_ALL = 0xFu; // GIF+TCIF+HTIF+TEIF per channel
#endif

	dma_sets_typedef* _info    = nullptr;
	static constexpr uint32_t MAX_COUNT = 0xFFFFU;
	uint32_t          _preset_ch  = 0;
	bool              _has_preset = false;
	static int32_t GetStreamIndex(void* s);

public:
	/** @brief Data transfer direction. */
	enum class DIR {
		From_Per,   ///< Peripheral → memory
		To_Per,     ///< Memory → peripheral
		Mem_To_Mem  ///< Memory → memory on G0; SetUp rejects it on F4/F7.
	};

	/** @brief Transfer element width. */
	enum class SIZE {
		Byte,      ///< 8-bit
		Half_Word, ///< 16-bit
		Word       ///< 32-bit
	};

	/**
	 * @brief DMA interrupt sources.
	 *
	 * Values map directly to the corresponding CR bits so they can be
	 * cast to uint32_t and OR'd into the register without a lookup table.
	 * DME (direct-mode error) exists only on F4/F7.
	 */
	enum class IRQ {
#if defined(STM32F4) || defined(STM32F7)
		TC  = DMA_SxCR_TCIE,
		HTC = DMA_SxCR_HTIE,
		TE  = DMA_SxCR_TEIE,
		DME = DMA_SxCR_DMEIE,
#elif defined(STM32G0)
		TC  = DMA_CCR_TCIE,
		HTC = DMA_CCR_HTIE,
		TE  = DMA_CCR_TEIE,
#endif
	};

	/**
	 * @brief DMA transfer configuration passed to SetUp().
	 *
	 * The @p channel field has different semantics per family:
	 *  - F4/F7: DMA channel selection (CHSEL bits, 0–7)
	 *  - G0:    DMAMUX request ID fitting the selected device's DMAREQ_ID field
	 *           (6 or 7 bits); the peripheral driver checks the actual route.
	 * Count is in elements, 0..65535. Zero addresses/count are permitted here
	 * for deferred preparation. Nonzero addresses must match data_size alignment.
	 */
	struct StreamSettings {
		uint32_t channel{0};
		DIR      direction{DIR::From_Per};
		SIZE     data_size{SIZE::Byte};
		bool     minc{false};
		bool     pinc{false};
		bool     circ{false};
		uint32_t per_address{0};
		uint32_t mem_address{0};
		uint32_t count{0}; // Zero means preparation without a start.
	};

	// DMAReq and Req are defined in dma_requests.hpp and become nested types here.
	// Access as: DMA_Sx::DMAReq, DMA_Sx::Req::Usart1::TX, etc.
#include "dma_requests.hpp"

#if defined(STM32F4) || defined(STM32F7)
	// Stream + CHSEL come from DMAReq; no separate channel choice needed.
	explicit DMA_Sx(DMA_Stream_TypeDef* stream) : _stream(stream) {}
	explicit DMA_Sx(DMAReq req) : _stream(req.stream), _preset_ch(req.ch), _has_preset(true) {}
#elif defined(STM32G0)
	// Channel chosen by the user; request ID comes from DMAReq (DMAMUX1).
	explicit DMA_Sx(DMA_Channel_TypeDef* ch) : _stream(ch) {}
	explicit DMA_Sx(DMA_Channel_TypeDef* ch, DMAReq req) : _stream(ch), _preset_ch(req.ch), _has_preset(true) {}
	explicit DMA_Sx(DMA_Channel_TypeDef* ch, uint32_t req) : _stream(ch), _preset_ch(req),   _has_preset(true) {}
#endif

	DMA_Sx() = delete;
	DMA_Sx(const DMA_Sx&) = delete;
	DMA_Sx& operator=(const DMA_Sx&) = delete;

	/**
	 * @brief Initialises the DMA stream/channel from a StreamSettings descriptor.
	 * @return SysInitStatus::InitOK on success, InitError if the stream is
	 *         unknown, already in use, or the settings are invalid.
	 * @note Constructor request takes precedence over settings.channel.
	 *       Does not start DMA. Repeated setup requires prior Release().
	 *       Mem_To_Mem is currently unsupported on F4/F7 and returns InitError.
	 */
	SysInitStatus SetUp(const StreamSettings& settings);

	/**
	 * @brief Stop and reset this object's hardware resource, then release ownership.
	 * @return OK when released; NotInit if this object does not own a resource.
	 * @note Disable peripheral DMA requests and unregister its handler first.
	 *       Waits for EN=0 without a timeout. Does not disable a shared NVIC line
	 *       or the controller clock. Constructor stream/request selection is retained.
	 */
	SysStatus Release();

	/** @brief Set an aligned address while stopped; NotInit, Busy or Error on failure. */
	inline SysStatus SetMemAddr(uint32_t addr) {
		if (!_info) return SysStatus::NotInit;
		const uint32_t cr = _CR();
		if (cr & CR_EN) return SysStatus::Busy;
		if (!addr || (addr & ((1U << ((cr & CR_MSIZE_Msk) >> CR_MSIZE_Pos)) - 1U)))
			return SysStatus::Error;
		_MAR() = addr;
		return SysStatus::OK;
	}

	/** @brief Set an aligned address while stopped; NotInit, Busy or Error on failure. */
	inline SysStatus SetPerAddr(uint32_t addr) {
		if (!_info) return SysStatus::NotInit;
		const uint32_t cr = _CR();
		if (cr & CR_EN) return SysStatus::Busy;
		if (!addr || (addr & ((1U << ((cr & CR_PSIZE_Msk) >> CR_PSIZE_Pos)) - 1U)))
			return SysStatus::Error;
		_PAR() = addr;
		return SysStatus::OK;
	}

	/** @brief Set 1..65535 elements while stopped; rejected values leave the count unchanged. */
	inline SysStatus SetCount(uint32_t n) {
		if (!_info) return SysStatus::NotInit;
		if (_CR() & CR_EN) return SysStatus::Busy;
		if (!n || n > MAX_COUNT) return SysStatus::Error;
		_NDTR() = n;
		return SysStatus::OK;
	}

	inline uint32_t GetCount() { return _NDTR(); }

	/** @brief Clears all status flags for this stream/channel. */
	inline void ClearFlags() {
		*_info->ifcr = FLAG_ALL << _info->if_offset;
	}

	/** @brief Returns true if the Transfer Complete flag is set. */
	inline bool GetTC_Flag() {
		return (*_info->isr & (FLAG_TC << _info->if_offset)) != 0;
	}

	/** @brief Enables a DMA interrupt source and unmasks its NVIC line. */
	inline void Enable_IRQ(IRQ irq) {
		_CR() |= static_cast<uint32_t>(irq);
		if (!NVIC_GetEnableIRQ(_info->irqn))
			NVIC_EnableIRQ(_info->irqn);
	}

	/** @brief Disables a DMA interrupt source (does not touch NVIC). */
	inline void Disable_IRQ(IRQ irq) {
		_CR() &= ~static_cast<uint32_t>(irq);
	}

	inline void MINC(FunctionalState en) {
		if (en) _CR() |=  CR_MINC; else _CR() &= ~CR_MINC;
	}
	inline void PINC(FunctionalState en) {
		if (en) _CR() |=  CR_PINC; else _CR() &= ~CR_PINC;
	}
	inline void CIRC(FunctionalState en) {
		if (en) _CR() |=  CR_CIRC; else _CR() &= ~CR_CIRC;
	}

	/**
	 * @brief Enable the prepared transfer or stop and wait for EN=0.
	 * @note SetUp() must have succeeded. Stop has no timeout and can be used
	 *       from IRQ. The caller prepares valid addresses/count before starting.
	 */
	inline void Stream_EN(FunctionalState en) {
		if (en) {
			__DMB(); // Publish preceding buffer writes before starting DMA.
			_CR() |= CR_EN;
		} else {
			_CR() &= ~CR_EN;
			while (_CR() & CR_EN) {}
			__DMB(); // Keep subsequent buffer accesses after the confirmed stop.
		}
	}

	/** @brief Returns the NVIC IRQ number for this stream/channel, or -1 if not set up. */
	inline IRQn_Type GetIRQn() const {
		return _info ? _info->irqn : static_cast<IRQn_Type>(-1);
	}
};

#endif
