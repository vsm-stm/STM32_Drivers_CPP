/**
 * @file    uart.hpp
 * @brief   USART/UART driver for STM32F4, STM32F7 and STM32G0.
 *
 * One object controls one peripheral. Three transfer modes are available:
 * - Send()/Receive(): blocking polling with a timeout for the whole call.
 * - Send_IRQ()/Receive_IRQ(): asynchronous byte/FIFO interrupt transfers.
 * - Send_DMA()/Receive_DMA(): asynchronous transfers using constructor-bound DMA.
 *
 * Initialization and IRQ ownership
 * -------------------------------
 * Initialize System/RCC before SetUp(). PCLK is the default; on F7/G0 the
 * constructor can select SYSCLK, HSI or LSE where the instance supports them.
 * SetUp() uses oversampling x16 and a selected 8N1/8E1/8O1/8N2 frame (default
 * 8N1); G0 PRESC is /1. The selected instance provides clock
 * gates, PCLK variables, vectors and pins for the selected MCU. Named DMAReq
 * entries carry USART ownership/direction; DMA objects are stored inside USART.
 * TX/RX pins come from USART::_N::TX/RX. Their number/AF are defined by PIN;
 * constinit construction checks ownership and DMA compatibility at compilation.
 *
 * SetUp() registers the UART IRQ once and separate TX/RX DMA
 * handlers, including two entries when the directions share one hardware vector.
 * Do not register this object manually. IRQ_en() changes peripheral source bits
 * only, so completion callbacks may disable a source without changing the registry.
 * SetUp() and DeInit() run in main, never from a callback.
 * Shared IRQ capacity is configured in irq_registry_settings.hpp.
 * USART and its DMA handlers must not preempt one another for the same object.
 * Each IRQ entry services at most one hardware FIFO per direction: 8 words
 * on G0 with FIFO enabled, otherwise one byte. IDLE owns its RX tail budget.
 * USART IRQ uses entry snapshots for source/DMA-gate checks, except TCIE/TC,
 * which are re-read after callbacks and TX handling.
 * UART_USE_ATOMIC in this header selects control-bit and UART clock-field updates:
 * 0 (default) uses ordinary SET_BIT/CLEAR_BIT/MODIFY_REG; 1 uses ST ATOMIC_*.
 * In atomic mode F4/F7 use LDREX/STREX retries; G0 briefly masks interrupts for
 * each update and restores the prior PRIMASK. Rebuild all UART users after changing it.
 *
 * Packet reception
 * ----------------
 * Receive_IRQ()/Receive_DMA() receive one packet, ending at IDLE or buffer capacity.
 * DMA is stopped and its final counter/cache state is resolved before publishing
 * the ready flag. GetDataReceivedCount() is a pure read; clear the ready flag
 * explicitly after consuming the packet, then start the next receive.
 * The receiver remains enabled between packets: this API does not guarantee
 * continuous reception without gaps. A new Receive discards stale input/errors.
 * On G0 IDLE disables DMAR and DMA without waiting, then publishes the packet
 * from USART IRQ. The last DMA request must have been serviced before this
 * stop; abort during a DMA transfer can leave CNDTR inaccurate. Failed packets
 * are discarded without reading their DMA length.
 *
 * TX completion
 * -------------
 * TX OK means the source bytes have been handed to UART and the buffer is reusable.
 * It does not mean that the last stop bit has left the pin. UART TC/OnTC() provides
 * physical completion, for example for RS-485 DE or before SetBaud().
 * Waiting for physical completion before SetBaud() is the caller's responsibility.
 *
 * F7 cache and DMA
 * ----------------
 * UART calls System cache helpers at buffer handover. They are empty on F4/G0;
 * on F7 they skip a range wholly inside one configured non-cacheable DMA region.
 * A cacheable RX allocation owns all touched 32-byte lines, including padding.
 * CPU must not access these lines while RX DMA is Busy. Packet length itself
 * does not have to be a multiple of 32. Buffers must also be accessible to DMA.
 *
 * Blocking example (after System/RCC initialization)
 * @code
 *   constinit USART uart(USART::_1::instance, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7);
 *   if (uart.SetUp() == SysInitStatus::InitOK) {
 *       uint8_t message[] = "Hello\r\n";
 *       SysStatus sent = uart.Send(message, sizeof(message) - 1, 100);
 *       uint8_t reply[4];
 *       SysStatus received = uart.Receive(reply, sizeof(reply), 500);
 *       // Check sent/received; the timeout covers each whole method call.
 *   }
 * @endcode
 *
 * IRQ packet example
 * @code
 *   static uint8_t packet[64]; // Keep alive until reception completes.
 *   if (uart.Receive_IRQ(packet, sizeof(packet)) == SysStatus::OK) {
 *       // Return to main; IRQ handles incoming bytes and IDLE.
 *   }
 *   // In main, before starting another receive:
 *   if (uart.GetDataReceivedFlag()) {
 *       uint32_t count = uart.GetDataReceivedCount();
 *       // Process packet[0..count) here, while reception is stopped.
 *       uart.ClearDataReceivedFlag();
 *       uart.Receive_IRQ(packet, sizeof(packet)); // Check the new start result.
 *   }
 * @endcode
 *
 * See the constructor/Receive_DMA() below for DMA examples, and UART.md for details.
 */
#ifndef UART_H_
#define UART_H_

#include "system.hpp"
#include "rcc.hpp"
#include "gpio.hpp"
#include "irq_registry.hpp"
#include "dma.hpp"

// Shared by uart.cpp and inline methods. Change here and rebuild all UART users.
#ifndef UART_USE_ATOMIC
#define UART_USE_ATOMIC 0
#endif

