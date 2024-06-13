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

class Interface_DMA
{
public:
	Interface_DMA(){};
	~Interface_DMA(){};
	void DMA_SetUp();
protected:
	DMA_Sx *dma_tx;
	DMA_Sx *dma_rx;
};

class Interface_USART : public Interface_DMA
{
public:
	Interface_USART(USART *_usart, DMA_Stream_TypeDef *_dma_tx, DMA_Stream_TypeDef *_dma_rx);
	~Interface_USART(){};

	SYS_StatusTypeDef Init();

	void Send(uint8_t* data, uint16_t data_len);
	void Receive(uint8_t* data, uint16_t data_len);
	void Receive(uint8_t* data, uint16_t data_len, bool cont);
	void IRQHandler();
	bool IsDataReceived;
protected:
	USART *usart;
	
	SYS_StatusTypeDef status_tx, status_rx;
	
	bool ContReceive;
};

class Interface_buffer
{
public:
	Interface_buffer(){};
	~Interface_buffer(){};

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
};

class Interface_buffer_USART : public Interface_buffer, public Interface_USART
{
public:
	Interface_buffer_USART(USART *_usart, DMA_Stream_TypeDef *_dma_tx, DMA_Stream_TypeDef *_dma_rx);

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
	/**
	 * @brief Start the transmit process.
	 */
	void StartTranssmit();

	/**
	 * @brief Start the receive process.
	 */
	void StartReceiver();
};

class Interface_SPI : public Interface_DMA
{
public:
	Interface_SPI(SPI *_spi, SPI::Init_struct_Typedef _init_data, DMA_Stream_TypeDef *_dma_tx, DMA_Stream_TypeDef *_dma_rx);
	~Interface_SPI(){};

	void Init();

	void Send_Receive(uint8_t* tx_data, uint8_t* rx_data, uint16_t data_len);

	void IRQHandler();
	bool IsDataReceived;
	SYS_StatusTypeDef GetStatus(){return status;};
protected:
	SPI *spi;
	SPI::Init_struct_Typedef spi_init_data;

	SYS_StatusTypeDef status;
};

class Interface_buffer_SPI : public Interface_buffer, public Interface_SPI
{
public:
	Interface_buffer_SPI(SPI *_spi, SPI::Init_struct_Typedef _init_data, DMA_Stream_TypeDef *_dma_tx, DMA_Stream_TypeDef *_dma_rx);

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

#endif /* #if defined(STM32F4) */

#endif /* INTERFACE_HPP_ */
