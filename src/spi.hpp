/**
 * @file    spi.hpp
 * @brief   SPI driver for STM32 (F4/F7/G0 families).
 *
 * Thin C++ wrapper around the STM32 SPI peripheral supporting blocking,
 * interrupt-driven, and DMA-based transfers.  Design mirrors the USART driver.
 *
 * Key design decisions
 * --------------------
 * - **Compile-time pin validation**: SCK/MOSI/MISO/SS pins are chosen from
 *   `SPI::_N::SCK` / `SPI::_N::MOSI` etc. (N = peripheral number) whose
 *   members carry the correct alternate-function index and peripheral base
 *   address.  The constructor asserts (hard-fault trap) on a mismatch.
 *
 * - **Register abstraction**: `TXD()`, `RXD()`, `SR_TXE`, `SR_RXNE`, `SR_BSY`
 *   are private helpers that keep the register layout out of higher-level code.
 *   F4/F7/G0 all share the same SPI_SR / SPI_DR layout; the only family split
 *   is the data-frame-format field (CR1::DFF on F4, CR2::DS on F7/G0).
 *
 * - **IRQ integration**: Inherits `IIRQHandler`.  Call `EnableNVIC_IRQ()` once
 *   after `SetUp()`; the driver registers itself in `IRQ_Registry`.  Override
 *   the virtual callbacks (`OnTxEmpty`, `OnRxByte`, etc.) in a subclass to
 *   customise transfer behaviour without re-implementing `HandleIRQ`.
 *
 * ============================================================================
 * Usage — blocking transfer
 * ============================================================================
 * @code
 *   SPI spi(SPI1, SPI::_1::SCK::PA5, SPI::_1::MOSI::PA7);
 *   spi.SetUp(SPI::Master_sel::Master, SPI::NSS_ctrl::Software,
 *             SPI::TYPE::TX, SPI::Data_frame_format::Byte,
 *             SPI::Frame_Format::MSB, SPI::cPolPha::None, SPI::BaudRate::DIV4);
 *   spi.Send(buf, sizeof(buf), 1000);
 * @endcode
 *
 * ============================================================================
 * Usage — interrupt-driven TX
 * ============================================================================
 * @code
 *   struct MySPI : SPI {
 *       using SPI::SPI;
 *       void OnTxEmpty() override { ... }  // optional; default feeds tx_data[]
 *   };
 *   MySPI spi(SPI1, SPI::_1::SCK::PA5, SPI::_1::MOSI::PA7);
 *   spi.SetUp(...);
 *   spi.EnableNVIC_IRQ();
 *   spi.Send_IRQ(buf, sizeof(buf));
 * @endcode
 *
 * ============================================================================
 * Usage — DMA TX
 * ============================================================================
 * @code
 *   SPI    spi(SPI1, SPI::_1::SCK::PA5, SPI::_1::MOSI::PA7);
 *   DMA_Sx dma_tx(DMA1_Channel3);   // G0: any free channel
 *
 *   spi.SetUp(...);
 *   spi.AttachDMA(&dma_tx, nullptr);  // configures DMA internally
 *   spi.EnableNVIC_IRQ();             // registers SPI IRQ for DMA-TC dispatch
 *   spi.SendDMA(buf, sizeof(buf));
 * @endcode
 *
 * Notes
 * -----
 *  - `AttachDMA` calls `DMA_Sx::SetUp()` internally — do NOT call it beforehand.
 *  - `ReceiveDMA` requires both TX and RX DMA channels (dummy 0xFF bytes on MOSI
 *    clock the peripheral while MISO data is captured into the user buffer).
 *  - Peripheral clock enable and GPIO setup are handled by `SetUp()`.
 */

#ifndef SPI_HPP_
#define SPI_HPP_

#include "system.hpp"
#include "rcc.hpp"
#include "gpio.hpp"
#include "dma.hpp"
#include "irq_registry.hpp"

class SPI : public IIRQHandler
{
public:
	// -----------------------------------------------------------------------
	// Configuration enumerations
	// -----------------------------------------------------------------------