#if UART_USE_ATOMIC
#define UART_SET_BIT ATOMIC_SET_BIT
#define UART_CLEAR_BIT ATOMIC_CLEAR_BIT
#define UART_MODIFY_REG ATOMIC_MODIFY_REG
#else
#define UART_SET_BIT SET_BIT
#define UART_CLEAR_BIT CLEAR_BIT
#define UART_MODIFY_REG MODIFY_REG
#endif

/**
 * @class USART
 * @brief Owns one USART/UART and its registered IRQ/DMA integration.
 *
 * TX and RX can operate independently. Configuration and buffer handover for
 * one object must be serialized by its caller. Enabling atomic register updates does not
 * serialize whole transfers or protect the associated buffer/state fields.
 * Do not copy/move the object. Keep it and active buffers
 * alive until their operations and registrations are stopped by DeInit().
 */
class USART : public IIRQHandler
{
public:
	/// Eight payload bits. Parity occupies the ninth hardware word bit.
	enum class FrameFormat : uint8_t { N8_1, E8_1, O8_1, N8_2 };
	// Named pin tables, FIFO enums and family-specific register accessors.
	// For example USART::_1::TX::PB6 stores port, pin, AF and USART1 ownership.
    /** Fixed device metadata from uart_defs.hpp; no runtime lookup. */
    struct Instance {
        uintptr_t periph;
        uintptr_t clk_reg;
        uint32_t clk_bit;
        const uint32_t* bus_clk;
        IRQn_Type irq;
#if defined(STM32F7) || defined(STM32G0)
        uint32_t clock_select; ///< Two-bit RCC selector mask; zero means fixed PCLK.
#endif
#if defined(STM32G0)
        uint32_t dma_tx_request, dma_rx_request;
#endif
    };
#include "uart_defs.hpp"

#if defined(STM32F7) || defined(STM32G0)
	/**
	 * @brief USART kernel clock, fixed when the object is constructed.
	 * Values match the two-bit RCC USARTxSEL field. Availability depends on
	 * the selected MCU and instance; a G0 instance without a selector uses PCLK only.
	 * The caller enables HSI/LSE before SetUp() and keeps the source running.
	 * Keep RCC source frequencies and bus dividers unchanged while UART is in use.
	 */
	enum class
	ClockSource : uint8_t {
		PCLK = 0,   ///< APB clock for this instance (default).
		SYSCLK = 1, ///< System clock before the AHB divider.
		HSI = 2,    ///< HSI 16 MHz; on G0 this is before HSIDIV.
		LSE = 3     ///< LSE 32768 Hz.
	};
#endif

	/**
	 * @brief Peripheral interrupt sources controlled by IRQ_en().
	 *
	 * TXE/RXNE/TC/IDLE map to CR1. G0 FIFO threshold sources map to CR3.
	 * Send_IRQ()/Receive_IRQ() select their byte/FIFO sources automatically;
	 * manual IRQ_en() is useful for notifications such as physical TX completion.
	 */
	enum class IRQ {
		TXE = CR1_TXEIE,             ///< TX data register empty / FIFO not full.
		RXNE = CR1_RXNEIE,           ///< RX data register/FIFO contains data.
		TC = USART_CR1_TCIE,         ///< Last frame, including its stop bit, completed.
		IDLE = USART_CR1_IDLEIE,     ///< Idle line: end the current packet.
#if defined(STM32G0)
		TXFIFO = USART_CR3_TXFTIE,   ///< G0 TX FIFO reached its configured threshold.
		RXFIFO = USART_CR3_RXFTIE,   ///< G0 RX FIFO reached its configured threshold.
#endif
	};
	/**
	 * @brief UART DMA request directions, not DMA controller enable bits.
	 * @see DMA_en(), SetUp()
	 */
	enum class DMA {
		TXRX = USART_CR3_DMAR | USART_CR3_DMAT, ///< Both UART request directions.
		TX = USART_CR3_DMAT,                    ///< UART TX requests only.
		RX = USART_CR3_DMAR                     ///< UART RX requests only.
	};

#if defined(STM32G0)
	using DMAReq = uintptr_t; ///< Request source follows USART and TX/RX.
#else
	using DMAReq = DMA_Sx::DMAReq;
#endif
	/**
	 * @brief Bit values delivered together to OnError(uint32_t).
	 *
	 * Parity/Framing/Noise/Overrun correspond to UART status bits 0..3.
	 * DmaTx/DmaRx identify the failing DMA direction, not raw DMA ISR bits.
	 * Several causes can be present in one callback; cast an Error to uint32_t
	 * when testing the mask. Hardware errors stop the affected operation and
	 * set its SysStatus::Error; a failed RX packet is not published as ready.
	 */
	enum class Error : uint32_t {
		Parity = 1U << 0,
		Framing = 1U << 1,
		Noise = 1U << 2,
		Overrun = 1U << 3,
		DmaTx = 1U << 16,
		DmaRx = 1U << 17
	};

