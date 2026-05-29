/**
 * @file    uart.hpp
 * @brief   USART driver for STM32 (G0 / F4 / F7 families).
 *
 * Provides a thin C++ wrapper around the STM32 USART peripheral.
 * The driver is designed around static peripheral tables so that all
 * clock-enable addresses, bus frequencies and IRQ numbers are looked
 * up at run-time from a single `PeriphInfo` table instead of being
 * scattered across application code.
 *
 * Key design decisions
 * --------------------
 * - **Compile-time pin validation**: TX/RX pins are chosen from
 *   `USART::_N::TX` / `USART::_N::RX` structs (N = peripheral number)
 *   whose members carry the correct alternate-function index and the
 *   expected peripheral base address.  The constructor asserts
 *   (hard-fault trap) if a pin that belongs to a different USART instance
 *   is passed by mistake.
 *
 * - **Register abstraction via private accessors**: `TXD()`, `RXD()`,
 *   `Status_reg()` and `Clear_reg()` hide the register-name differences
 *   between F4 (DR / SR) and G0/F7 (TDR / RDR / ISR / ICR).  All
 *   higher-level code uses these accessors and compiles unchanged across
 *   families.
 *
 * - **IRQ integration**: The driver is compatible with the IRQ_Registry
 *   dispatch system.  Call `EnableNVIC_IRQ()` after registering an
 *   `IIRQHandler` with `IRQ_Registry::Register()`.
 *
 * Typical usage
 * -------------
 * @code
 *   USART debug(USART1, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7);
 *   debug.SetUp();
 *   debug.Send(buf, len, 1000);
 * @endcode
 */

#ifndef UART_H_
#define UART_H_

#include "system.hpp"
#include "rcc.hpp"
#include "gpio.hpp"
#include "irq_registry.hpp"
#include "dma.hpp"

/**
 * @class USART
 * @brief STM32 USART peripheral driver.
 *
 * One instance maps to one hardware USART/UART peripheral.
 * Instances must not be copied or moved — they own the peripheral
 * exclusively for the lifetime of the object.
 */
class USART : public IIRQHandler
{
public:
	// -----------------------------------------------------------------------
	// Compile-time pin tables
	//
	// Each member is a constexpr PIN that encodes: GPIO port base, pin
	// number, alternate-function index, and the owning USART base address.
	// Zero RAM footprint — values exist only in the compiler's constant pool.
	// -----------------------------------------------------------------------

#include "uart_defs.hpp"

	/**
	 * @brief Selects which USART interrupt source to enable or disable.
	 *
	 * Values map directly to the corresponding CR1 enable bits so the enum
	 * can be cast to uint32_t without a lookup table.
	 */
	enum class IRQ{
		TXE  = CR1_TXEIE,				///< Transmit data register empty.
		RXNE = CR1_RXNEIE,				///< Receive data register not empty.
		TC   = USART_CR1_TCIE,			///< Transmission complete.
		IDLE = USART_CR1_IDLEIE,		///< Idle line detected.
	#if defined(STM32G0)
		TXFIFO   = USART_CR3_TXFTIE,	///< IRQ for TX FIFO threshold (G0 only).
		RXFIFO   = USART_CR3_RXFTIE,	///< IRQ for RX FIFO threshold (G0 only).
	#endif
	};

	/**
	 * @brief Selects DMA direction(s) to enable or disable.
	 */
	enum class DMA{
		TXRX = USART_CR3_DMAR | USART_CR3_DMAT, ///< Both TX and RX DMA.
		TX   = USART_CR3_DMAT,                  ///< TX DMA only.
		RX   = USART_CR3_DMAR,                  ///< RX DMA only.
	};


