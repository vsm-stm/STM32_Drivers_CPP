#ifndef INTERFACE_HPP_
#define INTERFACE_HPP_

#include <system.hpp>
#include <dma.hpp>
#include <uart.hpp>
#include <spi.hpp>
#include <i2c.hpp>
#include <gpio.hpp>

#include <list>
#include <string.h>

class Interface_DMA
{
public:
	Interface_DMA(
		DMA_Stream_TypeDef *_dma_tx,
		DMA_Stream_TypeDef *_dma_rx):
										dma_tx(_dma_tx),
										dma_rx(_dma_rx)
		{};
	~Interface_DMA(){};
	SYS_StatusTypeDef DMA_SetUp();
protected:
	DMA_Sx dma_tx;
	DMA_Sx dma_rx;
	DMA_Sx::StreamSettings tx_settings;
	DMA_Sx::StreamSettings rx_settings;

	enum class TXRX_Type
	{
		none,
		TXRX,
		TX,
		TX_reg,
		RX,
		RX_reg
	};
};

template <typename data_typedef>
class Buffer
{
public:
	Buffer(
		uint32_t tx_buffer_size,
		uint32_t rx_buffer_size) :
									tx_buffer_size_max(tx_buffer_size),
									rx_buffer_size_max(rx_buffer_size) {};
	~Buffer(){};

	const uint32_t tx_buffer_size_max;
	const uint32_t rx_buffer_size_max;

	std::list<data_typedef> tx;
	std::list<data_typedef> rx;

	uint32_t GetRxDataFirstSize() { return rx.front().size;};
	uint32_t GetRxDataCount() { return rx.size();};
	SYS_StatusTypeDef GetData(uint8_t* data, uint16_t size)
	{
		if(rx.size() == 0)
			return SYS_ERROR;
		memcpy(data, rx.front().data_prt, size);
		delete [] rx.front().data_ptr;
		rx.erase(rx.begin());

		return SYS_OK;
	};

};

class Interface_USART : public Interface_DMA
{
public:
	Interface_USART(
		USART *_usart,
		DMA_Stream_TypeDef *_dma_tx,
		DMA_Stream_TypeDef *_dma_rx,
		uint32_t tx_buffer_size,
		uint32_t rx_buffer_size);
	~Interface_USART(){};

	SYS_StatusTypeDef Init();

	SYS_StatusTypeDef Send(uint8_t* data, uint16_t data_size);
	SYS_StatusTypeDef Receive(uint8_t* data, uint16_t data_size, bool cont);
	SYS_StatusTypeDef Receive(uint8_t* data, uint16_t data_size);
	SYS_StatusTypeDef Receive(uint16_t data_size, bool cont);
	SYS_StatusTypeDef Receive(uint16_t data_size);	
	void IRQHandler();
	bool IsDataReceived;
protected:
	USART *usart;
	
	SYS_StatusTypeDef status_tx, status_rx;
	
	bool ContReceive;

	typedef struct _data
	{
		uint8_t *data_ptr;
		uint16_t size;
	}_data_typedef;

	Buffer<_data_typedef> buffer;

	inline SYS_StatusTypeDef TX(uint8_t* data, uint16_t data_size);
	inline SYS_StatusTypeDef RX(uint8_t* data, uint16_t data_size);
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

class Interface_SPI : public Interface_DMA
{
public:
	Interface_SPI(
		SPI *_spi,
		SPI::Init_struct_Typedef _init_data,
		DMA_Stream_TypeDef *_dma_tx,
		DMA_Stream_TypeDef *_dma_rx,
		uint32_t tx_buffer_size);
	~Interface_SPI(){};

	SYS_StatusTypeDef Init();

	SYS_StatusTypeDef Send_Receive(uint8_t* tx_data, uint8_t* rx_data, uint16_t data_size);
	SYS_StatusTypeDef Send(uint8_t* tx_data,uint16_t data_size);
	SYS_StatusTypeDef Receive(uint8_t* rx_data, uint16_t data_size);

	void IRQHandler();
	bool IsDataReceived;
	SYS_StatusTypeDef GetStatus(){return status;};
protected:
	SPI *spi;
	SPI::Init_struct_Typedef spi_init_data;

	uint8_t tmp_data[1];

	SYS_StatusTypeDef status;

	typedef struct _rxtx_data
	{
		uint8_t *tx_data_ptr;
		uint8_t *data_ptr;
		uint16_t size;
		TXRX_Type type;
	}rxtx_data_typedef;

	Buffer<rxtx_data_typedef> buffer;

	inline SYS_StatusTypeDef TXRX(uint8_t* tx_data, uint8_t* rx_data, uint16_t data_size, TXRX_Type type);
};

#if defined(STM32F7)
class Interface_I2C : public Interface_DMA
{
public:
	Interface_I2C(I2C *_i2c, DMA_Stream_TypeDef *_dma_tx, DMA_Stream_TypeDef *_dma_rx);
	~Interface_I2C(){};

	SYS_StatusTypeDef Init();

	void Send(uint8_t slave_addr, uint8_t* data, uint16_t data_len);
	void SendToAddr(uint8_t slave_addr, uint8_t* addr, uint8_t addr_size, uint8_t* data, uint16_t data_len);
	void Receive(uint8_t slave_addr, uint8_t* data, uint16_t data_len);
	void ReceiveFromAddr(uint8_t slave_addr, uint8_t* addr, uint8_t addr_size, uint8_t* data, uint16_t data_len);
	void IRQHandler();
	bool IsDataReceived;
protected:
	I2C *i2c;
	
	SYS_StatusTypeDef status;

	TXRX_Type txrx;
	uint16_t transfer_count;
	uint8_t _slave_addr;
	uint8_t* data_addr;
	bool need_reload_dma;
};

class Interface_buffer_I2C : public Interface_buffer, public Interface_I2C
{
public:
	Interface_buffer_I2C(I2C *_i2c, DMA_Stream_TypeDef *_dma_tx, DMA_Stream_TypeDef *_dma_rx);
	~Interface_buffer_I2C(){};

	void IRQHandler(void);

	void Send(uint8_t slave_addr, uint8_t* data, uint16_t data_len);
	void SendToAddr(uint8_t slave_addr, uint8_t* addr, uint8_t addr_size, uint8_t* data, uint16_t data_len);
	void Receive(uint8_t slave_addr, uint8_t* data, uint16_t data_len);
	void ReceiveFromAddr(uint8_t slave_addr, uint8_t* addr, uint8_t addr_size, uint8_t* data, uint16_t data_len);


	uint32_t GetRxDataCount()
	{
		return rx.size();
	};

	uint32_t GetFirstRxDataSize()
	{
		return rx.front().data_len;
	};

	/**
	 * @brief Get received data.
	 * @param data Pointer to buffer to store received data.
	 * @param size Size of data to retrieve.
	 */
	void GetRxData(uint8_t* data, uint32_t size)
	{
		memcpy(data, rx.front().data_ptr, size);
		delete [] rx.front().data_ptr;
		delete [] rx.front().data_ptr;
		rx.erase(rx.begin());
	};
protected:
	typedef struct _data
	{
		uint8_t slave_addr;
		bool use_reg_addr;
		uint8_t *reg_addr_ptr;
		uint16_t reg_addr_len;
		uint8_t *data_ptr;
		uint16_t data_len;
		TXRX_Type type;
	}Data_Typedef;

	std::list<Data_Typedef> tx, rx;

	inline void StartTranssmit();

};
#endif
#endif /* INTERFACE_HPP_ */
