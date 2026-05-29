#include "spi.hpp"

#define SPI_DEFS_CPP
#include "spi_defs.hpp"
#undef  SPI_DEFS_CPP

// ---------------------------------------------------------------------------
// Common cleanup helper — called at the end of every transfer.
// Disables SPE, clears RXONLY (harmless if it was never set), deasserts SS.
// ---------------------------------------------------------------------------

static inline void spi_end_transfer(SPI_TypeDef* SPIx, SPI& self)
{
	(void)self;
	SPIx->CR1 &= ~(SPI_CR1_SPE | SPI_CR1_RXONLY);
}

// ---------------------------------------------------------------------------
// Hardware init
// ---------------------------------------------------------------------------

SysInitStatus SPI::SetHard()
{
	const PeriphInfo* info = nullptr;
	for (const auto& e : spi_table)
		if (e.periph == SPIx) { info = &e; break; }
	if (!info) return SysInitStatus::InitError;

	_info = info;
	*info->clk_reg |= info->clk_bit;
	*info->rst_reg |= info->rst_bit;
	*info->rst_reg &= ~info->rst_bit;

	_clk.SetUp(PIN::TYPE::AF_PushPull, PIN::OUTPUT_SPEED::High);

	if (Master_slave == Master_sel::Master)
		_mosi.SetUp(PIN::TYPE::AF_PushPull, PIN::OUTPUT_SPEED::High);
	else
		_mosi.SetUp(PIN::TYPE::AF_OD_PulUp, PIN::OUTPUT_SPEED::High);

	if (_miso.IsValid())
	{
		if (Master_slave == Master_sel::Master)
			_miso.SetUp(PIN::TYPE::AF_OD_PulUp);
		else
			_miso.SetUp(PIN::TYPE::AF_PushPull);
	}

	if (_ss.IsValid())
	{
		if (nss_ctrl == NSS_ctrl::Hard)
			_ss.SetUp(PIN::TYPE::AF_PushPull);
		else if (Master_slave == Master_sel::Master)
			_ss.SetUp(PIN::TYPE::OUTPUT_PushPull);
		else
			_ss.SetUp(PIN::TYPE::INPUT_NO_Pull);
	}

	return SysInitStatus::InitOK;
}

// ---------------------------------------------------------------------------
// SetUp
// ---------------------------------------------------------------------------

SysInitStatus SPI::SetUp(	Master_sel mstr, NSS_ctrl nss, TYPE type,
							Data_frame_format dff, Frame_Format ff,
							cPolPha cpolpha, BaudRate br)
{
	nss_ctrl     = nss;
	Master_slave = mstr;

	SysInitStatus status = SetHard();
	if (status != SysInitStatus::InitOK) return status;

	SPIx->CR1 = static_cast<uint32_t>(mstr) |
#if defined(STM32F4)
				static_cast<uint32_t>(dff) |
#endif
				static_cast<uint32_t>(ff)      |
				static_cast<uint32_t>(cpolpha) |
				static_cast<uint32_t>(br) << SPI_CR1_BR_Pos;

#if defined(STM32F7) || defined(STM32G0)
	SPIx->CR2 = static_cast<uint32_t>(dff);
#endif

	if (nss_ctrl == NSS_ctrl::Hard && mstr == Master_sel::Master)
		SPIx->CR2 |= SPI_CR2_SSOE;

	if (nss_ctrl == NSS_ctrl::Software && mstr == Master_sel::Slave)
		SPIx->CR1 |= SPI_CR1_SSM;

	if (type == TYPE::RX)
		SPIx->CR1 |= SPI_CR1_RXONLY;

	_ss.SetLevel(1);
	_status = SysStatus::OK;
	return SysInitStatus::InitOK;
}

// ---------------------------------------------------------------------------
// Blocking transfers
// ---------------------------------------------------------------------------

SysStatus SPI::Receive(uint8_t* data, uint16_t len, uint32_t timeout)
{
	if (!data || !len)              return SysStatus::Error;
	if (_status == SysStatus::Busy) return SysStatus::Busy;

	SPIx->CR1 |= SPI_CR1_RXONLY;

	uint32_t tick = System::GetTick();
	SlaveSelect(ENABLE);
	SPIx->CR1 |= SPI_CR1_SPE;  // clock starts immediately in RXONLY master mode

	for (uint16_t i = 0; i < len; i++)
	{
		while (!(SPIx->SR & SR_RXNE))
		{
			if (System::GetTick() - tick > timeout)
			{
				SPIx->CR1 &= ~(SPI_CR1_SPE | SPI_CR1_RXONLY);
				SlaveSelect(DISABLE);
				return SysStatus::Timeout;
			}
		}
		data[i] = static_cast<uint8_t>(RXD());
	}

	SPIx->CR1 &= ~(SPI_CR1_SPE | SPI_CR1_RXONLY);
	SlaveSelect(DISABLE);
	return SysStatus::OK;
}

