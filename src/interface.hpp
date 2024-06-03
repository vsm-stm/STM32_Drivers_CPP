#ifndef INTERFACE_HPP_
#define INTERFACE_HPP_

#if defined(STM32F4)
#include <system.hpp>
#include <dma.hpp>
#include <uart.hpp>
#include <spi.hpp>

#include <vector>
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

	std::vector<tx_data_typedef> tx;
	std::vector<rx_data_typedef> rx;

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

class Interface_SPI : public Interface
{
public:
	Interface_SPI(SPI *_spi, SPI::Init_struct_Typedef _init_data, DMA_Stream_TypeDef *_dma_tx, DMA_Stream_TypeDef *_dma_rx);

	void Init();

	void IRQHandler(void);

	void Send_Receive(uint8_t *tx_data, uint16_t len);
	
	void Send(uint8_t *tx_data, uint16_t len);

	void Receive(uint16_t len);

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
	
	RXTX_Type curr_rxtx = RXTX_Type::none;
	void StartTranssmit(RXTX_Type rxtx);


};

#endif /* #if defined(STM32F4) */

#endif /* INTERFACE_HPP_ */
