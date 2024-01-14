#ifndef INTERFACE_HPP_
#define INTERFACE_HPP_

#include <system_f4.hpp>
#include <dma.hpp>
#include <uart.hpp>

#include <vector>

template <typename Tx, typename Rx>
class Interface
{
public:

	enum class interface_types
	{
		USART,
		SPI
	};

	Interface(USART *_usart, DMA_Sx *_dma_tx, DMA_Sx *_dma_rx) :
		usart(_usart),
		dma_tx(_dma_tx),
		dma_rx(_dma_rx)
	{
		type = interface_types::USART;
	};

	void Init();

	void Send(Tx data, uint32_t len);

	void Recieve(uint32_t len)
	{
		Recieve(len, false);
	};

	void Recieve(uint32_t len, bool cont)
	{
		rx_len = len;
		cont_rx = cont;
		rx_data_typedef tmp = {};
		rx.push_back(tmp);

		dma_rx->DMA_Stream_X->M0AR = reinterpret_cast<uint32_t>(rx.end());
		dma_rx->DMA_Stream_X->NDTR = rx_len;
		usart->Enable_IRQ(USART::IRQ::IDLE);
		dma_rx->ClearFlags();
		dma_rx->Enable_Stream();
	};

	void IRQHandler(void);

	~Interface()
	{};

private:
	interface_types type;

	typedef struct tx_data
	{
		Tx data;
		uint32_t len;
	}tx_data_typedef;
	std::vector<tx_data_typedef> tx;
	typedef struct rx_data
	{
		Rx data;
		uint32_t len;
	}rx_data_typedef;
	std::vector<rx_data_typedef> rx;
	uint32_t rx_len;
	bool cont_rx = false;

	USART *usart;
	DMA_Sx *dma_tx;
	DMA_Sx *dma_rx;

	void StartTranssmit(uint32_t data_addr, uint32_t size);
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

		tx.erase(tx.begin());
		if(tx.empty())
		{
			usart->Disable_IRQ(USART::IRQ::TC);
		}
		else
		{
			tx_data_typedef tmp;
			tmp = tx.front();
			StartTranssmit(reinterpret_cast<uint32_t>(&tx.front()), tmp.len);
		}
	}

	if((usart->USARTx->SR & USART_SR_IDLE)
	&& (usart->USARTx->CR1 & USART_CR1_IDLEIE))
	{
		
		if(cont_rx)
		{

		}
	}
}


template <typename Tx, typename Rx>
void Interface<Tx,Rx>::StartTranssmit(uint32_t data_addr, uint32_t size)
{
	usart->Enable_IRQ(USART::IRQ::TC);
	dma_tx->DMA_Stream_X->M0AR = data_addr;
	dma_tx->DMA_Stream_X->NDTR = size;
	dma_tx->ClearFlags();
	dma_tx->Enable_Stream();
}

template <typename Tx, typename Rx>
void Interface<Tx,Rx>::Send(Tx data, uint32_t len)
{
	tx_data_typedef tmp;
	tmp.data = data;
	tmp.len = len;

	tx.push_back(tmp);

	if(tx.size() == 1)
	{
		StartTranssmit(reinterpret_cast<uint32_t>(&tx.front()), len);
	}		
};

#endif /* INTERFACE_HPP_ */