SysStatus SPI::Send(uint8_t* data, uint16_t data_len, uint32_t timeout)
{
	if (_status == SysStatus::Busy) return SysStatus::Busy;

	uint32_t tick = System::GetTick();
	SlaveSelect(ENABLE);
	SPIx->CR1 |= SPI_CR1_SPE;

	for (uint32_t i = 0; i < data_len; i++)
	{
		while (!(SPIx->SR & SR_TXE))
		{
			if (System::GetTick() - tick > timeout)
				return SysStatus::Timeout;
		}
		*((volatile uint8_t*)&TXD()) = data[i];
	}

	while (SPIx->SR & SR_BSY)
	{
		if (System::GetTick() - tick > timeout)
			return SysStatus::Timeout;
	}

	SPIx->CR1 &= ~SPI_CR1_SPE;
	SlaveSelect(DISABLE);
	return SysStatus::OK;
}

SysStatus SPI::Send_Receive(uint8_t* tx_buf, uint8_t* rx_buf,
							uint16_t data_len, uint32_t timeout)
{
	if (_status == SysStatus::Busy) return SysStatus::Busy;

	uint32_t tick = System::GetTick();
	SlaveSelect(ENABLE);
	SPIx->CR1 |= SPI_CR1_SPE;

	for (uint32_t i = 0; i < data_len; i++)
	{
		*((volatile uint8_t*)&TXD()) = tx_buf[i];
		while (!(SPIx->SR & SR_RXNE))
		{
			if (System::GetTick() - tick > timeout)
				return SysStatus::Timeout;
		}
		rx_buf[i] = static_cast<uint8_t>(RXD());
	}

	while (SPIx->SR & SR_BSY)
	{
		if (System::GetTick() - tick > timeout)
			return SysStatus::Timeout;
	}

	SPIx->CR1 &= ~SPI_CR1_SPE;
	SlaveSelect(DISABLE);
	return SysStatus::OK;
}

// ---------------------------------------------------------------------------
// IRQ-driven transfers
// ---------------------------------------------------------------------------

SysStatus SPI::Send_IRQ(uint8_t* data, uint16_t len)
{
	if (!data || !len)              return SysStatus::Error;
	if (_status == SysStatus::Busy) return SysStatus::Busy;
	_status = SysStatus::Busy;

	tx_data    = { data, len };
	rx_data    = { nullptr, 0 };  // TX-only: OnTxEmpty handles cleanup
	_rx_active = false;
	SlaveSelect(ENABLE);
	SPIx->CR1 |= SPI_CR1_SPE;
	IRQ_en(IRQ::TXE, ENABLE);
	return SysStatus::OK;
}

SysStatus SPI::SendReceive_IRQ(uint8_t* tx, uint8_t* rx, uint16_t len)
{
	if (!tx || !rx || !len)         return SysStatus::Error;
	if (_status == SysStatus::Busy) return SysStatus::Busy;
	_status = SysStatus::Busy;

	tx_data    = { tx, len };
	rx_data    = { rx, len };  // full-duplex: OnRxByte handles cleanup
	_rx_active = true;
	SlaveSelect(ENABLE);
	SPIx->CR1 |= SPI_CR1_SPE;
	IRQ_en(IRQ::TXE, ENABLE);
	IRQ_en(IRQ::RXNE, ENABLE);
	return SysStatus::OK;
}

SysStatus SPI::Receive_IRQ(uint8_t* data, uint16_t len)
{
	if (!data || !len)              return SysStatus::Error;
	if (_status == SysStatus::Busy) return SysStatus::Busy;
	_status = SysStatus::Busy;

	rx_data    = { data, len };
	_rx_active = true;
	SPIx->CR1 |= SPI_CR1_RXONLY;
	SlaveSelect(ENABLE);
	SPIx->CR1 |= SPI_CR1_SPE;  // clock starts immediately
	IRQ_en(IRQ::RXNE, ENABLE);
	return SysStatus::OK;
}

// ---------------------------------------------------------------------------
// DMA attach
// ---------------------------------------------------------------------------

