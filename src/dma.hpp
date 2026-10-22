/**
 * @file    dma.hpp
 * @brief   DMA driver for STM32 (F4/F7 stream-based, G0 channel-based).
 *
 * The two families use fundamentally different DMA hardware:
 *  - F4/F7: DMA_Stream_TypeDef, registers CR/NDTR/PAR/M0AR, LISR/HISR flag pairs
 *  - G0:    DMA_Channel_TypeDef, registers CCR/CNDTR/CPAR/CMAR, single ISR/IFCR
 *
 * All register differences are hidden behind private inline accessors
 * (_CR, _NDTR, _PAR, _MAR) and per-family flag constants (FLAG_HT, FLAG_TC, FLAG_ALL).
 * Public API is identical on both families.
 *
 * Nested types (DIR, SIZE, IRQ, StreamSettings, DMAReq) live inside
 * the DMA_Sx class — access them as DMA_Sx::DIR::To_Per, DMA_Sx::DMAReq, etc.
 *
 * ============================================================================
 * Usage — standalone DMA transfer
 * ============================================================================
 *
 * 1. Construct DMA_Sx — choose one of the available constructor overloads.
 *
 *    STM32G0 — three overloads (DMAMUX1 allows any channel for any peripheral):
 *
 *    a) Channel + DMAReq from peripheral DMA definitions  [recommended]
 *       Request ID is taken from the table and written to DMAMUX1 automatically.
 *       @code
 *         DMA_Sx dma_tx(DMA1_Channel4_BASE, USART::_1::DMA::TX);
 *         DMA_Sx dma_rx(DMA1_Channel5_BASE, USART::_1::DMA::RX);
 *       @endcode
 *
 *    b) Channel + raw DMAMUX1 request ID  [when peripheral DMA definitions has no entry]
 *       @code
 *         DMA_Sx dma(DMA1_Channel3_BASE, 51u);  // USART1_TX request
 *       @endcode
 *
 *    c) Channel only  [request ID supplied later via StreamSettings::channel]
 *       @code
 *         DMA_Sx dma(DMA1_Channel3_BASE);
 *         // StreamSettings::channel must contain the DMAMUX1 request ID
 *       @endcode
 *
 *    STM32F4/F7 — two overloads (stream + CHSEL are hardware-fixed per peripheral):
 *
 *    a) DMAReq from peripheral DMA definitions  [recommended — encodes stream + CHSEL]
 *       @code
 *         DMA_Sx dma_tx(USART::_1::DMA::TX_DMA2_Stream7); // CHSEL=4
 *         DMA_Sx dma_rx(USART::_1::DMA::RX_DMA2_Stream5); // CHSEL=4
 *         DMA_Sx dma_rx(USART::_1::DMA::RX_DMA2_Stream2); // CHSEL=4
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
 * USART/SPI/ADC/TIM/I2C own embedded DMA objects selected by construction.
 * Do not create a second DMA_Sx for their selected resources.
 * USART registers separate TX/RX DMA handlers and calls DMA methods internally.
 *
 *    @code
 *      // 1. Declare driver objects (file or class scope)
 *      constinit USART uart(USART::_1::instance, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7,
 *                 DMA1_Channel4_BASE, DMA1_Channel5_BASE); // G0: source selected by USART
 *
 *      // 2. UART SetUp configures the constructor-bound DMA internally.
 *      auto initialized = uart.SetUp();
 *      // Check initialized == InitOK; UART owns IRQ registration/NVIC.
 *
 *      // 3. Transfer — the driver manages DMA internally
 *      uart.Send_DMA(tx_buf, sizeof(tx_buf));
 *      uart.Receive_DMA(rx_buf, sizeof(rx_buf));
 *      // Check each start result. Consume RX after ready, clear ready explicitly,
 *      // then start the next Receive_DMA; the DMA resources stay attached.
 *    @endcode
 *
 * Notes
 * -----
 *  - For standalone transfers, call DMA SetUp() once. Peripheral drivers configure
 *    their owned/bound DMA themselves; do not call it beforehand.
 *  - Repeated SetUp() is InitError until Release(), including on the same object.
 *  - SetUp() permits count=0 and address=0 for preparation without a start.
 *    SetCount() accepts 1..65535 elements; check its returned status.
 *  - SetMemAddr/SetPerAddr/SetCount require an owned, stopped channel. Stop
 *    through Stream_EN(DISABLE), which waits for EN=0 without a timeout.
 *  - On G0, DMA1 has 5 or 7 channels; some models also have DMA2 with 5 channels.
 *    Shared IRQ lines require enough IRQ_Registry slots for their consumers.
 *  - On F4/F7, stream + CHSEL are hardware-fixed; use the peripheral DMA definitions to pick
 *    the correct combination. Names include controller and stream, for example
 *    USART::_3::DMA::TX_DMA1_Stream3 and USART::_3::DMA::TX_DMA1_Stream4.
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

// Change here and rebuild DMA and its consumers. Ordinary register updates by default.
#ifndef DMA_USE_ATOMIC
#define DMA_USE_ATOMIC 0
#endif

#if DMA_USE_ATOMIC
#define DMA_SET_BIT ATOMIC_SET_BIT
#define DMA_CLEAR_BIT ATOMIC_CLEAR_BIT
#define DMA_MODIFY_REG ATOMIC_MODIFY_REG
#else
#define DMA_SET_BIT SET_BIT
#define DMA_CLEAR_BIT CLEAR_BIT
#define DMA_MODIFY_REG MODIFY_REG
#endif

/** @brief Read-only stream/channel descriptor; ownership flags are separate in dma.cpp. */
struct dma_sets_typedef {
	DMA_TypeDef*        ctrl;      ///< DMA1 or DMA2
	volatile uint32_t*  isr;       ///< Status register (LISR/HISR on F4, ISR on G0)
	volatile uint32_t*  ifcr;      ///< Clear register  (LIFCR/HIFCR on F4, IFCR on G0)
	uint8_t             if_offset; ///< Bit offset of this stream's flag group
	IRQn_Type           irqn;      ///< NVIC IRQ number
};