	/**
	 * @brief Fix peripheral, pins, optional DMA and clock; SetUp configures hardware.
	 *
	 * Declare a static/global object with constinit to check pins/DMA at compilation.
	 * No hardware is accessed. The deprecated pointer overload checks at runtime.
	 *
	 * @param instance USART::_N::instance; the legacy overload accepts a CMSIS pointer.
	 * @param baudrate Requested bits per second; validated against the kernel clock.
	 * @param tx Named USART::_N::TX pin, or PIN{} to leave TX unconfigured.
	 * @param rx Named USART::_N::RX pin, or PIN{} to leave RX unconfigured.
	 * @param dma_tx Named TX request; {} means no DMA. On G0: DMAx_Channely_BASE.
	 * @param dma_rx Named RX request; {} means no DMA. TX/RX resources must differ.
	 * @param clock_source F7/G0 only: kernel clock, PCLK by default. Immutable;
	 *        SetUp() applies the choice but never starts an oscillator. An unavailable
	 *        source or an HSI/LSE source that is not enabled and ready gives InitError.
	 *
	 * TX-only example:
	 * @code
	 *   constinit USART log(USART::_2::instance, 115200, USART::_2::TX::PA2, PIN{});
	 *   SysInitStatus status = log.SetUp();
	 * @endcode
	 *
	 * F7/G0 HSI example, after the caller has enabled HSI and waited for HSIRDY:
	 * @code
	 *   constinit USART link(USART::_1::instance, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7,
	 *              {}, {}, USART::ClockSource::HSI);
	 *   SysInitStatus status = link.SetUp();
	 * @endcode
	 *
	 * G0 DMA example (select channels available on the chosen MCU):
	 * @code
	 *   constinit USART uart(USART::_1::instance, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7,
	 *              DMA1_Channel4_BASE,
	 *              DMA1_Channel5_BASE);
	 *   SysInitStatus status = uart.SetUp(); // Configures UART and both DMA channels.
	 * @endcode
	 *
	 * F4/F7 DMA construction (stream and CHSEL are part of DMAReq):
	 * @code
	 *   constinit USART uart(USART::_1::instance, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7,
	 *              USART::_1::DMA::TX_DMA2_Stream7,
	 *              USART::_1::DMA::RX_DMA2_Stream5);
	 *   SysInitStatus status = uart.SetUp();
	 * @endcode
	 * The constructor checks request validity, USART ownership, direction and
	 * distinct TX/RX resources during constinit initialization. No Config wrapper
	 * or template API is required. Only the deprecated pointer overload uses DebugTrap.
	 * USART embeds two DMA_Sx objects; no external objects or heap allocation.
	 * SetUp acquires the selected resources, configures Byte/MINC and the UART
	 * data-register address, and registers separate DMA handlers. A failed setup
	 * releases only resources acquired by that call. DeInit releases DMA but
	 * preserves these bindings. Blocking and IRQ methods remain available.
	 */
	/// Use constinit at static storage duration to require checked constant initialization.
	constexpr explicit USART(const Instance& instance, uint32_t baudrate,
		PIN tx = {}, PIN rx = {}, DMAReq dma_tx = {}, DMAReq dma_rx = {}
#if defined(STM32F7) || defined(STM32G0)
		, ClockSource clock_source = ClockSource::PCLK
#endif
		)
		: _instance(instance), BaudRate(baudrate), _TX(tx), _RX(rx)
#if defined(STM32F7) || defined(STM32G0)
		, _clock_source(clock_source)
#endif
#if defined(STM32G0)
		, _dma_tx(dma_tx, instance.dma_tx_request), _dma_rx(dma_rx, instance.dma_rx_request)
#else
		, _dma_tx(dma_tx), _dma_rx(dma_rx)
#endif
		, _dma_tx_irq(*this), _dma_rx_irq(*this)
	{
		if ((tx.IsValid() && tx.periph_base && tx.periph_base != instance.periph)
			|| (rx.IsValid() && rx.periph_base && rx.periph_base != instance.periph)
			|| !DMA_Sx::RouteMatches(dma_tx, instance.periph, DMA_Sx::DIR::To_Per)
			|| !DMA_Sx::RouteMatches(dma_rx, instance.periph, DMA_Sx::DIR::From_Per)
			|| DMA_Sx::SameResource(dma_tx, dma_rx))
			__builtin_unreachable();
#if defined(STM32F7) || defined(STM32G0)
		if (static_cast<uint32_t>(clock_source) > static_cast<uint32_t>(ClockSource::LSE)
			|| (!instance.clock_select && clock_source != ClockSource::PCLK))
			__builtin_unreachable();
#endif
	}

	/// Compatibility path only: lookup and validation occur at runtime.
	[[deprecated("Use instance and constinit")]]
	explicit USART(USART_TypeDef* usartx, uint32_t baudrate,
		PIN tx = PIN{}, PIN rx = PIN{},
		#if defined(STM32G0)
		DMA_Channel_TypeDef* dma_tx = nullptr, DMA_Channel_TypeDef* dma_rx = nullptr
#else
		DMAReq dma_tx = {}, DMAReq dma_rx = {}
#endif
#if defined(STM32F7) || defined(STM32G0)
		, ClockSource clock_source = ClockSource::PCLK
#endif
		)
		: _instance(FindInstance(usartx)), USARTx(usartx), BaudRate(baudrate), _TX(tx), _RX(rx)
#if defined(STM32F7) || defined(STM32G0)
		, _clock_source(clock_source)
#endif
#if defined(STM32G0)
		, _dma_tx(dma_tx, _instance.dma_tx_request), _dma_rx(dma_rx, _instance.dma_rx_request)
#else
		, _dma_tx(dma_tx), _dma_rx(dma_rx)
#endif
		, _dma_tx_irq(*this), _dma_rx_irq(*this)
	{
		if (tx.IsValid() && tx.periph_base && tx.periph_base != (uint32_t)usartx)
			System::DebugTrap("USART: TX pin belongs to wrong peripheral");
		if (rx.IsValid() && rx.periph_base && rx.periph_base != (uint32_t)usartx)
			System::DebugTrap("USART: RX pin belongs to wrong peripheral");
#if defined(STM32G0)
		if (dma_tx && dma_tx == dma_rx)
			System::DebugTrap("USART: TX and RX require different DMA channels");
#else
		const auto& tx_req = dma_tx;
		const auto& rx_req = dma_rx;
		const bool has_tx = tx_req.stream || tx_req.ch || tx_req.periph_base;
		const bool has_rx = rx_req.stream || rx_req.ch || rx_req.periph_base;
		const bool same_resource = tx_req.stream == rx_req.stream;
		if (has_tx && (!_dma_tx.HasSelection() || !tx_req.IsValid()
			|| tx_req.periph_base != reinterpret_cast<uintptr_t>(usartx)
			|| tx_req.direction != DMA_Sx::DIR::To_Per))
			System::DebugTrap("USART: invalid TX DMA request");
		if (has_rx && (!_dma_rx.HasSelection() || !rx_req.IsValid()
			|| rx_req.periph_base != reinterpret_cast<uintptr_t>(usartx)
			|| rx_req.direction != DMA_Sx::DIR::From_Per))
			System::DebugTrap("USART: invalid RX DMA request");
		if (has_tx && has_rx && same_resource)
			System::DebugTrap("USART: TX and RX require different DMA resources");
#endif
	}
	USART() = delete;
	USART(const USART&) = delete;
	USART& operator=(const USART&) = delete;
	USART(USART&&) = delete;
	USART& operator=(USART&&) = delete;
	/**
	 * @brief Does not stop hardware or unregister automatically.
	 * @pre Call DeInit() before destroying an initialized/registered object.
	 */
	~USART() = default;

