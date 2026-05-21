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

#if defined(STM32G0)
	struct _1 {
		struct TX {
			static constexpr PIN PA9  = { GPIOA_BASE,  9, 0, USART1_BASE };
			static constexpr PIN PB6  = { GPIOB_BASE,  6, 0, USART1_BASE };
		};
		struct RX {
			static constexpr PIN PA10 = { GPIOA_BASE, 10, 0, USART1_BASE };
			static constexpr PIN PB7  = { GPIOB_BASE,  7, 0, USART1_BASE };
		};
	};
	struct _2 {
		struct TX {
			static constexpr PIN PA2  = { GPIOA_BASE,  2, 1, USART2_BASE };
			static constexpr PIN PA14 = { GPIOA_BASE, 14, 1, USART2_BASE };
		};
		struct RX {
			static constexpr PIN PA3  = { GPIOA_BASE,  3, 1, USART2_BASE };
			static constexpr PIN PA15 = { GPIOA_BASE, 15, 1, USART2_BASE };
		};
	};
#ifdef USART3_BASE
	struct _3 {
		struct TX {
			static constexpr PIN PB8  = { GPIOB_BASE,  8, 4, USART3_BASE };
			static constexpr PIN PC4  = { GPIOC_BASE,  4, 0, USART3_BASE };
			static constexpr PIN PC10 = { GPIOC_BASE, 10, 0, USART3_BASE };
		};
		struct RX {
			static constexpr PIN PB9  = { GPIOB_BASE,  9, 4, USART3_BASE };
			static constexpr PIN PC5  = { GPIOC_BASE,  5, 0, USART3_BASE };
			static constexpr PIN PC11 = { GPIOC_BASE, 11, 0, USART3_BASE };
		};
	};
#endif
#ifdef UART4_BASE
	struct _4 {
		struct TX {
			static constexpr PIN PA0  = { GPIOA_BASE,  0, 0, UART4_BASE };
			static constexpr PIN PC10 = { GPIOC_BASE, 10, 0, UART4_BASE };
		};
		struct RX {
			static constexpr PIN PA1  = { GPIOA_BASE,  1, 0, UART4_BASE };
			static constexpr PIN PC11 = { GPIOC_BASE, 11, 0, UART4_BASE };
		};
	};
#endif

	enum class FIFO{
		NO = 0, 
		EN = USART_CR1_FIFOEN
	};

	enum class FIFO_TH{
		NO = 0,
		TH_1_8 = 0b000,
		TH_1_4 = 0b001,
		TH_1_2 = 0b010,
		TH_3_4 = 0b011,
		TH_7_8 = 0b100,
		all	   = 0b111
	};

	static constexpr uint32_t FIFO_TH_TX_Pos = USART_CR3_TXFTCFG_Pos;
	static constexpr uint32_t FIFO_TH_RX_Pos = USART_CR3_RXFTCFG_Pos;

#elif defined(STM32F4) || defined(STM32F7)
	struct _1 {
		struct TX {
			static constexpr PIN PA9  = { GPIOA_BASE,  9, 7, USART1_BASE };
			static constexpr PIN PB6  = { GPIOB_BASE,  6, 7, USART1_BASE };
		};
		struct RX {
			static constexpr PIN PA10 = { GPIOA_BASE, 10, 7, USART1_BASE };
			static constexpr PIN PB7  = { GPIOB_BASE,  7, 7, USART1_BASE };
		};
	};
	struct _2 {
		struct TX {
			static constexpr PIN PA2  = { GPIOA_BASE,  2, 7, USART2_BASE };
			static constexpr PIN PD5  = { GPIOD_BASE,  5, 7, USART2_BASE };
		};
		struct RX {
			static constexpr PIN PA3  = { GPIOA_BASE,  3, 7, USART2_BASE };
			static constexpr PIN PD6  = { GPIOD_BASE,  6, 7, USART2_BASE };
		};
	};
#ifdef USART3_BASE
	struct _3 {
		struct TX {
			static constexpr PIN PB10 = { GPIOB_BASE, 10, 7, USART3_BASE };
			static constexpr PIN PC10 = { GPIOC_BASE, 10, 7, USART3_BASE };
			static constexpr PIN PD8  = { GPIOD_BASE,  8, 7, USART3_BASE };
		};
		struct RX {
			static constexpr PIN PB11 = { GPIOB_BASE, 11, 7, USART3_BASE };
			static constexpr PIN PC11 = { GPIOC_BASE, 11, 7, USART3_BASE };
			static constexpr PIN PD9  = { GPIOD_BASE,  9, 7, USART3_BASE };
		};
	};
