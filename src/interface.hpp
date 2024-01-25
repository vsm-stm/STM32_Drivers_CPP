#ifndef INTERFACE_HPP_
#define INTERFACE_HPP_

#include <system_f4.hpp>
#include <dma.hpp>
#include <uart.hpp>

#include <vector>
#include <malloc.h>
#include <string.h>

template <typename Tx, typename Rx>
class Interface
{
public:

	enum class interface_types
	{
		USART,
		SPI
	};

	Interface(USART *_usart, DMA_Sx *_dma_tx, DMA_Sx *_dma_rx, uint32_t size) :
		usart(_usart),
		dma_tx(_dma_tx),
		dma_rx(_dma_rx)
	{
		type = interface_types::USART;
		tx_data = static_cast<uint8_t*>(calloc(sizeof(uint8_t), size)); assert(tx_data);
	};

	void Init();

	void Send(Tx data, uint16_t len);
	void Send(Tx data);

	void Enable_Cont_Recieve(uint32_t len);
	void Enable_Cont_Recieve();
	void Recieve(Rx data, uint32_t len);

	uint32_t GetRxDataCount()
	{
		return rx.size();
	};
	Rx GetRxData()
	{
		rx_data_typedef tmp = rx.front();
		rx.erase(rx.begin());
		return tmp.data;
	};

	inline void IRQHandler(void);

	~Interface()
	{};

private:
	interface_types type;
	uint8_t *tx_data;

	typedef struct _tx_data
	{
		Tx data;
		uint32_t address;
		uint16_t len;
	}tx_data_typedef;
	std::vector<tx_data_typedef> tx;

	typedef struct rx_data
	{
		Rx data;
		uint32_t len;
	}rx_data_typedef;
	std::vector<rx_data_typedef> rx;
	rx_data_typedef tmp_rx_data;
	bool cont_rx = false;

	USART *usart;
	DMA_Sx *dma_tx;
	DMA_Sx *dma_rx;

	void StartTranssmit(uint32_t data_addr, uint32_t size);
	void StartReceiver(uint32_t data_addr, uint32_t size);
};


template <typename Tx, typename Rx>
void Interface<Tx,Rx>::Init()
{
	usart->SetUp();
	usart->Enable_DMA();
	dma_tx->SetUp();
	dma_tx->Enable_MINC();
	dma_rx->SetUp();
	dma_rx->Enable_MINC();
};

template <typename Tx, typename Rx>
void Interface<Tx,Rx>::IRQHandler(void)
{
	if((usart->USARTx->SR & USART_SR_TC)
	&& (usart->USARTx->CR1 & USART_CR1_TCIE))
	{
		usart->ClearFlags();

		if(tx.size() != 0)
		{
			tx.erase(tx.begin());
		}

		if(tx.size() == 0)
		{
			usart->Disable_IRQ(USART::IRQ::TC);
		}
		else
		{
			StartTranssmit(tx.front().address, tx.front().len);
		}
	}

	if((usart->USARTx->SR & USART_SR_IDLE)
	&& (usart->USARTx->CR1 & USART_CR1_IDLEIE))
	{
		dma_rx->Disable_Stream();
		usart->USARTx->DR;

		rx.push_back(tmp_rx_data);
		tmp_rx_data = {0};
		if(cont_rx)
		{
			dma_rx->ClearFlags();
			dma_rx->Enable_Stream();
		}
		else
		{
			usart->Disable_IRQ(USART::IRQ::IDLE);
		}
	}
}


template <typename Tx, typename Rx>
void Interface<Tx,Rx>::StartTranssmit(uint32_t data_addr, uint32_t size)
{
	memcpy(tx_data, reinterpret_cast<uint8_t*>(data_addr), size);
	dma_tx->DMA_Stream_X->M0AR = reinterpret_cast<uint32_t>(tx_data);
	dma_tx->DMA_Stream_X->NDTR = size;
	usart->ClearFlags();
	usart->Enable_IRQ(USART::IRQ::TC);
	dma_tx->ClearFlags();
	dma_tx->Enable_Stream();
}

template <typename Tx, typename Rx>
void Interface<Tx,Rx>::StartReceiver(uint32_t data_addr, uint32_t size)
{
	dma_rx->DMA_Stream_X->M0AR = data_addr;
	dma_rx->DMA_Stream_X->NDTR = size;
	usart->Enable_IRQ(USART::IRQ::IDLE);
	dma_rx->ClearFlags();
	dma_rx->Enable_Stream();
}

template <typename Tx, typename Rx>
void Interface<Tx,Rx>::Send(Tx data)
{
	tx.push_back({data,0,sizeof(data)});
	tx.back().address = reinterpret_cast<uint32_t>(&tx.back().data);

	if(tx.size() == 1)
	{
		StartTranssmit(tx.front().address, tx.front().len);
	}
};

template <typename Tx, typename Rx>
void Interface<Tx,Rx>::Send(Tx data, uint16_t len)
{
	tx.push_back({data,0,len});
	tx.back().address = reinterpret_cast<uint32_t>(tx.back().data);

	if(tx.size() == 1)
	{
		StartTranssmit(tx.front().address, tx.front().len);
	}		
};

template <typename Tx, typename Rx>
void Interface<Tx,Rx>::Enable_Cont_Recieve()
{
	tmp_rx_data.len = sizeof(Rx);
	cont_rx = true;

	StartReceiver(reinterpret_cast<uint32_t>(&tmp_rx_data.data), tmp_rx_data.len);
}
template <typename Tx, typename Rx>
void Interface<Tx,Rx>::Recieve(Rx data, uint32_t len)
{
	cont_rx = false;

	tmp_rx_data.len = len;
	tmp_rx_data.data = data;

	StartReceiver(reinterpret_cast<uint32_t>(tmp_rx_data.data), tmp_rx_data.len);
}
#endif /* INTERFACE_HPP_ */