	/**
	 * @brief Configure GPIO/UART and constructor-bound DMA; register their IRQs.
	 *
	 * Uses the constructor's kernel clock (PCLK on F4), oversampling x16,
	 * 8 data bits, no parity and one stop bit. BRR is rounded from
	 * (kernel_clock + baud/2) / baud; it must fit 16..65535,
	 * and requested baud must not exceed kernel_clock/16. G0 PRESC is /1.
	 * HSI/LSE must already be enabled and ready; SetUp() only checks their state.
	 * SetUp() registers this object and each bound DMA direction separately.
	 * Request compatibility was checked by construction. Busy/invalid hardware
	 * resources are rejected at acquisition; partial setup releases its acquisitions.
	 * Transfer sources themselves are enabled later by the selected operation.
	 *
	 * @param fifo NO by default. EN is supported only on FIFO-capable G0 instances.
	 * @param fifo_th_tx G0 TX threshold when FIFO is enabled.
	 * @param fifo_th_rx G0 RX threshold when FIFO is enabled.
	 * FIFO_TH::all is encoding 5: RX full, TX empty. F4/F7 accept NO only.
	 * @return InitOK on success; InitError for invalid peripheral/clock/baud/FIFO,
	 *         repeated setup or a failed IRQ registration (including a full row).
	 * @pre System/RCC are initialized. Call from main; repeated setup needs DeInit().
	 *
	 * G0 FIFO example, on an instance supporting FIFO:
	 * @code
	 *   uart.SetUp(USART::FIFO::EN, USART::FIFO_TH::TH_1_2,
	 *                              USART::FIFO_TH::TH_1_2);
	 * @endcode
	 */
	/// format is selected once during setup; all formats keep eight payload bits.
	SysInitStatus SetUp(FIFO fifo = FIFO::NO, FIFO_TH fifo_th_tx = FIFO_TH::NO,
		FIFO_TH fifo_th_rx = FIFO_TH::NO, FrameFormat format = FrameFormat::N8_1);

	/**
	 * @brief Enable/disable one UART interrupt source without changing the registry.
	 *
	 * SetUp() owns the UART registration until DeInit(). This method neither
	 * registers/unregisters handlers nor changes NVIC, and is usable in callbacks.
	 * Before SetUp() it has no effect. FIFO sources require SetUp(FIFO::EN).
	 * UART_USE_ATOMIC selects ordinary or atomic changes of the control bits.
	 * Transfer methods manage their own sources; disabling one manually during
	 * an operation does not by itself complete that operation or change Busy.
	 *
	 * @param irq UART source in CR1, or G0 FIFO threshold source in CR3.
	 * @param en ENABLE to set, DISABLE to clear.
	 * @code
	 *   uart.IRQ_en(USART::IRQ::TC, ENABLE); // OnTC(): last stop bit completed.
	 * @endcode
	 */
	inline void IRQ_en(IRQ irq, FunctionalState en)
	{
		if (!_info) return;
#if defined(STM32G0)
		auto& reg = (irq == IRQ::TXFIFO || irq == IRQ::RXFIFO) ? USARTx->CR3 : USARTx->CR1;
#else
		auto& reg = USARTx->CR1;
#endif
		if (en) UART_SET_BIT(reg, static_cast<uint32_t>(irq));
		else UART_CLEAR_BIT(reg, static_cast<uint32_t>(irq));
	}