	enum class Master_sel {
		Slave  = 0,
		Master = SPI_CR1_SSM | SPI_CR1_SSI | SPI_CR1_MSTR
	};

	enum class NSS_ctrl {
		Hard = 0,
		Software
	};

	enum class Data_frame_format {
#if defined(STM32F4)
		Byte      = 0,
		Half_Word = SPI_CR1_DFF
#elif defined(STM32F7) || defined(STM32G0)
		Byte      = 0b0111 << SPI_CR2_DS_Pos,
		Half_Word = 0b1111 << SPI_CR2_DS_Pos
#endif
	};

	enum class Frame_Format {
		MSB = 0,
		LSB = SPI_CR1_LSBFIRST
	};

	enum class TYPE {
		TXRX = 0,
		TX   = 1,
		RX   = 2,
	};

	enum class cPolPha {
		None    = 0,
		cPha    = SPI_CR1_CPHA,
		cPol    = SPI_CR1_CPOL,
		cPolPha = SPI_CR1_CPHA | SPI_CR1_CPOL
	};

	enum class BaudRate {
		DIV2 = 0,
		DIV4,
		DIV8,
		DIV16,
		DIV32,
		DIV64,
		DIV128,
		DIV256
	};

	/**
	 * @brief SPI interrupt source.
	 * Values map directly to the corresponding CR2 enable bits (same for F4/F7/G0).
	 */
	enum class IRQ {
		TXE  = SPI_CR2_TXEIE,
		RXNE = SPI_CR2_RXNEIE,
		ERR  = SPI_CR2_ERRIE
	};

	// -----------------------------------------------------------------------
	// Compile-time pin tables — peripheral-specific nested structs.
	//
	// Each member is a constexpr PIN that encodes: GPIO port base, pin number,
	// alternate-function index, and the owning SPI base address.
	// Zero RAM footprint — values exist only in the compiler's constant pool.
	// -----------------------------------------------------------------------

#include "spi_defs.hpp"

	// -----------------------------------------------------------------------
	// Constructor
	// -----------------------------------------------------------------------

	/**
	 * @brief Constructs a SPI driver instance.
	 *
	 * Validates at construction time that each provided pin belongs to the
	 * correct SPI peripheral.  A mismatch triggers a breakpoint trap.
	 *
	 * @param spix  Pointer to the hardware peripheral (e.g. SPI1).
	 * @param sck   SCK pin from SPI::_N::SCK::<PXn>.
	 * @param mosi  MOSI pin from SPI::_N::MOSI::<PXn>.
	 * @param miso  MISO pin from SPI::_N::MISO::<PXn>.  Pass PIN{} to omit.
	 * @param ss    SS pin from SPI::_N::SS::<PXn>.      Pass PIN{} for software SS.
	 */
	explicit SPI(SPI_TypeDef* spix,
				 PIN sck,
				 PIN mosi,
				 PIN miso = PIN{},
				 PIN ss   = PIN{})
		: SPIx(spix), _clk(sck), _mosi(mosi), _miso(miso), _ss(ss)
	{
		if (sck.IsValid()  && sck.periph_base  && sck.periph_base  != (uint32_t)spix)
			System::DebugTrap("SPI: SCK  pin belongs to wrong peripheral");
		if (mosi.IsValid() && mosi.periph_base && mosi.periph_base != (uint32_t)spix)
			System::DebugTrap("SPI: MOSI pin belongs to wrong peripheral");
		if (miso.IsValid() && miso.periph_base && miso.periph_base != (uint32_t)spix)
			System::DebugTrap("SPI: MISO pin belongs to wrong peripheral");
		if (ss.IsValid()   && ss.periph_base   && ss.periph_base   != (uint32_t)spix)
			System::DebugTrap("SPI: SS   pin belongs to wrong peripheral");
	}

	SPI() = delete;
	SPI(SPI const&)            = delete;
	SPI(SPI&&)                 = delete;
	SPI& operator=(SPI const&) = delete;
	SPI& operator=(SPI&&)      = delete;
	~SPI() = default;

