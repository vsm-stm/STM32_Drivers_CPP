#ifndef INTERFACE_HPP_
#define INTERFACE_HPP_

#if defined(STM32F4)
#include <system.hpp>
#include <dma.hpp>
#include <uart.hpp>
#include <spi.hpp>
#include <gpio.hpp>

#include <list>
#include <string.h>

class Interface
{
public:
	Interface(){};

	/**
	 * @brief Get the count of received data.
	 * @return Count of received data.
	 */
	uint32_t GetRxDataCount()
	{
		return rx.size();
	};

	uint32_t GetFirstRxDataSize()
	{
		return rx.front().len;
	};

	/**
	 * @brief Get received data.
	 * @param data Pointer to buffer to store received data.
	 * @param size Size of data to retrieve.
	 */
	void GetRxData(uint8_t* data, uint32_t size)
	{
		memcpy(data, rx.front().data_ptr, size);
		delete rx.front().data_ptr;
		rx.erase(rx.begin());
	};

	inline void SetUpDMA();

	/**
	 * @brief Destructor for the Interface class.
	 */
	~Interface(){};

protected:
	/**
	 * @brief Structure defining transmit data.
	 */
	typedef struct _tx_data
	{
		uint8_t *data_ptr;
		uint16_t len;
	}tx_data_typedef;

	/**
	 * @brief Structure defining receive data.
	 */
	typedef struct rx_data
	{
		uint8_t *data_ptr;
		uint16_t len;
	}rx_data_typedef;

	std::list<tx_data_typedef> tx;
	std::list<rx_data_typedef> rx;

	bool cont_rx = false;

	DMA_Sx *dma_tx;
	DMA_Sx *dma_rx;
};

class Interface_USART : public Interface
{
public:
	Interface_USART(USART *_usart, DMA_Stream_TypeDef *_dma_tx, DMA_Stream_TypeDef *_dma_rx);

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
	void Enable_Cont_Recieve(uint16_t len);

	/**
	 * @brief Receive data from the interface.
	 */
	void Recieve(uint16_t len);
	void IRQHandler(void);
private:
	USART *usart;

	/**
	 * @brief Start the transmit process.
	 */
	void StartTranssmit();

	/**
	 * @brief Start the receive process.
	 */
	void StartReceiver();

};



// template <uint32_t buffer_size>
class Interface_SPI : public Interface
{
public:
	Interface_SPI(SPI *_spi, SPI::Init_struct_Typedef _init_data, DMA_Stream_TypeDef *_dma_tx, DMA_Stream_TypeDef *_dma_rx);

	void Init();

	void IRQHandler(void);

	void Send_Receive(uint8_t *tx_data, uint16_t len);
	
	void Send(uint8_t *tx_data, uint16_t len);

	void Receive(uint16_t len);

	/**
	 * @brief Get the count of received data.
	 * @return Count of received data.
	 */
	uint32_t GetRxDataCount()
	{
		return rx.size();
	};

	uint32_t GetFirstRxDataSize()
	{
		return rx.front().len;
	};

	/**
	 * @brief Get received data.
	 * @param data Pointer to buffer to store received data.
	 * @param size Size of data to retrieve.
	 */
	void GetRxData(uint8_t* data, uint32_t size)
	{
		memcpy(data, rx.front().rx_data_ptr, size);
		delete [] rx.front().tx_data_ptr;
		delete [] rx.front().rx_data_ptr;
		rx.erase(rx.begin());
	};

private:
	SPI *spi;
	SPI::Init_struct_Typedef spi_init_data;

	typedef enum
	{
		none,
		RXTX,
		TX,
		RX
	}RXTX_Type;

	typedef struct _rxtx_data
	{
		// uint8_t tx_data[buffer_size];
		// uint8_t rx_data[buffer_size];
		uint8_t *tx_data_ptr;
		uint8_t *rx_data_ptr;
		uint16_t len;
		RXTX_Type type;
	}rxtx_data_typedef;

	std::list<rxtx_data_typedef> tx, rx;
	
	inline void StartTranssmit();

};

// template <uint32_t buffer_size>
// Interface_SPI<buffer_size>::Interface_SPI(SPI *_spi, SPI::Init_struct_Typedef _init_data, DMA_Stream_TypeDef *_dma_tx, DMA_Stream_TypeDef *_dma_rx) :
// 		Interface(),
// 		spi(_spi),
// 		spi_init_data(_init_data)
// 	{
// 		uint32_t ch_tx = 0, ch_rx = 0;

// 		if(spi->SPIx == SPI1)
// 		{
// 			ch_tx = 3;
// 			ch_rx = 3;
// 		}
// 		else if(spi->SPIx == SPI4)
// 		{
// 			if(_dma_tx == DMA2_Stream1)
// 				ch_tx = 4;
// 			else
// 				ch_tx = 5;
// 			if(_dma_rx == DMA2_Stream0)
// 				ch_rx = 4;
// 			else
// 				ch_rx = 5;
// 		}