	/**
	 * @brief Constructs a USART driver instance.
	 *
	 * The constructor validates at run-time that @p tx and @p rx pins
	 * belong to @p usartx.  A pin mismatch triggers a breakpoint trap
	 * and an infinite loop so the error is caught immediately in debug.
	 *
	 * @param usartx   Pointer to the hardware peripheral (e.g. USART1).
	 * @param baudrate Desired baud rate in bits per second.
	 * @param tx       TX pin selected from USART::_N::TX::<PXn>.
	 *                 Pass a default-constructed PIN{} to leave TX unconfigured.
	 * @param rx       RX pin selected from USART::_N::RX::<PXn>.
	 *                 Pass a default-constructed PIN{} to leave RX unconfigured.
	 */
	explicit USART(USART_TypeDef *usartx, uint32_t baudrate,
				   PIN tx = PIN{}, PIN rx = PIN{}) :
		USARTx(usartx),
		BaudRate(baudrate),
		_TX(tx),
		_RX(rx)
	{
		if (tx.IsValid() && tx.periph_base && tx.periph_base != (uint32_t)usartx)
			System::DebugTrap("USART: TX pin belongs to wrong peripheral");
		if (rx.IsValid() && rx.periph_base && rx.periph_base != (uint32_t)usartx)
			System::DebugTrap("USART: RX pin belongs to wrong peripheral");
	}

	USART() = delete;
	USART(const USART&) = delete;
	USART& operator=(const USART&) = delete;
	USART(USART&&) = delete;
	USART& operator=(USART&&) = delete;

	~USART(){};

	/**
	 * @brief Initialises the peripheral: enables the clock, configures
	 *        GPIO pins and programs the baud rate and control registers.
	 *
	 * Must be called once before any data transfer.  The peripheral info
	 * table is searched by comparing USARTx with each known peripheral
	 * base address; a missing entry returns SysInitStatus::Error.
	 *
	 * @return SysInitStatus::OK on success, SysInitStatus::Error if the
	 *         peripheral is not found in the driver table.
	 */
	SysInitStatus SetUp(FIFO fifo = FIFO::NO, FIFO_TH fifo_th_tx = FIFO_TH::NO, FIFO_TH fifo_th_rx = FIFO_TH::NO);

	/**
	 * @brief Enables or disables a USART interrupt source.
	 * On first enable: registers in IRQ_Registry and unmasks NVIC.
	 * On last disable: unregisters and masks NVIC.
	 */
	inline void IRQ_en(IRQ irq, FunctionalState en)
	{
		if (_info == nullptr) return;
	#if defined(STM32F4) || defined(STM32F7)
		if (en) USARTx->CR1 |=  static_cast<uint32_t>(irq);
		else    USARTx->CR1 &= ~static_cast<uint32_t>(irq);
		bool all_cleared = !(USARTx->CR1 & (CR1_TXEIE | CR1_RXNEIE | USART_CR1_TCIE | USART_CR1_IDLEIE));
	#elif defined(STM32G0)
		auto& reg = (irq == IRQ::TXE || irq == IRQ::RXNE || irq == IRQ::TC || irq == IRQ::IDLE)
					? USARTx->CR1 : USARTx->CR3;
		if (en) reg |=  static_cast<uint32_t>(irq);
		else    reg &= ~static_cast<uint32_t>(irq);
		bool all_cleared = !(USARTx->CR1 & (CR1_TXEIE | CR1_RXNEIE | USART_CR1_TCIE | USART_CR1_IDLEIE))
						&& !(USARTx->CR3 & (USART_CR3_TXFTIE | USART_CR3_RXFTIE));
	#endif
		if (en && !NVIC_GetEnableIRQ(_info->irq)) {
			IRQ_Registry::Register(_info->irq, this);
			NVIC_EnableIRQ(_info->irq);
		} else if (!en && all_cleared) {
			IRQ_Registry::Unregister(_info->irq);
			NVIC_DisableIRQ(_info->irq);
		}
	}