#endif
#ifdef UART4_BASE
	struct _4 {
		struct TX {
			static constexpr PIN PA0  = { GPIOA_BASE,  0, 8, UART4_BASE };
			static constexpr PIN PC10 = { GPIOC_BASE, 10, 8, UART4_BASE };
		};
		struct RX {
			static constexpr PIN PA1  = { GPIOA_BASE,  1, 8, UART4_BASE };
			static constexpr PIN PC11 = { GPIOC_BASE, 11, 8, UART4_BASE };
		};
	};
#endif
#ifdef UART5_BASE
	struct _5 {
		struct TX {
			static constexpr PIN PC12 = { GPIOC_BASE, 12, 8, UART5_BASE };
		};
		struct RX {
			static constexpr PIN PD2  = { GPIOD_BASE,  2, 8, UART5_BASE };
		};
	};
#endif
#ifdef USART6_BASE
	struct _6 {
		struct TX {
			static constexpr PIN PC6  = { GPIOC_BASE,  6, 8, USART6_BASE };
			static constexpr PIN PG14 = { GPIOG_BASE, 14, 8, USART6_BASE };
		};
		struct RX {
			static constexpr PIN PC7  = { GPIOC_BASE,  7, 8, USART6_BASE };
			static constexpr PIN PG9  = { GPIOG_BASE,  9, 8, USART6_BASE };
		};
	};
#endif
#ifdef UART7_BASE
	struct _7 {
		struct TX {
			static constexpr PIN PE8  = { GPIOE_BASE,  8, 8, UART7_BASE };
			static constexpr PIN PF7  = { GPIOF_BASE,  7, 8, UART7_BASE };
		};
		struct RX {
			static constexpr PIN PE7  = { GPIOE_BASE,  7, 8, UART7_BASE };
			static constexpr PIN PF6  = { GPIOF_BASE,  6, 8, UART7_BASE };
		};
	};
#endif
#ifdef UART8_BASE
	struct _8 {
		struct TX {
			static constexpr PIN PE1  = { GPIOE_BASE,  1, 8, UART8_BASE };
		};
		struct RX {
			static constexpr PIN PE0  = { GPIOE_BASE,  0, 8, UART8_BASE };
		};
	};
#endif

	enum class FIFO{
		NO = 0,
	};

	enum class FIFO_TH{
		NO = 0,
	};

	static constexpr uint32_t FIFO_TH_TX_Pos = 0;
	static constexpr uint32_t FIFO_TH_RX_Pos = 0;
#endif

private:
	// -----------------------------------------------------------------------
	// Register-name abstraction layer
	//
	// F4 uses a single DR for both TX and RX, and SR for status/clear.
	// G0 and F7 split the data register into TDR/RDR and use ISR/ICR.
	// Private accessors hide these differences from all public methods.
	// -----------------------------------------------------------------------

	#if defined(STM32F4)
		static constexpr uint32_t CR1_TXEIE  = USART_CR1_TXEIE;
		static constexpr uint32_t CR1_RXNEIE = USART_CR1_RXNEIE;

		volatile uint32_t& TXD()        const { return USARTx->DR; }
		volatile uint32_t& RXD()        const { return USARTx->DR; }
		volatile uint32_t& Status_reg() const { return USARTx->SR; }
		volatile uint32_t& Clear_reg()  const { return USARTx->SR; }

		static constexpr uint32_t ISR_TXE  = USART_SR_TXE;
		static constexpr uint32_t ISR_RXNE = USART_SR_RXNE;
		static constexpr uint32_t ISR_IDLE = USART_SR_IDLE;
		static constexpr uint32_t ISR_TC   = USART_SR_TC;

	#elif defined(STM32F7)
		static constexpr uint32_t CR1_TXEIE  = USART_CR1_TXEIE;
		static constexpr uint32_t CR1_RXNEIE = USART_CR1_RXNEIE;

		volatile uint32_t& TXD()        const { return USARTx->TDR; }
		volatile uint32_t& RXD()        const { return USARTx->RDR; }
		volatile uint32_t& Status_reg() const { return USARTx->ISR; }
		volatile uint32_t& Clear_reg()  const { return USARTx->ICR; }

		static constexpr uint32_t ISR_TXE  = USART_ISR_TXE;
		static constexpr uint32_t ISR_RXNE = USART_ISR_RXNE;
		static constexpr uint32_t ISR_IDLE = USART_ISR_IDLE;
		static constexpr uint32_t ISR_TC   = USART_ISR_TC;

	#elif defined(STM32G0)
		static constexpr uint32_t CR1_TXEIE  = USART_CR1_TXEIE_TXFNFIE;
		static constexpr uint32_t CR1_RXNEIE = USART_CR1_RXNEIE_RXFNEIE;

		volatile uint32_t& TXD()        const { return USARTx->TDR; }
		volatile uint32_t& RXD()        const { return USARTx->RDR; }
		volatile uint32_t& Status_reg() const { return USARTx->ISR; }
		volatile uint32_t& Clear_reg()  const { return USARTx->ICR; }

		static constexpr uint32_t ISR_TXE  = USART_ISR_TXE_TXFNF;
		static constexpr uint32_t ISR_RXNE = USART_ISR_RXNE_RXFNE;
		static constexpr uint32_t ISR_IDLE = USART_ISR_IDLE;
		static constexpr uint32_t ISR_TC   = USART_ISR_TC;
		static constexpr uint32_t ISR_TXFT = USART_ISR_TXFT;
		static constexpr uint32_t ISR_RXFT = USART_ISR_RXFT;

	#endif