// ---------------------------------------------------------------------------

class DMA_Sx {
public:
	// Normalized flag masks returned by GetIRQFlags(), independent of stream/channel.
#if defined(STM32F4) || defined(STM32F7)
	static constexpr uint32_t FLAG_HT = DMA_LISR_HTIF0;
	static constexpr uint32_t FLAG_TC = DMA_LISR_TCIF0;
	static constexpr uint32_t FLAG_ERRORS = DMA_LISR_TEIF0 | DMA_LISR_DMEIF0;
#elif defined(STM32G0)
	static constexpr uint32_t FLAG_HT = DMA_ISR_HTIF1;
	static constexpr uint32_t FLAG_TC = DMA_ISR_TCIF1;
	static constexpr uint32_t FLAG_ERRORS = DMA_ISR_TEIF1;
#endif

private:
	// Register-name abstraction layer.
	// F4/F7 use DMA_Stream_TypeDef (CR/NDTR/PAR/M0AR).
	// G0    uses DMA_Channel_TypeDef (CCR/CNDTR/CPAR/CMAR).
#if defined(STM32F4) || defined(STM32F7)
	uintptr_t _stream;
	DMA_Stream_TypeDef* Stream() const { return reinterpret_cast<DMA_Stream_TypeDef*>(_stream); }
	volatile uint32_t& _CR()   const { return Stream()->CR;   }
	volatile uint32_t& _NDTR() const { return Stream()->NDTR; }
	volatile uint32_t& _PAR()  const { return Stream()->PAR;  }
	volatile uint32_t& _MAR()  const { return Stream()->M0AR; }
	static constexpr uint32_t CR_EN   = DMA_SxCR_EN;
	static constexpr uint32_t CR_MINC = DMA_SxCR_MINC;
	static constexpr uint32_t CR_PINC = DMA_SxCR_PINC;
	static constexpr uint32_t CR_CIRC = DMA_SxCR_CIRC;
	static constexpr uint32_t CR_MSIZE_Msk = DMA_SxCR_MSIZE_Msk;
	static constexpr uint32_t CR_MSIZE_Pos = DMA_SxCR_MSIZE_Pos;
	static constexpr uint32_t CR_PSIZE_Msk = DMA_SxCR_PSIZE_Msk;
	static constexpr uint32_t CR_PSIZE_Pos = DMA_SxCR_PSIZE_Pos;
	static constexpr uint32_t FLAG_ALL = DMA_LIFCR_CTCIF0 | DMA_LIFCR_CHTIF0 |
	                                     DMA_LIFCR_CTEIF0 | DMA_LIFCR_CDMEIF0 |
	                                     DMA_LIFCR_CFEIF0;
#elif defined(STM32G0)
	uintptr_t _stream;
	DMA_Channel_TypeDef* Stream() const { return reinterpret_cast<DMA_Channel_TypeDef*>(_stream); }
	volatile uint32_t& _CR()   const { return Stream()->CCR;   }
	volatile uint32_t& _NDTR() const { return Stream()->CNDTR; }
	volatile uint32_t& _PAR()  const { return Stream()->CPAR;  }
	volatile uint32_t& _MAR()  const { return Stream()->CMAR;  }
	static constexpr uint32_t CR_EN   = DMA_CCR_EN;
	static constexpr uint32_t CR_MINC = DMA_CCR_MINC;
	static constexpr uint32_t CR_PINC = DMA_CCR_PINC;
	static constexpr uint32_t CR_CIRC = DMA_CCR_CIRC;
	static constexpr uint32_t CR_MSIZE_Msk = DMA_CCR_MSIZE_Msk;
	static constexpr uint32_t CR_MSIZE_Pos = DMA_CCR_MSIZE_Pos;
	static constexpr uint32_t CR_PSIZE_Msk = DMA_CCR_PSIZE_Msk;
	static constexpr uint32_t CR_PSIZE_Pos = DMA_CCR_PSIZE_Pos;
	static constexpr uint32_t FLAG_ALL = 0xFu; // GIF+TCIF+HTIF+TEIF per channel
#endif