void SPI::AttachDMA(DMA_Sx* tx, DMA_Sx* rx)
{
	uint32_t tx_req = 0, rx_req = 0;
	for (const auto& e : spi_dma_req_table)
		if (e.periph == SPIx) { tx_req = e.tx_req; rx_req = e.rx_req; break; }

	if (tx)
	{
		DMA_Sx::StreamSettings cfg{};
		cfg.channel     = tx_req;
		cfg.direction   = DMA_Sx::DIR::To_Per;
		cfg.data_size   = DMA_Sx::SIZE::Byte;
		cfg.minc        = true;
		cfg.per_address = reinterpret_cast<uint32_t>(&SPIx->DR);
		tx->SetUp(cfg);

		IRQ_Registry::Register(tx->GetIRQn(), this);
		tx->Enable_IRQ(DMA_Sx::IRQ::TC);
		_dma_tx = tx;
	}

	if (rx)
	{
		DMA_Sx::StreamSettings cfg{};
		cfg.channel     = rx_req;
		cfg.direction   = DMA_Sx::DIR::From_Per;
		cfg.data_size   = DMA_Sx::SIZE::Byte;
		cfg.minc        = true;
		cfg.per_address = reinterpret_cast<uint32_t>(&SPIx->DR);
		rx->SetUp(cfg);

		// Register only if this DMA IRQ line is not already registered (shared channels).
		if (!tx || tx->GetIRQn() != rx->GetIRQn())
			IRQ_Registry::Register(rx->GetIRQn(), this);
		rx->Enable_IRQ(DMA_Sx::IRQ::TC);
		_dma_rx = rx;
	}
}

// ---------------------------------------------------------------------------
// DMA transfers
// ---------------------------------------------------------------------------

SysStatus SPI::SendDMA(uint8_t* data, uint32_t len, FunctionalState minc)
{
	if (!_dma_tx || !data || !len)  return SysStatus::Error;
	if (_status == SysStatus::Busy) return SysStatus::Busy;
	_status    = SysStatus::Busy;
	_rx_active = false;

	_dma_tx->ClearFlags();
	_dma_tx->MINC(minc);
	_dma_tx->SetMemAddr(reinterpret_cast<uint32_t>(data));
	_dma_tx->SetCount(len);

	SlaveSelect(ENABLE);
	SPIx->CR1 |= SPI_CR1_SPE;
	DMA_TX(ENABLE);
	_dma_tx->Stream_EN(ENABLE);
	return SysStatus::OK;
}

SysStatus SPI::ReceiveDMA(uint8_t* data, uint32_t len)
{
	if (!_dma_tx || !_dma_rx || !data || !len) return SysStatus::Error;
	if (_status == SysStatus::Busy)            return SysStatus::Busy;
	_status    = SysStatus::Busy;
	_rx_active = true;

	// TX: clock out 0xFF dummy bytes with no memory increment
	_dma_tx->ClearFlags();
	_dma_tx->MINC(DISABLE);
	_dma_tx->SetMemAddr(reinterpret_cast<uint32_t>(&spi_dummy_byte));
	_dma_tx->SetCount(len);

	// RX: capture incoming bytes into the user buffer
	_dma_rx->ClearFlags();
	_dma_rx->MINC(ENABLE);
	_dma_rx->SetMemAddr(reinterpret_cast<uint32_t>(data));
	_dma_rx->SetCount(len);

	SlaveSelect(ENABLE);
	SPIx->CR1 |= SPI_CR1_SPE;
	DMA_RX(ENABLE);
	DMA_TX(ENABLE);
	_dma_rx->Stream_EN(ENABLE);
	_dma_tx->Stream_EN(ENABLE);
	return SysStatus::OK;
}

SysStatus SPI::SendReceive_DMA(uint8_t* tx, uint8_t* rx, uint32_t len)
{
	if (!_dma_tx || !_dma_rx || !tx || !rx || !len) return SysStatus::Error;
	if (_status == SysStatus::Busy)                  return SysStatus::Busy;
	_status    = SysStatus::Busy;
	_rx_active = true;

	_dma_tx->ClearFlags();
	_dma_tx->MINC(ENABLE);
	_dma_tx->SetMemAddr(reinterpret_cast<uint32_t>(tx));
	_dma_tx->SetCount(len);

	_dma_rx->ClearFlags();
	_dma_rx->MINC(ENABLE);
	_dma_rx->SetMemAddr(reinterpret_cast<uint32_t>(rx));
	_dma_rx->SetCount(len);

	SlaveSelect(ENABLE);
	SPIx->CR1 |= SPI_CR1_SPE;
	DMA_RX(ENABLE);
	DMA_TX(ENABLE);
	_dma_rx->Stream_EN(ENABLE);
	_dma_tx->Stream_EN(ENABLE);
	return SysStatus::OK;
}

