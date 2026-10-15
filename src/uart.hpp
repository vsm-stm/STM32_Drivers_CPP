/**
 * @file    uart.hpp
 * @brief   USART/UART driver for STM32F4, STM32F7 and STM32G0.
 *
 * One object controls one peripheral. Three transfer modes are available:
 * - Send()/Receive(): blocking polling with a timeout for the whole call.
 * - Send_IRQ()/Receive_IRQ(): asynchronous byte/FIFO interrupt transfers.
 * - Send_DMA()/Receive_DMA(): asynchronous transfers using attached DMA resources.
 *
 * Initialization and IRQ ownership
 * -------------------------------
 * Initialize System/RCC before SetUp(). PCLK is the default; on F7/G0 the
 * constructor can select SYSCLK, HSI or LSE where the instance supports them.
 * SetUp() uses oversampling x16 and 8N1; G0 PRESC is /1. Peripheral tables provide clock
 * gates, PCLK variables, vectors, pins and DMAReq routes for the selected MCU.
 * TX/RX pins come from USART::_N::TX/RX. Their number/AF are defined by PIN;
 * the USART constructor checks the owning peripheral at run time.
 *
 * SetUp() registers the UART IRQ once; AttachDMA() registers unique DMA vectors.
 * Do not register this object manually. IRQ_en() changes peripheral source bits
 * only, so completion callbacks may disable a source without changing the registry.
 * SetUp(), AttachDMA() and DeInit() run in main, never from a callback.
 * Shared IRQ capacity is configured in irq_registry_settings.hpp.
 *
 * Packet reception
 * ----------------
 * Receive_IRQ()/Receive_DMA() receive one packet, ending at IDLE or buffer capacity.
 * DMA is stopped and its final counter/cache state is resolved before publishing
 * the ready flag. GetDataReceivedCount() is a pure read; clear the ready flag
 * explicitly after consuming the packet, then start the next receive.
 * The receiver remains enabled between packets: this API does not guarantee
 * continuous reception without gaps. A new Receive discards stale input/errors.
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
 *   USART uart(USART1, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7);
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
 * See AttachDMA()/Receive_DMA() below for DMA examples, and UART.md for details.
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
 * @brief Owns one USART/UART and its registered IRQ/DMA integration.
 *
 * TX and RX can operate independently. Configuration and buffer handover for
 * one object must be serialized by its caller; no internal locks are provided.
 * Do not copy/move the object. Keep it, attached DMA objects and active buffers
 * alive until their operations and registrations are stopped by DeInit().
 */
class USART : public IIRQHandler
{
public:
	// Named pin tables, FIFO enums and family-specific register accessors.
	// For example USART::_1::TX::PB6 stores port, pin, AF and USART1 ownership.
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
	 * @see DMA_en(), AttachDMA()
	 */
	enum class DMA {
		TXRX = USART_CR3_DMAR | USART_CR3_DMAT, ///< Both UART request directions.
		TX = USART_CR3_DMAT,                    ///< UART TX requests only.
		RX = USART_CR3_DMAR                     ///< UART RX requests only.
	};
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
	 * @brief Store peripheral, baud and pins; hardware is configured by SetUp().
	 *
	 * A valid pin carrying a different peripheral base calls System::DebugTrap().
	 * This ownership check is performed at run time; construction is not SetUp().
	 *
	 * @param usartx Hardware USART/UART pointer, for example USART1.
	 * @param baudrate Requested bits per second; validated against the kernel clock.
	 * @param tx Named USART::_N::TX pin, or PIN{} to leave TX unconfigured.
	 * @param rx Named USART::_N::RX pin, or PIN{} to leave RX unconfigured.
	 * @param clock_source F7/G0 only: kernel clock, PCLK by default. Immutable;
	 *        SetUp() applies the choice but never starts an oscillator. An unavailable
	 *        source or an HSI/LSE source that is not enabled and ready gives InitError.
	 *
	 * TX-only example:
	 * @code
	 *   USART log(USART2, 115200, USART::_2::TX::PA2, PIN{});
	 *   SysInitStatus status = log.SetUp();
	 * @endcode
	 *
	 * F7/G0 HSI example, after the caller has enabled HSI and waited for HSIRDY:
	 * @code
	 *   USART link(USART1, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7,
	 *              USART::ClockSource::HSI);
	 *   SysInitStatus status = link.SetUp();
	 * @endcode
	 */
	explicit USART(USART_TypeDef* usartx, uint32_t baudrate,
		PIN tx = PIN{}, PIN rx = PIN{}
#if defined(STM32F7) || defined(STM32G0)
		, ClockSource clock_source = ClockSource::PCLK
#endif
		)
		: USARTx(usartx), BaudRate(baudrate), _TX(tx), _RX(rx)
#if defined(STM32F7) || defined(STM32G0)
		, _clock_source(clock_source)
#endif
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
	/**
	 * @brief Does not stop hardware or unregister automatically.
	 * @pre Call DeInit() before destroying an initialized/registered object.
	 */
	~USART() = default;