	const dma_sets_typedef* _info = nullptr;
	static constexpr uint32_t MAX_COUNT = 0xFFFFU;
	uint32_t          _preset_ch  = 0;
	bool              _has_preset = false;
	static constexpr int32_t GetStreamIndex(uintptr_t base)
	{

#if defined(STM32F4) || defined(STM32F7)
		constexpr uintptr_t stride = DMA1_Stream1_BASE - DMA1_Stream0_BASE;
		constexpr uintptr_t dma1_base = DMA1_Stream0_BASE;
		constexpr uintptr_t dma1_end = DMA1_Stream7_BASE;
		constexpr uintptr_t dma2_base = DMA2_Stream0_BASE;
		constexpr uintptr_t dma2_end = DMA2_Stream7_BASE;

		if (base >= dma1_base && base <= dma1_end && (base - dma1_base) % stride == 0U)
			return (base - dma1_base) / stride;
		if (base >= dma2_base && base <= dma2_end && (base - dma2_base) % stride == 0U)
			return 8 + (base - dma2_base) / stride;

#elif defined(STM32G0)
		constexpr uintptr_t stride = DMA1_Channel2_BASE - DMA1_Channel1_BASE;
		constexpr uintptr_t ch1_base = DMA1_Channel1_BASE;
#if defined(DMA1_Channel7)
		constexpr uintptr_t ch1_end = DMA1_Channel7_BASE;
#else
		constexpr uintptr_t ch1_end = DMA1_Channel5_BASE;
#endif

		if (base >= ch1_base && base <= ch1_end && (base - ch1_base) % stride == 0U)
			return (base - ch1_base) / stride;
#if defined(DMA2_Channel1)
		constexpr uintptr_t ch2_base = DMA2_Channel1_BASE;
		constexpr uintptr_t ch2_end = DMA2_Channel5_BASE;
		if (base >= ch2_base && base <= ch2_end && (base - ch2_base) % stride == 0U)
			return 7 + (base - ch2_base) / stride;
#endif
#endif

		return -1;
	}

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