	/**
	 * @brief Enables or disables DMA requests for the peripheral.
	 * @param en  ENABLE to activate, DISABLE to deactivate.
	 * @param dma Which direction(s) to control (default: both TX and RX).
	 */
	inline void DMA_en(FunctionalState en, DMA dma = DMA::TXRX)
	{
		if(en)
			USARTx->CR3 |=   static_cast<uint32_t>(dma);
		else
			USARTx->CR3 &= ~(static_cast<uint32_t>(dma));
	}

#if defined(STM32F4)
	/**
	 * @brief Clears all status flags by writing 0 to SR (F4 only).
	 *
	 * On F4 the status register is cleared by writing; on G0/F7 use the
	 * overload that takes an ISR_FLAGS argument.
	 */
	inline void ClearFlags()
	{
		USARTx->SR = 0;
	}
#elif defined(STM32F7) || defined(STM32G0)
	/**
	 * @brief Selectable interrupt clear flags (G0 / F7).
	 *
	 * Values map directly to ICR bits.
	 */
	enum class ISR_FLAGS
	{
		PE   = USART_ICR_PECF,   ///< Parity error.
		FE   = USART_ICR_FECF,   ///< Framing error.
		ORE  = USART_ICR_ORECF,  ///< Overrun error.
		IDLE = USART_ICR_IDLECF, ///< Idle line.
		TC   = USART_ICR_TCCF    ///< Transmission complete.
	};

	/**
	 * @brief Clears a specific status flag via ICR (G0 / F7).
	 * @param flag Flag to clear (see USART::ISR_FLAGS).
	 */
	inline void ClearFlags(ISR_FLAGS flag)
	{
		USARTx->ICR = static_cast<uint32_t>(flag);
	}
#endif

	/**
	 * @brief Transmits a byte buffer in blocking mode.
	 *
	 * Polls the TXE flag for each byte.  Returns early with
	 * SysStatus::Timeout if the flag does not set within @p timeout
	 * milliseconds.
	 *
	 * @param data    Pointer to the data buffer to transmit.
	 * @param len     Number of bytes to send.
	 * @param timeout Maximum wait time per byte, in milliseconds.
	 * @return SysStatus::OK on success, SysStatus::Timeout on failure.
	 */
	SysStatus Send(uint8_t *data, uint32_t len, uint32_t timeout = 100);

	SysStatus Send_IRQ(uint8_t *data, uint32_t len);

	/**
	 * @brief Attaches DMA channels for TX and/or RX, configures them, and
	 *        registers this instance as the IRQ handler for the DMA lines.
	 *
	 * Calls DMA_Sx::SetUp() internally — do NOT call it separately beforehand.
	 * Direction, peripheral address (TDR/RDR), data width (Byte) and minc are
	 * set automatically.  Must be called after USART::SetUp().
	 *
	 * @param tx  DMA channel for transmit (nullptr to skip TX DMA).
	 * @param rx  DMA channel for receive  (nullptr to skip RX DMA).
	 */
	void AttachDMA(DMA_Sx* tx = nullptr, DMA_Sx* rx = nullptr);

	/** @brief Starts a DMA TX transfer. Returns Busy if one is already in progress. */
	SysStatus Send_DMA(uint8_t* data, uint32_t len);

	/** @brief Starts a DMA RX transfer. Enables IDLE detection for early termination. */
	SysStatus Receive_DMA(uint8_t* data, uint32_t len);


	/**
	 * @brief Receives a fixed number of bytes in blocking mode.
	 *
	 * Polls the RXNE flag for each byte.  Returns early with
	 * SysStatus::Timeout if the flag does not set within @p timeout
	 * milliseconds.
	 *
	 * @param data    Pointer to the receive buffer.
	 * @param len     Number of bytes to receive.
	 * @param timeout Maximum wait time per byte, in milliseconds.
	 * @return SysStatus::OK on success, SysStatus::Timeout on failure.
	 */
	SysStatus Receive(uint8_t *data, uint32_t len, uint32_t timeout = 100);