	/**
	 * @brief Enable the clock, configure GPIO/UART and register the UART IRQ.
	 *
	 * Uses the constructor's kernel clock (PCLK on F4), oversampling x16,
	 * 8 data bits, no parity and one stop bit. BRR is rounded from
	 * (kernel_clock + baud/2) / baud; it must fit 16..65535,
	 * and requested baud must not exceed kernel_clock/16. G0 PRESC is /1.
	 * HSI/LSE must already be enabled and ready; SetUp() only checks their state.
	 * SetUp() registers this object once and enables the UART NVIC vector.
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
	SysInitStatus SetUp(FIFO fifo = FIFO::NO, FIFO_TH fifo_th_tx = FIFO_TH::NO,
		FIFO_TH fifo_th_rx = FIFO_TH::NO);

	/**
	 * @brief Enable/disable one UART interrupt source without changing the registry.
	 *
	 * SetUp() owns the UART registration until DeInit(). This method neither
	 * registers/unregisters handlers nor changes NVIC, and is usable in callbacks.
	 * Before SetUp() it has no effect. FIFO sources require SetUp(FIFO::EN).
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
		if (en) reg |= static_cast<uint32_t>(irq);
		else reg &= ~static_cast<uint32_t>(irq);
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
	 */
	inline void DMA_en(FunctionalState en, DMA dma = DMA::TXRX)
	{
		if (en) USARTx->CR3 |= static_cast<uint32_t>(dma);
		else USARTx->CR3 &= ~static_cast<uint32_t>(dma);
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
	 */
	enum class ISR_FLAGS {
		PE = USART_ICR_PECF,
		FE = USART_ICR_FECF,
		NE = USART_ICR_NCF,
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
	 * @brief Configure and attach TX and/or RX DMA resources to this UART.
	 * @param tx TX DMA object, or nullptr to omit TX DMA.
	 * @param rx RX DMA object, or nullptr to omit RX DMA. At least one is required.
	 * @return InitOK after configuration/registration; InitError for a missing
	 *         UART setup, wrong DMAReq route, repeated attachment, the same object
	 *         for TX/RX, active transfer, an owned resource or setup/IRQ failure.
	 *
	 * Calls DMA SetUp() with Byte, MINC, direction and DR/TDR/RDR address; do not
	 * configure the DMA separately first. The full stream/CHSEL pair (F4/F7)
	 * or request ID (G0) is checked against UART DMAReq routes.
	 * Configures TC/TE and F4/F7 DME interrupts. Shared TX/RX vectors register
	 * this object once. Failure rolls back only resources acquired by this call.
	 * DMA remains attached after a transfer; reassignment requires DeInit(),
	 * which releases the resources. DMA objects must outlive the attachment.
	 *
	 * G0 example (choose existing DMA channels on the selected MCU):
	 * @code
	 *   DMA_Sx tx(DMA1_Channel4, DMA_Sx::Req::Usart1::TX);
	 *   DMA_Sx rx(DMA1_Channel5, DMA_Sx::Req::Usart1::RX);
	 *   if (uart.SetUp() == SysInitStatus::InitOK) {
	 *       SysInitStatus attached = uart.AttachDMA(&tx, &rx);
	 *       // Check attached before using Send_DMA()/Receive_DMA().
	 *   }
	 * @endcode
	 *
	 * F4/F7 construction (stream and CHSEL are part of DMAReq):
	 * @code
	 *   DMA_Sx tx(DMA_Sx::Req::Usart1::TX_DMA2_Stream7);
	 *   DMA_Sx rx(DMA_Sx::Req::Usart1::RX_DMA2_Stream5); // CH4, RX.
	 *   // uart.SetUp(), then uart.AttachDMA(&tx, &rx), checking both results.
	 * @endcode
	 *
	 * Call from main, never from IRQ/callback. Repeated attachment is an error
	 * until DeInit(), including an attempt to add a second direction later.
	 */
	SysInitStatus AttachDMA(DMA_Sx* tx = nullptr, DMA_Sx* rx = nullptr);
	/**
	 * @brief Start one DMA transmission using the attached TX resource.
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
	 * disable DMAR, stop DMA and wait for EN=0, read its remaining count,
	 * invalidate the bytes written by DMA, retain a fitting UART/FIFO tail,
	 * then publish RX OK and ready/count. Errors instead publish RX Error and
	 * invoke OnError(), without a ready packet.
	 *
	 * F7 cacheable RX storage must own every touched 32-byte cache line, including
	 * padding. Do not share these lines with unrelated data or access them from
	 * CPU while Busy. System skips cache maintenance for a range wholly within
	 * one configured non-cacheable DMA region; buffer ownership still applies.
	 * F4/G0 cache helpers are empty.
	 *
	 * Example after successful SetUp()/AttachDMA():
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
	 * Writes BRR with UE=0, restores CR1 and stores the new BaudRate so it is
	 * retained for a later setup. Uses the same rounded x16 divider and the same
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
	 * @param parity Ignored. SetUp() configures no parity.
	 */
	inline void SetParity(uint32_t parity) { (void)parity; }

	/**
	 * @brief Stop this UART, release attached DMA and remove its IRQ registrations.
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
	 *   // DMA resources are released. Check a new SetUp()/AttachDMA() before use.
	 * @endcode
	 */
	void DeInit();

private:
	USART_TypeDef* USARTx; ///< Controlled hardware register block.
	uint32_t BaudRate;     ///< Requested baud retained across DeInit()/SetUp().

	/**
	 * @brief One row of the static peripheral table defined by uart_defs.hpp.
	 * SetUp() looks up the hardware pointer once and retains the selected row.
	 * bus_clk points to the System PCLK variable maintained by RCC setup.
	 */
	struct PeriphInfo {
		USART_TypeDef* periph;
		volatile uint32_t* clk_reg;
		uint32_t clk_bit;
		const uint32_t* bus_clk;
		IRQn_Type irq;
#if defined(STM32F7) || defined(STM32G0)
		uint32_t clock_select; ///< Two-bit RCC selector mask; zero means fixed PCLK.
#endif
	};
	static const PeriphInfo usart_table[];
	PIN _TX; ///< Optional TX pin and stored AF.
	PIN _RX; ///< Optional RX pin and stored AF.
#if defined(STM32F7) || defined(STM32G0)
	const ClockSource _clock_source; ///< Constructor choice, retained across DeInit().
#endif
	/// Selected nominal kernel frequency; zero for an unavailable/not-ready source.
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
	DMA_Sx* _dma_tx = nullptr;
	DMA_Sx* _dma_rx = nullptr;
	const PeriphInfo* _info = nullptr;
	/**
	 * @brief Registry entry point shared by the UART and its DMA vectors.
	 * Processes enabled sources of active operations, acknowledges flags and
	 * invokes the callbacks below. Application subclasses override callbacks,
	 * not this dispatch method.
	 */
	void HandleIRQ() override final;

protected:
	/**
	 * @name Peripheral callbacks
	 * Called in IRQ for asynchronous operations. Keep application work bounded;
	 * process completed packets in main. OnError() from blocking Receive() runs
	 * in the calling context instead.
	 *
	 * OnRxByte/OnTxEmpty/OnIdle/OnDmaTxComplete/OnDmaRxComplete perform the standard
	 * buffer/state handling. Call their base implementation before adding logic
	 * if that handling is required. OnTC and OnError are notifications with empty
	 * defaults. Never change the IRQ registry or call SetUp/AttachDMA/DeInit here.
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
	 *   Link link(USART1, 115200, USART::_1::TX::PB6, USART::_1::RX::PB7);
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
	 * With DMA: disable requests, stop/wait, count and invalidate written data.
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

#endif
