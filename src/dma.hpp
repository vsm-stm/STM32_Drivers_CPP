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
 */

#ifndef DMA_H_
#define DMA_H_

#include "system.hpp"
#include "rcc.hpp"
#include "dma_requests.hpp"

namespace DMA_Sx_ns {

/** @brief Data transfer direction. */
enum class DIR {
	From_Per,   ///< Peripheral → memory
	To_Per,     ///< Memory → peripheral
	Mem_To_Mem  ///< Memory → memory
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

/** @brief Per-stream/channel static descriptor stored in dma.cpp. */
struct dma_sets_typedef {
	DMA_TypeDef*        ctrl;      ///< DMA1 or DMA2
	volatile uint32_t*  isr;       ///< Status register (LISR/HISR on F4, ISR on G0)
	volatile uint32_t*  ifcr;      ///< Clear register  (LIFCR/HIFCR on F4, IFCR on G0)
	uint8_t             if_offset; ///< Bit offset of this stream's flag group
	IRQn_Type           irqn;      ///< NVIC IRQ number
	bool                used;      ///< Set by SetUp() — prevents double-init
};

/**
 * @brief DMA transfer configuration passed to SetUp().
 *
 * The @p channel field has different semantics per family:
 *  - F4/F7: DMA channel selection (CHSEL bits, 0–7)
 *  - G0:    DMAMUX peripheral request ID (0–63)
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
	uint32_t count{0};
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
	// TCIF1 sits at bit 1 within each 4-bit group in ISR
	static constexpr uint32_t FLAG_TC  = DMA_ISR_TCIF1;
	static constexpr uint32_t FLAG_ALL = 0xFu; // GIF+TCIF+HTIF+TEIF per channel
#endif

	dma_sets_typedef* _info = nullptr;
	uint32_t _preset_ch    = 0;
	bool     _has_preset   = false;
	static int32_t GetStreamIndex(void* s);

public:
#if defined(STM32F4) || defined(STM32F7)
	// Stream + CHSEL come from DMAReq; no separate channel choice needed.
	explicit DMA_Sx(DMA_Stream_TypeDef* stream) : _stream(stream) {}
	explicit DMA_Sx(DMAReq req) : _stream(req.stream), _preset_ch(req.ch), _has_preset(true) {}
#elif defined(STM32G0)
	// Channel chosen by the user; request ID comes from DMAReq (DMAMUX1).
	explicit DMA_Sx(DMA_Channel_TypeDef* ch) : _stream(ch) {}
	explicit DMA_Sx(DMA_Channel_TypeDef* ch, DMAReq req) : _stream(ch), _preset_ch(req.ch), _has_preset(true) {}
	explicit DMA_Sx(DMA_Channel_TypeDef* ch, uint32_t req) : _stream(ch), _preset_ch(req),    _has_preset(true) {}
#endif

	DMA_Sx() = delete;
	DMA_Sx(const DMA_Sx&) = delete;
	DMA_Sx& operator=(const DMA_Sx&) = delete;

	/**
	 * @brief Initialises the DMA stream/channel from a StreamSettings descriptor.
	 * @return SysInitStatus::InitOK on success, InitError if the stream is
	 *         unknown, already in use, or the settings are invalid.
	 */
	SysInitStatus SetUp(const StreamSettings& settings);

	inline SysStatus SetMemAddr(uint32_t addr) {
		if (!addr) return SysStatus::Error;
		_MAR() = addr;
		return SysStatus::OK;
	}

	inline SysStatus SetPerAddr(uint32_t addr) {
		if (!addr) return SysStatus::Error;
		_PAR() = addr;
		return SysStatus::OK;
	}

	inline SysStatus SetCount(uint32_t n) {
		if (!n) return SysStatus::Error;
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

	/** @brief Enables or disables the stream/channel (EN bit). */
	inline void Stream_EN(FunctionalState en) {
		if (en) _CR() |=  CR_EN; else _CR() &= ~CR_EN;
	}

	/** @brief Returns the NVIC IRQ number for this stream/channel, or -1 if not set up. */
	inline IRQn_Type GetIRQn() const {
		return _info ? _info->irqn : static_cast<IRQn_Type>(-1);
	}
};

} // namespace DMA_Sx_ns

#endif