	/**
	 * @brief Change the UART DMAT/DMAR request gates.
	 * @param en ENABLE to allow UART requests, DISABLE to stop issuing requests.
	 * @param dma TX, RX or TXRX (default).
	 *
	 * This low-level method does not configure/start/stop/release a DMA resource,
	 * disable the UART receiver, or perform cache/buffer handover. Normal *_DMA()
	 * operations manage these gates themselves: DMA starts before requests are
	 * enabled; RX completion disables requests and waits for DMA EN=0.
	 * UART_USE_ATOMIC selects ordinary or atomic DMAT/DMAR changes.
	 */
	inline void DMA_en(FunctionalState en, DMA dma = DMA::TXRX)
	{
		if (en) UART_SET_BIT(USARTx->CR3, static_cast<uint32_t>(dma));
		else UART_CLEAR_BIT(USARTx->CR3, static_cast<uint32_t>(dma));
	}

#if defined(STM32F4)
	/**
	 * @brief Write zero to F4 SR to clear flags supporting write-zero clearing.
	 *
	 * This does not replace the SR/DR read sequence required for IDLE, RXNE and
	 * receive errors. The driver's receive/completion paths perform that sequence
	 * themselves, after stopping DMA when necessary. Do not use this function
	 * to cancel or complete a transfer.
	 */
	inline void ClearFlags() { USARTx->SR = 0; }
#else
	/**
	 * @brief F7/G0 flags cleared through ICR, including noise error (NE).
	 * Values are ICR masks; F4 instead provides the SR-based ClearFlags().
	 * CMSIS names the noise-clear bit NCF on F7 and NECF on G0.
	 */
	enum class ISR_FLAGS {
		PE = USART_ICR_PECF,
		FE = USART_ICR_FECF,
#if defined(STM32G0)
		NE = USART_ICR_NECF,
#else
		NE = USART_ICR_NCF,
#endif
		ORE = USART_ICR_ORECF,
		IDLE = USART_ICR_IDLECF,
		TC = USART_ICR_TCCF
	};
	/**
	 * @brief Acknowledge one F7/G0 status flag through ICR.
	 * @param flag Flag to clear; this does not change transfer state or counters.
	 * @code
	 *   uart.ClearFlags(USART::ISR_FLAGS::TC);
	 * @endcode
	 */
	inline void ClearFlags(ISR_FLAGS flag) { USARTx->ICR = static_cast<uint32_t>(flag); }
#endif

	/**
	 * @brief Queue all bytes into UART by polling TXE/TXFNF.
	 * @param data Readable source buffer; contents are not modified.
	 * @param len Nonzero number of bytes.
	 * @param timeout Maximum wait budget in milliseconds for the whole call,
	 *                not a separate timeout for each byte. Uses System::GetTick().
	 * @return OK when all bytes are queued; NotInit before SetUp(); Error for
	 *         null/zero length or unconfigured TX; Busy if TX is active; Timeout
	 *         if a required TXE wait exhausts the budget.
	 *
	 * OK permits source buffer reuse, but does not guarantee the final stop bit
	 * has left the pin. On Timeout, already queued bytes may still be transmitted;
	 * the stored TX status is Timeout. A new operation can be started after
	 * Error/Timeout without repeating SetUp(). Do not modify TX configuration
	 * before physical completion.
	 */
	SysStatus Send(uint8_t* data, uint32_t len, uint32_t timeout = 100);
	/**
	 * @brief Poll RXNE/RXFNE until exactly len bytes have been read.
	 * @param data Writable receive buffer, valid for the duration of this call.
	 * @param len Nonzero number of bytes to read; IDLE does not end this method.
	 * @param timeout Milliseconds for the whole call; requires a running System tick.
	 * @return OK after len bytes; NotInit before SetUp(); Error for null/zero
	 *         length, unconfigured RX or a hardware receive error; Busy if RX
	 *         is active; Timeout when a required RX wait exhausts the budget.
	 *
	 * Clears previous ready/count/overflow state and discards stale UART input
	 * before starting. Partial data may have been written on Error/Timeout;
	 * this blocking API does not publish the asynchronous ready/count result.
	 * A hardware receive error calls OnError() in the calling context.
	 */
	SysStatus Receive(uint8_t* data, uint32_t len, uint32_t timeout = 100);
	/**
	 * @brief Start asynchronous TX through TXE or G0 FIFO threshold interrupts.
	 * @param data Source buffer; keep alive and unchanged until TX leaves Busy.
	 * @param len Number of bytes, 1..65535.
	 * @return OK when started (not completed); NotInit before SetUp(); Error
	 *         for an invalid buffer/length or unconfigured TX; Busy if TX is active.
	 *
	 * OnTxEmpty() feeds the buffer and disables its source when all bytes are
	 * queued. Then GetTxStatus() is OK and the buffer can be reused. Physical
	 * completion is the separate TC/OnTC() event, enabled with IRQ_en(TC, ENABLE).
	 */
	SysStatus Send_IRQ(uint8_t* data, uint32_t len);
	/**
	 * @brief Start one IRQ-received packet ending at IDLE or buffer capacity.
	 * @param data Writable buffer, kept alive until RX is no longer Busy.
	 * @param len Capacity, 1..65535 bytes.
	 * @return OK on start; NotInit before SetUp(); Error for invalid buffer/length
	 *         or unconfigured RX; Busy if another RX operation is active.
	 *
	 * Starts with fresh ready/count/overflow state and discards stale input/errors.
	 * Enables byte/FIFO RX, IDLE and receive error sources. Completion disables
	 * those sources, sets RX OK and publishes ready/count; an error sets Error
	 * and invokes OnError() without publishing a ready packet.
	 * Read/process the completed buffer, explicitly clear ready, then rearm.
	 */
	SysStatus Receive_IRQ(uint8_t* data, uint32_t len);