	// Only the common value type is defined here; named routes belong to peripheral defs.
#include "dma_requests.hpp"

#if defined(STM32F4) || defined(STM32F7)
	// Stream + CHSEL come from DMAReq; no separate channel choice needed.
	explicit DMA_Sx(DMA_Stream_TypeDef* stream) : _stream(reinterpret_cast<uintptr_t>(stream)) {}
	constexpr explicit DMA_Sx(DMAReq req)
		: _stream(req.stream),
		  _preset_ch(req.ch), _has_preset(true) {}
#elif defined(STM32G0)
	// Channel chosen by the user; request ID comes from DMAReq (DMAMUX1).
	explicit DMA_Sx(DMA_Channel_TypeDef* ch) : _stream(reinterpret_cast<uintptr_t>(ch)) {}
	explicit DMA_Sx(DMA_Channel_TypeDef* ch, DMAReq req) : _stream(reinterpret_cast<uintptr_t>(ch)), _preset_ch(req.ch), _has_preset(true) {}
	explicit DMA_Sx(DMA_Channel_TypeDef* ch, uint32_t req) : _stream(reinterpret_cast<uintptr_t>(ch)), _preset_ch(req),   _has_preset(true) {}
#endif

	/// Numeric selection supports constinit; an empty selection is optional DMA.
	constexpr explicit DMA_Sx(uintptr_t base) : _stream(base) {}
	constexpr DMA_Sx(uintptr_t base, uint32_t request)
		: _stream(base), _preset_ch(request), _has_preset(true) {}
#if defined(STM32G0)
	constexpr DMA_Sx(uintptr_t base, DMAReq request)
		: _stream(base), _preset_ch(request.ch), _has_preset(true) {}
#endif

	static constexpr bool ValidResource(uintptr_t base) { return GetStreamIndex(base) >= 0; }
#if defined(STM32F4) || defined(STM32F7)
	static constexpr bool RouteMatches(DMAReq req, uintptr_t owner, DIR dir) {
		return !(req.stream || req.ch || req.periph_base)
			|| (req.IsValid() && ValidResource(req.stream)
				&& req.periph_base == owner && req.direction == dir);
	}
	static constexpr bool SameResource(DMAReq a, DMAReq b) {
		return a.stream && a.stream == b.stream;
	}
#else
	static constexpr bool RouteMatches(uintptr_t channel, uintptr_t, DIR) {
		return !channel || ValidResource(channel);
	}
	static constexpr bool SameResource(uintptr_t a, uintptr_t b) {
		return a && a == b;
	}
#endif

	/** @brief Check a peripheral's DMAReq against this object's constructor selection.
	 * No hardware access or ownership change. With no preset, SetUp must receive req.ch.
	 * F4/F7 compare stream and any constructor preset; G0 compares the preset
	 * request but leaves actual channel existence to SetUp(). This is a route
	 * candidate check, not a reservation or proof the resource is free.
	 */
	inline bool CanUseRequest(DMAReq req) const {
		if (!req.IsValid() || (_has_preset && _preset_ch != req.ch)) return false;
	#if defined(STM32F4) || defined(STM32F7)
		return _stream == req.stream;
	#else
		return true; // DMAMUX permits the request on any existing channel.
	#endif
	}