	// -----------------------------------------------------------------------
	// Init helpers
	// -----------------------------------------------------------------------

	typedef struct {
		Master_sel        mstr;
		NSS_ctrl          nss_ctrl;
		TYPE              type;
		Data_frame_format dff;
		Frame_Format      ff;
		cPolPha           cpolpha;
		BaudRate          br;
	} Init_struct_Typedef;

	SysInitStatus SetUp(Master_sel mstr, TYPE type)
	{
		return SetUp(mstr, NSS_ctrl::Hard, type,
					 Data_frame_format::Byte, Frame_Format::MSB, cPolPha::None, BaudRate::DIV2);
	}

	SysInitStatus SetUp(Init_struct_Typedef s)
	{
		return SetUp(s.mstr, s.nss_ctrl, s.type, s.dff, s.ff, s.cpolpha, s.br);
	}

	SysInitStatus SetUp(Master_sel mstr, NSS_ctrl nss, TYPE type,
						Data_frame_format dff, Frame_Format ff,
						cPolPha cpolpha, BaudRate br);

	// -----------------------------------------------------------------------
	// Runtime control
	// -----------------------------------------------------------------------

	inline void Enable()  { SPIx->CR1 |=  SPI_CR1_SPE; }
	inline void Disable() { SPIx->CR1 &= ~SPI_CR1_SPE; }

	inline void SlaveSelect(FunctionalState en)
	{
		if (nss_ctrl == NSS_ctrl::Software)
		{
			if (en)
			{
				if (Master_slave == Master_sel::Master) _ss.SetLevel(0);
				else SPIx->CR1 &= ~SPI_CR1_SSI;
			}
			else
			{
				if (Master_slave == Master_sel::Master) _ss.SetLevel(1);
				else SPIx->CR1 |= SPI_CR1_SSI;
			}
		}
	}

	inline void DMA_TX(FunctionalState en)
	{
		if (en) SPIx->CR2 |=  SPI_CR2_TXDMAEN;
		else    SPIx->CR2 &= ~SPI_CR2_TXDMAEN;
	}

	inline void DMA_RX(FunctionalState en)
	{
		if (en) SPIx->CR2 |=  SPI_CR2_RXDMAEN;
		else    SPIx->CR2 &= ~SPI_CR2_RXDMAEN;
	}

	/**
	 * @brief Enables or disables a SPI interrupt source in CR2.
	 * On first enable: registers in IRQ_Registry and unmasks NVIC.
	 * On last disable: unregisters and masks NVIC.
	 */
	void IRQ_en(IRQ irq, FunctionalState en)
	{
		if (_info == nullptr) return;
		if (en) {
			SPIx->CR2 |= static_cast<uint32_t>(irq);
			if (!NVIC_GetEnableIRQ(_info->irq)) {
				IRQ_Registry::Register(_info->irq, this);
				NVIC_EnableIRQ(_info->irq);
			}
		} else {
			SPIx->CR2 &= ~static_cast<uint32_t>(irq);
			if (!(SPIx->CR2 & (SPI_CR2_TXEIE | SPI_CR2_RXNEIE | SPI_CR2_ERRIE))) {
				IRQ_Registry::Unregister(_info->irq);
				NVIC_DisableIRQ(_info->irq);
			}
		}
	}

	// -----------------------------------------------------------------------
	// Blocking transfers
	// -----------------------------------------------------------------------

	/**
	 * @brief Transmits a byte buffer in blocking mode.
	 * Polls TXE for each byte; returns Timeout if the flag stalls.
	 */
	SysStatus Send(uint8_t* data, uint16_t data_len, uint32_t timeout = 100);

	/**
	 * @brief Receives a byte buffer in blocking RXONLY mode.
	 *
	 * Temporarily sets RXONLY if not already active, enables SPE (clock
	 * starts immediately), polls RXNE for each byte, then disables SPE and
	 * restores the original RXONLY state.
	 */
	SysStatus Receive(uint8_t* data, uint16_t len, uint32_t timeout = 100);