// 		dma_tx = new DMA_Sx(_dma_tx, 
// 					ch_tx,
// 					reinterpret_cast<uint32_t>(&spi->SPIx->DR),
// 					DMA_Sx::Per_Type::spi,
// 					DMA_Sx::DIR::To_Per);
// 		dma_rx = new DMA_Sx(_dma_rx, 
// 					ch_rx,
// 					reinterpret_cast<uint32_t>(&spi->SPIx->DR),
// 					DMA_Sx::Per_Type::spi,
// 					DMA_Sx::DIR::From_Per);
// 	};

// template <uint32_t buffer_size>
// void Interface_SPI<buffer_size>::Init()
// {
// 	spi->SetUp(spi_init_data);
// 	spi->DMA_TX(ENABLE);
// 	spi->DMA_RX(ENABLE);

// 	SetUpDMA();
// };

// template <uint32_t buffer_size>
// void Interface_SPI<buffer_size>::Send_Receive(uint8_t *tx_data, uint16_t len)
// {
// 	rxtx_data_typedef tmp_data;
// 	tmp_data.len = len;
// 	tmp_data.type = RXTX_Type::RXTX;
// 	GPIOA->BSRR = GPIO_BSRR_BS10;
// 	tx.push_back(tmp_data);

// 	// tx.back().tx_data_ptr = new uint8_t[len];
// 	// tx.back().rx_data_ptr = new uint8_t[len];
// 	memcpy(tx.back().tx_data, tx_data, len);

// 	GPIOA->BSRR = GPIO_BSRR_BR10;
// 	if(tx.size() == 1)
// 	{
// 		StartTranssmit();
// 	}
// }

// template <uint32_t buffer_size>
// void Interface_SPI<buffer_size>::Send(uint8_t *tx_data, uint16_t len)
// {
// 	rxtx_data_typedef tmp_data;
// 	tmp_data.len = len;
// 	tmp_data.type = RXTX_Type::TX;

// 	GPIOA->BSRR = GPIO_BSRR_BS10;
// 	// tmp_data.tx_data_ptr = new uint8_t[len];
// 	// tmp_data.rx_data_ptr = new uint8_t[1];	

// 	memcpy(tmp_data.tx_data, tx_data, len);	

// 	tx.push_back(tmp_data);


// 	GPIOA->BSRR = GPIO_BSRR_BR10;
// 	if(tx.size() == 1)
// 	{
// 		StartTranssmit();
// 	}
		
// }

// template <uint32_t buffer_size>
// void Interface_SPI<buffer_size>::Receive(uint16_t len)
// {
// 	rxtx_data_typedef tmp_data;
// 	tmp_data.len = len;
// 	tmp_data.type = RXTX_Type::RX;

// 	tx.push_back(tmp_data);

// 	// tx.back().tx_data_ptr = new uint8_t[1];
// 	// tx.back().rx_data_ptr = new uint8_t[len];
// 	tx.back().tx_data[0] = 0;

// 	if(tx.size() == 1)
// 	{
// 		StartTranssmit();
// 	}
// }

// template <uint32_t buffer_size>
// void Interface_SPI<buffer_size>::StartTranssmit()
// {
// 	if(tx.size() == 0)
// 		return;

// 	dma_tx->ClearFlags();
// 	dma_rx->ClearFlags();

// 	dma_tx->DMA_Stream_X->M0AR = reinterpret_cast<uint32_t>(&tx.front().tx_data);
// 	dma_tx->DMA_Stream_X->NDTR = tx.front().len;
// 	dma_tx->MINC(ENABLE);

// 	dma_rx->DMA_Stream_X->M0AR = reinterpret_cast<uint32_t>(&tx.front().rx_data);
// 	dma_rx->DMA_Stream_X->NDTR = tx.front().len;
// 	dma_rx->MINC(ENABLE);

// 	if(tx.front().type == RXTX_Type::RX)
// 		dma_tx->MINC(DISABLE);
// 	if(tx.front().type == RXTX_Type::TX)
// 		dma_rx->MINC(DISABLE);

// 	dma_rx->Enable_IRQ(DMA_Sx::IRQ::TC);

// 	spi->SlaveSelect(ENABLE);

// 	spi->DMA_TX(ENABLE);
// 	spi->DMA_RX(ENABLE);

// 	dma_tx->Enable_Stream();
// 	dma_rx->Enable_Stream();

// 	spi->Enable();
// }

// template <uint32_t buffer_size>
// void Interface_SPI<buffer_size>::IRQHandler(void)
// {
// 	spi->Disable();
// 	spi->DMA_TX(DISABLE);
// 	spi->DMA_RX(DISABLE);

// 	spi->SlaveSelect(DISABLE);

// 	dma_rx->Disable_IRQ(DMA_Sx::IRQ::TC);

// 	if(tx.front().type != RXTX_Type::TX)
// 		rx.push_back(tx.front());
// 	// if(tx.front().type == RXTX_Type::TX)
// 	// {
// 	// 	delete [] rx.front().tx_data_ptr;
// 	// 	delete [] rx.front().rx_data_ptr;
// 	// }
// 	tx.erase(tx.begin());

// 	if(tx.size() != 0)
// 	{
// 		StartTranssmit();
// 	}

// }

#endif /* #if defined(STM32F4) */

#endif /* INTERFACE_HPP_ */