	/**
	 * @brief Start one DMA transmission using the constructor-bound TX resource.
	 * @param data DMA-accessible source, kept alive and unchanged until TX completes.
	 * @param len Number of bytes, 1..65535.
	 * @return OK on start; NotInit before UART setup; Error for missing TX DMA,
	 *         invalid buffer/length or unconfigured TX; Busy if TX is active.
	 *         DMA address/count preparation failures are returned to the caller.
	 *
	 * System::CacheClean() publishes source bytes before DMA starts; on F4/G0
	 * it is empty, while F7 skips maintenance for a configured non-cacheable range.
	 * OnDmaTxComplete() stops/acknowledges DMA and sets TX OK. The source buffer
	 * is then reusable, but physical UART TC remains a separate event.
	 * A DMA error stops TX, sets Error and calls OnError() with Error::DmaTx.
	 */
	SysStatus Send_DMA(uint8_t* data, uint32_t len);
	/**
	 * @brief Start one DMA-received packet, ending at IDLE or capacity.
	 * @param data DMA-accessible writable allocation, alive until RX completes.
	 * @param len Packet capacity, 1..65535 bytes; not restricted to multiples of 32.
	 * @return OK on start; NotInit before UART setup; Error for missing RX DMA,
	 *         invalid buffer/length or unconfigured RX; Busy if RX is active.
	 *         DMA address/count preparation failures are returned unchanged.
	 *
	 * Clears previous ready/count/overflow state and stale UART input/errors.
	 * Calls System::CacheCleanInvalidate() before starting DMA. On completion:
	 * disable DMAR and request a DMA stop. On F4/F7 the DMA TC IRQ then reads the count,
	 * invalidate the bytes written by DMA, retain a fitting UART/FIFO tail,
	 * then publish RX OK and ready/count. Errors instead publish RX Error and
	 * invoke OnError(), without a ready packet.
	 * G0 IDLE completes in USART IRQ without waiting. This assumes that the last
	 * DMA request has completed; arbitrary mid-transfer abort does not guarantee
	 * CNDTR. Failed G0 packets are discarded without calculating their length.
	 *
	 * F7 cacheable RX storage must own every touched 32-byte cache line, including
	 * padding. Do not share these lines with unrelated data or access them from
	 * CPU while Busy. System skips cache maintenance for a range wholly within
	 * one configured non-cacheable DMA region; buffer ownership still applies.
	 * F4/G0 cache helpers are empty.
	 *
	 * Example after successful SetUp() with constructor-bound RX DMA:
	 * @code
	 *   alignas(32) static uint8_t packet[64]; // Own two entire cache lines.
	 *   SysStatus started = uart.Receive_DMA(packet, 37); // Logical capacity is 37.
	 *   // Later in main, if started == OK:
	 *   if (uart.GetDataReceivedFlag()) {
	 *       uint32_t count = uart.GetDataReceivedCount();
	 *       // Process packet[0..count); GetDataReceivedCount() does not clear it.
	 *       uart.ClearDataReceivedFlag();
	 *       uart.Receive_DMA(packet, 37); // Check result before awaiting next packet.
	 *   }
	 * @endcode
	 *
	 * The method does not provide uninterrupted reception between packets.
	 * UART can still receive into RDR/FIFO after DMA stops; rearm explicitly
	 * after consuming the completed buffer.
	 */
	SysStatus Receive_DMA(uint8_t* data, uint32_t len);

	/**
	 * @brief Whether an asynchronous packet has been completed successfully.
	 * Not a blocking Receive() result. Error paths do not publish ready.
	 * The next Receive or ClearDataReceivedFlag() clears it.
	 */
	inline bool GetDataReceivedFlag() { return data_received; }
	/**
	 * @brief Read the completed IRQ/DMA packet length without changing state.
	 * @return Zero while RX is Busy, after Error, or without a ready packet;
	 *         otherwise the completed byte count. Repeated reads return the same
	 *         value until ClearDataReceivedFlag() or a new Receive.
	 * @see GetDataReceivedFlag(), ClearDataReceivedFlag()
	 */
	inline uint32_t GetDataReceivedCount() {
		return rx_status == SysStatus::OK && data_received ? data_received_count : 0;
	}
	/**
	 * @brief Clear only the asynchronous ready flag and received byte count.
	 * @pre Reception is stopped and the completed packet has been consumed.
	 * Does not stop DMA/IRQ, restart reception, or reset overflow/status.
	 * Do not use this as cancellation while RX is Busy.
	 */
	inline void ClearDataReceivedFlag() { data_received = false; data_received_count = 0; }
	/**
	 * @brief Read the software buffer-overflow flag without clearing it.
	 * Counts bytes discarded by the software receive callbacks when no room
	 * remains; this is distinct from hardware ORE delivered to OnError().
	 */
	inline bool GetOverflow() { return data_overflow; }
	/**
	 * @brief Read and clear software overflow count and its flag.
	 * @pre Reception has stopped; do not use read-and-clear concurrently with RX.
	 * A new Receive also resets overflow state. Unlike GetDataReceivedCount(),
	 * this getter retains its read-and-clear behavior.
	 */
	inline uint32_t GetOverflowCount() {
		uint32_t count = data_overflow_count;
		data_overflow_count = 0;
		data_overflow = false;
		return count;
	}
	/**
	 * @brief Current TX state: NotInit, Busy, OK, Error or blocking Timeout.
	 * OK after IRQ/DMA TX permits source reuse; physical completion is UART TC.
	 */
	SysStatus GetTxStatus() const { return tx_status; }
	/**
	 * @brief Current RX state: NotInit, Busy, OK, Error or blocking Timeout.
	 * OK alone does not identify a new packet; use GetDataReceivedFlag() for
	 * successful asynchronous reception and then GetDataReceivedCount().
	 */
	SysStatus GetRxStatus() const { return rx_status; }