	/**
	 * @brief Full-duplex blocking transfer — TX and RX simultaneously.
	 * @param tx_data  Source buffer (sent on MOSI).
	 * @param rx_data  Destination buffer (received on MISO).
	 */
	SysStatus Send_Receive(uint8_t* tx_data, uint8_t* rx_data,
						   uint16_t data_len, uint32_t timeout = 100);

	// -----------------------------------------------------------------------
	// Interrupt-driven TX
	// -----------------------------------------------------------------------

	/**
	 * @brief Starts a non-blocking IRQ-driven TX transfer.
	 *
	 * Enables the TXE interrupt; the default OnTxEmpty() feeds bytes from
	 * the buffer one at a time and deasserts SS when finished.
	 * Override OnTxEmpty() in a subclass for custom behaviour.
	 *
	 * @return SysStatus::Busy if a transfer is already in progress.
	 */
	SysStatus Send_IRQ(uint8_t* data, uint16_t len);

	/**
	 * @brief Starts a non-blocking IRQ-driven RX-only transfer.
	 *
	 * Sets RXONLY, enables SPE (clock starts), then enables RXNE interrupt.
	 * OnRxByte() captures each byte; RXONLY is restored to its original
	 * state after the last byte is received.
	 *
	 * @return SysStatus::Busy if a transfer is already in progress.
	 */
	SysStatus Receive_IRQ(uint8_t* data, uint16_t len);

	/**
	 * @brief Starts a non-blocking IRQ-driven full-duplex transfer.
	 *
	 * Enables TXE and RXNE interrupts simultaneously.  OnTxEmpty() feeds
	 * @p tx bytes; OnRxByte() captures bytes into @p rx.  SS is deasserted
	 * and SPE disabled after the last RXNE fires.
	 *
	 * @return SysStatus::Busy if a transfer is already in progress.
	 */
	SysStatus SendReceive_IRQ(uint8_t* tx, uint8_t* rx, uint16_t len);

	// -----------------------------------------------------------------------
	// DMA transfers
	// -----------------------------------------------------------------------

	/**
	 * @brief Attaches DMA channels and configures them internally.
	 *
	 * Calls DMA_Sx::SetUp() — do NOT call it separately beforehand.
	 * Direction, peripheral address (DR), data width (Byte) and minc are
	 * set automatically from the SPI peripheral pointer.
	 * Registers this instance as the IRQ handler for each DMA TC line.
	 *
	 * @param tx  DMA channel for transmit (nullptr to skip TX DMA).
	 * @param rx  DMA channel for receive  (nullptr to skip RX DMA).
	 */
	void AttachDMA(DMA_Sx* tx = nullptr, DMA_Sx* rx = nullptr);

	/**
	 * @brief Starts a DMA TX transfer.
	 *
	 * Asserts SS, enables SPE, starts DMA.  OnDmaTxComplete() is called
	 * when the TC interrupt fires.
	 * 
	 * @param data Source buffer to send on MOSI.
	 * @param len  Number of bytes to send.
	 * @param minc Whether to increment the memory address after each byte.
	 * @return SysStatus::Busy if a transfer is already in progress.
	 */
	SysStatus SendDMA(uint8_t* data, uint32_t len, FunctionalState minc = ENABLE);

	/**
	 * @brief Starts a DMA full-duplex transfer (RX captured, TX = 0xFF dummy).
	 *
	 * Clocks 0xFF on MOSI to drive the bus while capturing MISO into @p data.
	 * Requires both TX and RX DMA channels to have been attached via AttachDMA().
	 * OnDmaRxComplete() fires on completion.
	 *
	 * @return SysStatus::Error if channels are not attached.
	 *         SysStatus::Busy  if a transfer is already in progress.
	 */
	SysStatus ReceiveDMA(uint8_t* data, uint32_t len);

	/**
	 * @brief Starts a DMA RX-only transfer (no TX DMA channel needed).
	 *
	 * Sets RXONLY, enables SPE (clock starts immediately), starts RX DMA.
	 * OnDmaRxComplete() fires when done and restores RXONLY to its original state.
	 *
	 * @return SysStatus::Error if RX channel is not attached.
	 *         SysStatus::Busy  if a transfer is already in progress.
	 */
	SysStatus Receive_DMA(uint8_t* data, uint32_t len);