	/// Whether a hardware stream/channel was selected; does not acquire it.
	constexpr bool HasSelection() const { return _stream != 0; }

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
	 * @param settings Numeric request/channel, direction, widths, addresses/count.
	 * count=0 and zero addresses allow deferred preparation without starting.
	 * Existing hardware ownership, including this same configured object, rejects
	 * another SetUp until Release(). Validation is about register fields/alignment;
	 * the peripheral consumer validates the real route via DMAReq.
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

	/**
	 * @brief Set 1..65535 elements while the owned stream/channel is stopped.
	 * @return NotInit without ownership, Busy while EN=1, Error for zero/out-of-range.
	 * Rejected calls do not write the counter. This is element count, not bytes;
	 * unlike deferred SetUp(count=0), a transfer preparation needs a nonzero count.
	 */
	inline SysStatus SetCount(uint32_t n) {
		if (!_info) return SysStatus::NotInit;
		if (_CR() & CR_EN) return SysStatus::Busy;
		if (!n || n > MAX_COUNT) return SysStatus::Error;
		_NDTR() = n;
		return SysStatus::OK;
	}

	/**
	 * @brief Prepare an owned, stopped DMA with one control-register update.
	 * @param addr Aligned memory address; caller guarantees DMA accessibility.
	 * @param n Element count in 1..65535.
	 * @param minc ENABLE increments memory; DISABLE repeats one element.
	 * @param irq_mask IRQ source bits to enable (bitwise OR of IRQ values).
	 * @pre Successful SetUp, EN=0 and peripheral requests disabled. The caller
	 *      guarantees all arguments; this low-level path performs no checks.
	 * Writes memory address/count, updates MINC and the supplied IRQ bits together,
	 * and clears this resource's flags. Other control bits are preserved. Does not
	 * touch NVIC, start DMA, wait or perform cache maintenance.
	 */
	inline void PrepareTransfer(uint32_t addr, uint32_t n, FunctionalState minc, uint32_t irq_mask) {
		_MAR() = addr;
		_NDTR() = n;
		DMA_MODIFY_REG(_CR(), CR_MINC, (minc == ENABLE ? CR_MINC : 0U) | irq_mask);
		ClearFlags();
	}

	/**
	 * @brief Read the current remaining hardware element count.
	 * @pre Successful SetUp. To derive a final packet length, stop DMA and wait
	 *      for EN=0 first; an active transfer can change this value immediately.
	 */
	inline uint32_t GetCount() { return _NDTR(); }

	/** @brief Clears all status flags for this stream/channel. */
	inline void ClearFlags() {
		*_info->ifcr = FLAG_ALL << _info->if_offset;
	}

	/** @brief Returns true if the Transfer Complete flag is set. */
	inline bool GetTC_Flag() {
		return (*_info->isr & (FLAG_TC << _info->if_offset)) != 0;
	}

	/** @brief Read this stream/channel's half-transfer flag without clearing it.
	 * @pre Successful SetUp. Reads the status flag regardless of IRQ::HTC enable.
	 */
	inline bool GetHT_Flag() const {
		return (*_info->isr & (FLAG_HT << _info->if_offset)) != 0;
	}

	/** @brief Clear only this stream/channel's HT flag; retain TC and errors.
	 * @pre Successful SetUp. Status and clear registers use the same bit positions.
	 * One write to the clear register; no read/modify/write or DMA stop.
	 */
	inline void ClearHT_Flag() {
		*_info->ifcr = FLAG_HT << _info->if_offset;
	}

	/** @brief Clear only this stream/channel's TC flag; retain HT and errors.
	 * @pre Successful SetUp. Status and clear registers use the same bit positions.
	 * One write to the clear register; no read/modify/write or DMA stop.
	 */
	inline void ClearTC_Flag() {
		*_info->ifcr = FLAG_TC << _info->if_offset;
	}

	/** @brief Read this stream's error bits, normalized to the first flag group.
	 * F4/F7: TEIF0, DMEIF0 (direct mode); G0: TEIF1. Zero when not configured.
	 */
	inline uint32_t GetErrorFlags() const {
		if (!_info) return 0;
		return (*_info->isr >> _info->if_offset) & FLAG_ERRORS;
	}

