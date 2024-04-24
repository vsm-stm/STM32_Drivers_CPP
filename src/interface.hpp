#ifndef INTERFACE_HPP_
#define INTERFACE_HPP_

#if defined(STM32F4)
#include <system_f4.hpp>
#elif defined(STM32F7)
#include <system_f7.hpp>
#endif
#include <dma.hpp>
#include <uart.hpp>

#include <vector>
#include <string.h>

template <uint32_t tx_buffer_size, uint32_t rx_buffer_size>
class Interface
{
public:
	/**
	 * @brief Enumeration for different interface types.
	 */
	enum class interface_types
	{
		USART,  ///< USART interface type
		SPI     ///< SPI interface type
	};

	/**
	 * @brief Constructor for Interface class.
	 * @param _usart Pointer to USART instance.
	 * @param _dma_tx Pointer to DMA instance for transmit.
	 * @param _dma_rx Pointer to DMA instance for receive.
	 */
	Interface(USART *_usart, DMA_Sx *_dma_tx, DMA_Sx *_dma_rx) :
		usart(_usart),
		dma_tx(_dma_tx),
		dma_rx(_dma_rx)
	{
		type = interface_types::USART;
	};

	/**
	 * @brief Initialize the Interface.
	 */
	void Init();

	/**
	 * @brief Send data over the interface.
	 * @param data Pointer to data to be sent.
	 * @param len Length of the data.
	 */
	void Send(uint8_t* data, uint16_t len);

	/**
	 * @brief Enable continuous receive mode.
	 */
	void Enable_Cont_Recieve();

	/**
	 * @brief Receive data from the interface.
	 */
	void Recieve();

	/**
	 * @brief Get the count of received data.
	 * @return Count of received data.
	 */
	uint32_t GetRxDataCount()
	{
		return rx.size();
	};

	/**
	 * @brief Get received data.
	 * @param data Pointer to buffer to store received data.
	 * @param size Size of data to retrieve.
	 */
	void GetRxData(uint8_t* data, uint32_t size)
	{
		memcpy(data, rx.front().data, size);
		rx.erase(rx.begin());
	};

	/**
	 * @brief IRQ handler for the interface.
	 */
	void IRQHandler(void);

	/**
	 * @brief Destructor for the Interface class.
	 */
	~Interface()
	{};

private:
	interface_types type;

	/**
	 * @brief Structure defining transmit data.
	 */
	typedef struct _tx_data
	{
		uint8_t data[tx_buffer_size];
		uint16_t len = tx_buffer_size;
	}tx_data_typedef;

	tx_data_typedef tmp_tx_data;

	/**
	 * @brief Structure defining receive data.
	 */
	typedef struct rx_data
	{
		uint8_t data[rx_buffer_size];
		uint16_t len = rx_buffer_size;
	}rx_data_typedef;

	rx_data_typedef tmp_rx_data;

	std::vector<tx_data_typedef> tx;
	std::vector<rx_data_typedef> rx;

	bool cont_rx = false;

	USART *usart;
	DMA_Sx *dma_tx;
	DMA_Sx *dma_rx;

	/**
	 * @brief Start the transmit process.
	 */
	void StartTranssmit();

	/**
	 * @brief Start the receive process.
	 */
	void StartReceiver();
};

/**
 * @brief Initialize the Interface.
 */
template <uint32_t tx_buffer_size, uint32_t rx_buffer_size>
void Interface<tx_buffer_size,rx_buffer_size>::Init()
{
	usart->SetUp();
	usart->Enable_DMA();
	dma_tx->SetUp();
	dma_tx->Enable_MINC();
	dma_rx->SetUp();
	dma_rx->Enable_MINC();
};

/**
 * @brief IRQ handler for the interface.
 */
template <uint32_t tx_buffer_size, uint32_t rx_buffer_size>
void Interface<tx_buffer_size,rx_buffer_size>::IRQHandler(void)
{
	// Handle Transmit Complete (TC) interrupt
	if((usart->USARTx->SR & USART_SR_TC)
	&& (usart->USARTx->CR1 & USART_CR1_TCIE))
	{
		usart->ClearFlags();

		if(tx.size() != 0)
		{
			tx.erase(tx.begin());
			tmp_tx_data = {};
		}

		if(tx.size() == 0)
		{
			usart->Disable_IRQ(USART::IRQ::TC);
		}
		else
		{
			StartTranssmit();
		}
	}

	// Handle Idle Line Detected interrupt
	if((usart->USARTx->SR & USART_SR_IDLE)
	&& (usart->USARTx->CR1 & USART_CR1_IDLEIE))
	{
		dma_rx->Disable_Stream();
		usart->USARTx->DR;

		rx.push_back(tmp_rx_data);
		tmp_rx_data = {};
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

/**
 * @brief Start the transmit process.
 */
template <uint32_t tx_buffer_size, uint32_t rx_buffer_size>
void Interface<tx_buffer_size,rx_buffer_size>::StartTranssmit()
{
	if(tx.size()>0)
	{
		tmp_tx_data = tx.front();

		dma_tx->DMA_Stream_X->M0AR = reinterpret_cast<uint32_t>(tmp_tx_data.data);
		dma_tx->DMA_Stream_X->NDTR = tmp_tx_data.len;
		usart->ClearFlags();
		usart->Enable_IRQ(USART::IRQ::TC);
		dma_tx->ClearFlags();
		dma_tx->Enable_Stream();
	}
}

/**
 * @brief Start the receive process.
 */
template <uint32_t tx_buffer_size, uint32_t rx_buffer_size>
void Interface<tx_buffer_size,rx_buffer_size>::StartReceiver()
{
	dma_rx->DMA_Stream_X->M0AR = reinterpret_cast<uint32_t>(tmp_rx_data.data);
	dma_rx->DMA_Stream_X->NDTR = sizeof(tmp_rx_data.data);
	usart->Enable_IRQ(USART::IRQ::IDLE);
	dma_rx->ClearFlags();
	dma_rx->Enable_Stream();
}

/**
 * @brief Send data over the interface.
 * @param data Pointer to data to be sent.
 * @param len Length of the data.
 */
template <uint32_t tx_buffer_size, uint32_t rx_buffer_size>
void Interface<tx_buffer_size,rx_buffer_size>::Send(uint8_t* data, uint16_t len)
{
	tx_data_typedef tmp;
	memcpy(tmp.data, data, len);
	tmp.len = len;
	tx.push_back(tmp);

	if(tx.size() == 1)
	{
		StartTranssmit();
	}       
};

/**
 * @brief Enable continuous receive mode.
 */
template <uint32_t tx_buffer_size, uint32_t rx_buffer_size>
void Interface<tx_buffer_size,rx_buffer_size>::Enable_Cont_Recieve()
{
	cont_rx = true;

	StartReceiver();
}

/**
 * @brief Receive data from the interface.
 */
template <uint32_t tx_buffer_size, uint32_t rx_buffer_size>
void Interface<tx_buffer_size,rx_buffer_size>::Recieve()
{
	cont_rx = false;

	StartReceiver();
}

#endif /* INTERFACE_HPP_ */