	/**
	 * @brief Starts a DMA full-duplex transfer with actual TX data.
	 *
	 * TX DMA sends @p tx; RX DMA captures MISO into @p rx simultaneously.
	 * Requires both TX and RX DMA channels.  OnDmaRxComplete() fires when
	 * all bytes have been received (TX DMA TC defers cleanup to RX DMA TC).
	 *
	 * @return SysStatus::Error if channels are not attached.
	 *         SysStatus::Busy  if a transfer is already in progress.
	 */
	SysStatus SendReceive_DMA(uint8_t* tx, uint8_t* rx, uint32_t len);

	// -----------------------------------------------------------------------
	// Status query
	// -----------------------------------------------------------------------

	/** @brief Returns the current transfer status (Busy while any operation is in progress). */
	inline SysStatus GetStatus() const { return _status; }

private:
	// -----------------------------------------------------------------------
	// Register-name abstraction layer
	//
	// F4/F7/G0 share the same SPI_DR register for both TX and RX.
	// SR flags are identical across all three families.
	// -----------------------------------------------------------------------

	inline volatile uint32_t& TXD() const { return SPIx->DR; }
	inline volatile uint32_t& RXD() const { return SPIx->DR; }

	static constexpr uint32_t SR_TXE  = SPI_SR_TXE;
	static constexpr uint32_t SR_RXNE = SPI_SR_RXNE;
	static constexpr uint32_t SR_BSY  = SPI_SR_BSY;
	static constexpr uint32_t SR_ERR  = SPI_SR_OVR | SPI_SR_MODF | SPI_SR_CRCERR;

	// -----------------------------------------------------------------------
	// Peripheral info table (defined in spi.cpp)
	// -----------------------------------------------------------------------

	struct PeriphInfo {
		SPI_TypeDef*        periph;
		volatile uint32_t*  clk_reg;
		uint32_t            clk_bit;
		volatile uint32_t*  rst_reg;
		uint32_t            rst_bit;
		uint32_t const*     bus_clk;
		IRQn_Type           irq;
	};
	static const PeriphInfo spi_table[];

	// -----------------------------------------------------------------------
	// Transfer state
	// -----------------------------------------------------------------------

	struct DataBuf { uint8_t* ptr; uint16_t size; };

	DataBuf   tx_data{};
	DataBuf   rx_data{};

	SysStatus _status{ SysStatus::NotInit };

	DMA_Sx*   _dma_tx = nullptr;
	DMA_Sx*   _dma_rx = nullptr;

	bool      _rx_active = false; ///< True when an RX channel is part of the current DMA transfer.

	// -----------------------------------------------------------------------
	// IRQ dispatch — HandleIRQ is the single entry point called by IRQ_Registry.
	// Override virtual callbacks to customise behaviour without touching dispatch.
	// -----------------------------------------------------------------------

	void HandleIRQ() override final;

	/** @brief Called when TX data register is empty (IRQ-driven TX). */
	virtual void OnTxEmpty();
	/** @brief Called for each received byte (IRQ-driven or DMA-less RX). */
	virtual void OnRxByte(uint8_t byte);
	/** @brief Called on OVR / MODF / CRCERR. */
	virtual void OnError();
	/** @brief Called when the TX DMA channel signals Transfer Complete. */
	virtual void OnDmaTxComplete();
	/** @brief Called when the RX DMA channel signals Transfer Complete. */
	virtual void OnDmaRxComplete();

	SysInitStatus SetHard();

protected:
	SPI_TypeDef* SPIx;  ///< Hardware peripheral register block.
	PIN _clk{};
	PIN _mosi{};
	PIN _miso{};
	PIN _ss{};

	NSS_ctrl   nss_ctrl{};
	Master_sel Master_slave{};

	const PeriphInfo* _info = nullptr;
};

#endif /* SPI_HPP_ */