	/**
	 * @brief Validate and change baud without repeating the full UART setup.
	 * @param baud Requested bits per second, representable at the kernel clock.
	 * @return OK after programming; NotInit before SetUp(); Error for zero or an
	 *         unrepresentable divider or a source that is no longer ready;
	 *         Busy while software TX or RX is active.
	 *         Rejected calls leave BRR and the stored BaudRate unchanged.
	 *
	 * Writes BRR with UE=0 and restores the previous UE bit using UART_USE_ATOMIC.
	 * Stores the new BaudRate for a later setup. Other CR1 bits are not restored from
	 * an old snapshot. Uses the same rounded x16 divider and the same
	 * constructor-selected source as SetUp(); the clock source is not changed.
	 * @pre Caller has ensured physical TX completion via TC/OnTC(), RX is stopped,
	 *      and the peer will not transmit while the UART is temporarily disabled.
	 *      TX OK alone is insufficient: the final stop bit may still be active.
	 * The driver does not wait for TC or store an additional completion flag.
	 * If IRQ handling clears TC, observe the OnTC() event in the application.
	 *
	 * @code
	 *   // In main, after the application observed physical TC, with RX stopped:
	 *   SysStatus changed = uart.SetBaud(9600);
	 * @endcode
	 */
	SysStatus SetBaud(uint32_t baud);
	/**
	 * @brief Reserved API; currently has no effect.
	 * @param parity Ignored. Select FrameFormat in SetUp() instead.
	 */
	inline void SetParity(uint32_t parity) { (void)parity; }

	/**
	 * @brief Stop this UART, release owned DMA and remove its IRQ registrations.
	 *
	 * Disables UART sources/requests, stops DMA with EN=0 confirmation and
	 * returns an active F7 RX buffer to CPU through the System cache helper.
	 * Removes only this object's entries from UART/DMA vectors, retaining other
	 * registered handlers and their shared NVIC state. Releases owned DMA,
	 * clears descriptors/flags/counters and sets TX/RX to NotInit.
	 *
	 * Leaves the UART peripheral clock enabled. Does not publish a cancelled
	 * packet as successful and does not wait for physical TX completion: finish
	 * TC first if truncating the last byte is unacceptable.
	 * @pre Call from main, not from IRQ/callback. No effect before successful SetUp().
	 * @code
	 *   uart.DeInit();
	 *   // Bindings remain; a successful SetUp() acquires the same DMA again.
	 * @endcode
	 */
	void DeInit();

private:
	const Instance& _instance;
	USART_TypeDef* USARTx = nullptr; ///< Hardware binding is fixed by the constructor.
	uint32_t BaudRate;     ///< Requested baud retained across DeInit()/SetUp().

	/**
	 * @brief Active setup metadata; the fixed instance is bound by the constructor.
	 * _info remains null until successful hardware setup; it is not a lookup result.
	 * bus_clk points to the System PCLK variable maintained by RCC setup.
	 */
	using PeriphInfo = Instance;
	PIN _TX; ///< Optional TX pin and stored AF.
	PIN _RX; ///< Optional RX pin and stored AF.
#if defined(STM32F7) || defined(STM32G0)
	const ClockSource _clock_source; ///< Constructor choice, retained across DeInit().
#endif
	/// Selected nominal kernel frequency; zero for an unavailable/not-ready source.
	static const Instance& FindInstance(USART_TypeDef* usart);
	uint32_t KernelClock(const PeriphInfo& info) const;

	/**
	 * @brief Asynchronous transfer descriptor, initialized before enabling a source.
	 * TX IRQ and RX IRQ advance the cursor and reduce remaining size.
	 * RX DMA keeps original address/capacity to compute length and service cache.
	 * Blocking methods use their arguments directly.
	 */
	struct Data {
		uint8_t* data_ptr;
		uint16_t size;
	};
	Data tx_data{};
	Data rx_data{}; ///< IRQ: cursor/remaining; DMA: original address/capacity.
	// Single main/IRQ state fields. They are not locks or thread synchronization;
	// completed packet consumption/reset precedes the next receive.
	volatile SysStatus tx_status{SysStatus::NotInit};
	volatile SysStatus rx_status{SysStatus::NotInit};
	volatile bool data_received{false};
	volatile uint32_t data_received_count{0};
	volatile bool data_overflow{false};
	volatile uint32_t data_overflow_count{0};
	DMA_Sx _dma_tx; ///< Owned TX resource; null hardware selection when absent.
	DMA_Sx _dma_rx; ///< Owned RX resource; no external DMA object or rebinding.
	const PeriphInfo* _info = nullptr;
	SysInitStatus SetUpDMA(); ///< Configure constructor-bound DMA during SetUp().
	/// Maximum ready words serviced in one direction per IRQ entry.
	uint32_t FifoDepth() const {
#if defined(STM32G0)
		return (USARTx->CR1 & USART_CR1_FIFOEN) ? 8U : 1U;
#else
		return 1U;
#endif
	}
	volatile bool _rx_dma_idle = false; ///< IDLE requested a DMA stop; TC finishes RX.
	volatile uint32_t _rx_errors = 0; ///< UART errors retained until DMA has stopped.