	/** @brief Read HT, TC and supported errors together in one ISR access.
	 * Decode this snapshot with FLAG_HT, FLAG_TC and FLAG_ERRORS.
	 * FEIF is excluded: the driver uses direct mode, not FIFO.
	 * No acknowledgement or stop; zero before SetUp.
	 * A stop requested after this snapshot may produce TC in the next IRQ.
	 */
	inline uint32_t GetIRQFlags() const {
		if (!_info) return 0;
		return (*_info->isr >> _info->if_offset) & (FLAG_HT | FLAG_TC | FLAG_ERRORS);
	}

	/**
	 * @brief Set the DMA source bit and optionally enable its NVIC line.
	 * @param enable_nvic false preserves NVIC enable/pending state; true (default)
	 *                    enables the line without clearing its pending state.
	 * @pre Successful SetUp and the consumer's handler already registered.
	 * Does not register a callback/object, clear flags or start a transfer.
	 */
	inline void Enable_IRQ(IRQ irq, bool enable_nvic = true) {
		DMA_SET_BIT(_CR(), static_cast<uint32_t>(irq));
		if (enable_nvic && !NVIC_GetEnableIRQ(_info->irqn))
			NVIC_EnableIRQ(_info->irqn);
	}

	/** @brief Disables a DMA interrupt source (does not touch NVIC). */
	inline void Disable_IRQ(IRQ irq) {
		DMA_CLEAR_BIT(_CR(), static_cast<uint32_t>(irq));
	}

	/// Change memory-address increment; configure while stopped and under the owner's control.
	inline void MINC(FunctionalState en) {
		if (en) DMA_SET_BIT(_CR(), CR_MINC); else DMA_CLEAR_BIT(_CR(), CR_MINC);
	}
	/// Change peripheral-address increment; ordinary peripheral registers keep it disabled.
	inline void PINC(FunctionalState en) {
		if (en) DMA_SET_BIT(_CR(), CR_PINC); else DMA_CLEAR_BIT(_CR(), CR_PINC);
	}
	/// Change circular mode while stopped; the consumer defines completion/buffer ownership.
	inline void CIRC(FunctionalState en) {
		if (en) DMA_SET_BIT(_CR(), CR_CIRC); else DMA_CLEAR_BIT(_CR(), CR_CIRC);
	}

	/**
	 * @brief Request a stop without waiting for the current transfer to finish.
	 * @pre Successful SetUp. The peripheral consumer controls its DMA requests.
	 * On F4/F7 a software stop produces TC after completion of the interrupted
	 * transfer. Do not publish the buffer or reconfigure DMA before that event.
	 * G0 abort/restart requires no transfer in progress on the DMA master port.
	 */
	inline void RequestStop() { DMA_CLEAR_BIT(_CR(), CR_EN); }

	/** @brief Read EN once; no wait and no flag acknowledgement.
	 * @pre Successful SetUp. On F4/F7 use TC for RX buffer handover after a stop.
	 */
	inline bool IsEnabled() const { return (_CR() & CR_EN) != 0U; }

	/**
	 * @brief Enable the prepared transfer or stop and wait for EN=0.
	 * @note SetUp() must have succeeded. Blocking stop is for main-loop code;
	 *       IRQ handlers use RequestStop() and finish on their hardware event.
	 *       The caller prepares valid addresses/count before starting.
	 *       Enabling has no argument/status validation and returns void by contract.
	 *       Barriers order RAM access around the DMA handover, but do not clean
	 *       or invalidate cache. The peripheral consumer uses System helpers.
	 */
	inline void Stream_EN(FunctionalState en) {
		if (en) {
			__DMB(); // Publish preceding buffer writes before starting DMA.
			DMA_SET_BIT(_CR(), CR_EN);
		} else {
			DMA_CLEAR_BIT(_CR(), CR_EN);
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