SysStatus SPI::Receive_DMA(uint8_t* data, uint32_t len)
{
	if (!_dma_rx || !data || !len)  return SysStatus::Error;
	if (_status == SysStatus::Busy) return SysStatus::Busy;
	_status    = SysStatus::Busy;
	_rx_active = true;

	_dma_rx->ClearFlags();
	_dma_rx->MINC(ENABLE);
	_dma_rx->SetMemAddr(reinterpret_cast<uint32_t>(data));
	_dma_rx->SetCount(len);

	SPIx->CR1 |= SPI_CR1_RXONLY;
	SlaveSelect(ENABLE);
	DMA_RX(ENABLE);
	_dma_rx->Stream_EN(ENABLE);
	SPIx->CR1 |= SPI_CR1_SPE;  // clock starts immediately
	return SysStatus::OK;
}

// ---------------------------------------------------------------------------
// IRQ dispatch
// ---------------------------------------------------------------------------

void SPI::HandleIRQ()
{
	// DMA TC checks — this handler is also registered for the DMA IRQ lines.
	if (_dma_tx && _dma_tx->GetTC_Flag())
	{
		_dma_tx->Stream_EN(DISABLE);
		_dma_tx->ClearFlags();
		OnDmaTxComplete();
	}
	if (_dma_rx && _dma_rx->GetTC_Flag())
	{
		_dma_rx->Stream_EN(DISABLE);
		_dma_rx->ClearFlags();
		OnDmaRxComplete();
	}

	// SPI peripheral flags — only dispatch if the interrupt source is enabled
	// in CR2, so TX-only transfers don't accidentally trigger OnRxByte.
	uint32_t cr2 = SPIx->CR2;
	uint32_t sr  = SPIx->SR;
	if ((cr2 & SPI_CR2_RXNEIE) && (sr & SR_RXNE))
		while (SPIx->SR & SR_RXNE) { OnRxByte(static_cast<uint8_t>(RXD())); }
	if ((cr2 & SPI_CR2_TXEIE)  && (sr & SR_TXE))  OnTxEmpty();
	if ((cr2 & SPI_CR2_ERRIE)  && (sr & SR_ERR))  OnError();
}

// ---------------------------------------------------------------------------
// Default virtual callbacks
// ---------------------------------------------------------------------------

void SPI::OnTxEmpty()
{
	if (tx_data.size > 0)
	{
		*((volatile uint8_t*)&TXD()) = *tx_data.ptr++;
		tx_data.size--;
	}
	else if (!_rx_active)
	{
		// TX-only: no RX pending — drain shift register and release bus.
		IRQ_en(IRQ::TXE, DISABLE);
		while (SPIx->SR & SR_BSY) {}
		SPIx->CR1 &= ~SPI_CR1_SPE;
		SlaveSelect(DISABLE);
		_status = SysStatus::OK;
	}
	else
	{
		// Full-duplex: TX done, last byte still shifting out.
		// Cleanup deferred to OnRxByte when the last RXNE fires.
		IRQ_en(IRQ::TXE, DISABLE);
	}
}

void SPI::OnRxByte(uint8_t byte)
{
	if (rx_data.size > 0)
	{
		*rx_data.ptr++ = byte;
		rx_data.size--;
		if (rx_data.size == 0)
		{
			IRQ_en(IRQ::RXNE, DISABLE);
			while (SPIx->SR & SR_BSY) {}
			SPIx->CR1 &= ~(SPI_CR1_SPE | SPI_CR1_RXONLY);
			SlaveSelect(DISABLE);
			_rx_active = false;
			_status    = SysStatus::OK;
		}
	}
}

void SPI::OnError() {}

void SPI::OnDmaTxComplete()
{
	DMA_TX(DISABLE);
	if (_rx_active)
		return;  // full-duplex DMA: cleanup deferred to OnDmaRxComplete

	// TX-only DMA: drain shift register and release bus.
	while (!(SPIx->SR & SR_TXE)) {}
	while (SPIx->SR & SR_BSY) {}
	SPIx->CR1 &= ~SPI_CR1_SPE;
	SlaveSelect(DISABLE);
	_status = SysStatus::OK;
}

void SPI::OnDmaRxComplete()
{
	DMA_TX(DISABLE);
	DMA_RX(DISABLE);
	while (SPIx->SR & SR_BSY) {}
	SPIx->CR1 &= ~(SPI_CR1_SPE | SPI_CR1_RXONLY);
	SlaveSelect(DISABLE);
	if (_dma_tx) _dma_tx->MINC(ENABLE);  // restore for next SendDMA
	_rx_active = false;
	_status    = SysStatus::OK;
}