	SysStatus Receive_IRQ(uint8_t *data, uint32_t len);

	inline bool GetDataReceivedFlag() { return data_received; }
	inline uint32_t GetDataReceivedCount() {
		uint32_t count = data_received_count;
		data_received_count = 0;
		data_received = false;
		return count;
	}

	inline void ClearDataReceivedFlag() { data_received = false; data_received_count = 0; }

	inline bool GetOverflow() { return data_overflow; }
	inline uint32_t GetOverflowCount() {
		uint32_t count = data_overflow_count;
		data_overflow_count = 0;
		data_overflow = false;
		return count;
	}

	/**
	 * @brief Reprograms the baud rate without re-initialising the peripheral.
	 *
	 * Requires that SetUp() has already been called (so @p _info is valid).
	 * Has no effect if the peripheral was not found during SetUp().
	 *
	 * @param baud New baud rate in bits per second.
	 */
	inline void SetBaud(uint32_t baud) {
		if (_info != nullptr)
			USARTx->BRR = *_info->bus_clk / baud;
	}

	/** @brief Reserved for future parity configuration. Currently a no-op. */
	inline void SetParity(uint32_t parity) { (void)parity; }

	/**
	 * @brief Resets all control registers and unregisters the IRQ handler.
	 *
	 * Does not disable the peripheral clock.  Call SetUp() again to
	 * re-initialise.
	 */
	inline void DeInit(){
		if (_info) { IRQ_Registry::Unregister(_info->irq); NVIC_DisableIRQ(_info->irq); }
		USARTx->CR1 = 0;
		USARTx->CR2 = 0;
		USARTx->CR3 = 0;
	}

private:
	USART_TypeDef *USARTx; ///< Pointer to the peripheral register block.
	uint32_t BaudRate;     ///< Baud rate stored for reference after SetUp().

	/**
	 * @brief Compile-time descriptor for one USART peripheral instance.
	 *
	 * Stored in a `static const` table in uart.cpp.  SetUp() searches the
	 * table by peripheral pointer to fill @p _info.
	 */
	struct PeriphInfo {
		USART_TypeDef*      periph;    ///< Peripheral register block address.
		volatile uint32_t*  clk_reg;   ///< Clock-enable register (APBxENR).
		uint32_t            clk_bit;   ///< Bit mask within clk_reg.
		uint32_t const*     bus_clk;   ///< Pointer to the bus frequency variable.
		IRQn_Type           irq;       ///< NVIC IRQ number for this peripheral.
	};
	static const PeriphInfo usart_table[]; ///< Defined in uart.cpp.

	PIN _TX; ///< Configured TX pin (invalid if TX-only is not used).
	PIN _RX; ///< Configured RX pin (invalid if RX-only is not used).

	/** @brief Internal buffer descriptor used by Send() / Receive(). */
	typedef struct _data
	{
		uint8_t  *data_ptr; ///< Pointer to the active data buffer.
		uint16_t  size;     ///< Remaining byte count.
	} data_typedef;

	data_typedef tx_data; ///< Active TX transfer state.
	data_typedef rx_data; ///< Active RX transfer state.

	SysStatus tx_status{SysStatus::NotInit};
	SysStatus rx_status{SysStatus::NotInit};

	bool data_received{false};
	uint32_t data_received_count{0};
	bool data_overflow{false};
	uint32_t data_overflow_count{0};

	DMA_Sx* _dma_tx = nullptr;
	DMA_Sx* _dma_rx = nullptr;

	void HandleIRQ() override final;

	virtual void OnRxByte(uint8_t);
	virtual void OnTxEmpty();
	virtual void OnIdle();
	virtual void OnTC();
	virtual void OnDmaTxComplete();
	virtual void OnDmaRxComplete();

	const PeriphInfo* _info = nullptr; ///< Points into usart_table after SetUp().
};

#endif