public:
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
		auto chk = [usartx](const PIN& p) {
			if (p.IsValid() && p.periph_base && p.periph_base != (uint32_t)usartx)
				{ __BKPT(0); while(1); }
		};
		chk(tx); chk(rx);
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
	 * @brief Enables a USART interrupt source in CR1.
	 * @param irq Interrupt source to enable (see USART::IRQ).
	 */
	inline void Enable_IRQ(IRQ irq)
	{
	#if defined(STM32F4) || defined(STM32F7)
		USARTx->CR1 |= static_cast<uint32_t>(irq);
	#elif defined(STM32G0)
		if(irq == IRQ::TXE || irq == IRQ::RXNE || irq == IRQ::TC || irq == IRQ::IDLE)
			USARTx->CR1 |= static_cast<uint32_t>(irq);
		else
			USARTx->CR3 |= static_cast<uint32_t>(irq);
	#endif
	}

	/**
	 * @brief Disables a USART interrupt source in CR1.
	 * @param irq Interrupt source to disable (see USART::IRQ).
	 */
	inline void Disable_IRQ(IRQ irq)
	{
	#if defined(STM32F4) || defined(STM32F7)
		USARTx->CR1 &= ~(static_cast<uint32_t>(irq));
	#elif defined(STM32G0)
		if(irq == IRQ::TXE || irq == IRQ::RXNE || irq == IRQ::TC || irq == IRQ::IDLE)
			USARTx->CR1 &= ~(static_cast<uint32_t>(irq));
		else
			USARTx->CR3 &= ~(static_cast<uint32_t>(irq));
	#endif
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

	inline bool GetDataReceived() { return data_received; }
	inline uint32_t GetDataReceivedCount() {
		uint32_t count = data_received_count;
		data_received_count = 0;
		data_received = false;
		return count;
	}
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
	 * @brief Registers this instance in IRQ_Registry and enables the NVIC line.
	 *
	 * Has no effect if SetUp() was not called successfully.
	 */
	inline void EnableNVIC_IRQ() {
		if (_info == nullptr) return;
		IRQ_Registry::Register(_info->irq, this);
		NVIC_EnableIRQ(_info->irq);
	}

	/**
	 * @brief Unregisters from IRQ_Registry and disables the NVIC line.
	 *
	 * Has no effect if SetUp() was not called successfully.
	 */
	inline void DisableNVIC_IRQ() {
		if (_info == nullptr) return;
		IRQ_Registry::Unregister(_info->irq);
		NVIC_DisableIRQ(_info->irq);
	}

	/**
	 * @brief Resets all control registers and disables the NVIC interrupt.
	 *
	 * Does not disable the peripheral clock.  Call SetUp() again to
	 * re-initialise.
	 */
	inline void DeInit(){
		DisableNVIC_IRQ();
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