	// Separate registry objects keep the existing IIRQHandler registry unchanged.
	// Each adapter forwards directly to one DMA direction; no source selection.
	class DmaTxIRQ final : public IIRQHandler {
	public:
		constexpr explicit DmaTxIRQ(USART& uart) : _uart(uart) {}
		void HandleIRQ() override { _uart.HandleDmaTxIRQ(); }
	private:
		USART& _uart;
	};
	class DmaRxIRQ final : public IIRQHandler {
	public:
		constexpr explicit DmaRxIRQ(USART& uart) : _uart(uart) {}
		void HandleIRQ() override { _uart.HandleDmaRxIRQ(); }
	private:
		USART& _uart;
	};
	DmaTxIRQ _dma_tx_irq;
	DmaRxIRQ _dma_rx_irq;
	void HandleDmaTxIRQ();
	void HandleDmaRxIRQ();
	/**
	 * @brief USART-only registry entry point; DMA has two separate handlers.
	 * Application subclasses override callbacks, not these dispatch methods.
	 */
	void HandleIRQ() override final;

protected:
	/**
	 * @name Peripheral callbacks
	 * Called in IRQ for asynchronous operations. Keep application work bounded;
	 * process completed packets in main. OnError() from blocking Receive() runs
	 * in the calling context instead.
	 * On F4/F7, OnIdle() for DMA RX runs from DMA RX TC after the requested stop,
	 * not from USART IRQ. Without DMA it runs from USART IRQ as before.
	 * On G0, IDLE completion runs from USART IRQ, capacity from DMA RX IRQ.
	 *
	 * OnRxByte/OnTxEmpty/OnIdle/OnDmaTxComplete/OnDmaRxComplete perform the standard
	 * buffer/state handling. Call their base implementation before adding logic
	 * if that handling is required. OnTC and OnError are notifications with empty
	 * defaults. Never change the IRQ registry or call SetUp/DeInit here.
	 *
	 * Example: caller-owned notification of physical completion and errors:
	 * @code
	 *   class Link : public USART {
	 *   public:
	 *       using USART::USART;
	 *       volatile bool wire_complete = false;
	 *       volatile uint32_t last_error = 0;
	 *   protected:
	 *       void OnTC() override { wire_complete = true; }
	 *       void OnError(uint32_t errors) override { last_error = errors; }
	 *       void OnIdle() override {
	 *           USART::OnIdle(); // Stop/count/cache and publish the packet first.
	 *           // Main can consume the packet using the standard ready/count API.
	 *       }
	 *   };
	 *
	 *   constinit Link link(USART::_1::instance, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7);
	 *   static uint8_t message[] = "Hello\r\n";
	 *   if (link.SetUp() == SysInitStatus::InitOK) {
	 *       link.IRQ_en(USART::IRQ::TC, ENABLE);
	 *       link.wire_complete = false; // Reset before each new Send_*().
	 *       SysStatus started = link.Send_IRQ(message, sizeof(message) - 1);
	 *       // Check started before awaiting wire_complete.
	 *   }
	 *   // Later in main, after observing wire_complete and with RX stopped:
	 *   // SysStatus changed = link.SetBaud(9600);
	 * @endcode
	 * @{
	 */
	/**
	 * @brief Store one IRQ-received byte, advance the cursor and update count.
	 * At capacity, disable receive sources and publish the completed packet.
	 * If no room remains, discard the byte and update software overflow.
	 */
	virtual void OnRxByte(uint8_t byte);
	/// Feed UART/FIFO from TX IRQ buffer; at exhaustion disable its source and set TX OK.
	virtual void OnTxEmpty();
	/**
	 * @brief End the active packet on IDLE and acknowledge the hardware flag.
	 * With DMA on F4/F7: runs after TC has confirmed the requested stop; read
	 * the final count and invalidate written data without waiting in IRQ.
	 * G0: disable DMAR/DMA without waiting, then count only an error-free packet.
	 * Preserve a fitting pending UART/FIFO tail, then publish ready or OnError().
	 */
	virtual void OnIdle();
	/**
	 * @brief Notification that the UART TC source fired (physical TX completion).
	 * HandleIRQ() has acknowledged TC. Empty by default; use for application
	 * completion tracking/RS-485 DE. Enable with IRQ_en(IRQ::TC, ENABLE).
	 */
	virtual void OnTC();
	/// Stop/acknowledge TX DMA, disable DMAT and set TX OK; UART may still be shifting.
	virtual void OnDmaTxComplete();
	/**
	 * @brief Finish RX at DMA capacity using the same buffer handover as IDLE.
	 * Default implementation calls USART::OnIdle() explicitly; it does not call
	 * a subclass's OnIdle() override. Override this callback for capacity events.
	 */
	virtual void OnDmaRxComplete();
	/**
	 * @brief Notification after the failing operation is stopped and marked Error.
	 * @param errors Combined Error mask; may include multiple UART/DMA causes.
	 * A failed RX is not ready. Empty default; no automatic retry or recovery.
	 * A new operation may be started after Error without repeating SetUp().
	 */
	virtual void OnError(uint32_t errors);
	/** @} */
};

// Emitted only for callers of the deprecated pointer constructor.
inline const USART::Instance& USART::FindInstance(USART_TypeDef* usart)
{
    switch (reinterpret_cast<uintptr_t>(usart)) {
#ifdef USART1_BASE
    case USART1_BASE: return _1::instance;
#endif
#ifdef USART2_BASE
    case USART2_BASE: return _2::instance;
#endif
#ifdef USART3_BASE
    case USART3_BASE: return _3::instance;
#endif
#if defined(USART4_BASE)
    case USART4_BASE: return _4::instance;
#elif defined(UART4_BASE)
    case UART4_BASE: return _4::instance;
#endif
#if defined(USART5_BASE)
    case USART5_BASE: return _5::instance;
#elif defined(UART5_BASE)
    case UART5_BASE: return _5::instance;
#endif
#ifdef USART6_BASE
    case USART6_BASE: return _6::instance;
#endif
#if defined(USART7_BASE)
    case USART7_BASE: return _7::instance;
#elif defined(UART7_BASE)
    case UART7_BASE: return _7::instance;
#endif
#if defined(USART8_BASE)
    case USART8_BASE: return _8::instance;
#elif defined(UART8_BASE)
    case UART8_BASE: return _8::instance;
#endif
    default: System::DebugTrap("USART: unknown legacy instance");
    }
}

#endif
