#ifndef INTERFACE_HPP_
#define INTERFACE_HPP_

#ifndef PACKET_SIZE
#define PACKET_SIZE 256
#endif

#include <deque>

#include "system.hpp"
#include "dma.hpp"
#include "uart.hpp"
#include "spi.hpp"
#include "i2c.hpp"
#include "gpio.hpp"

class Interface_DMA
{
public:
	Interface_DMA(
		DMA_Sx_ns::DMA_Desc tx_desc,
		DMA_Sx_ns::DMA_Desc rx_desc)
		: dma_tx(tx_desc.Stream()), dma_rx(rx_desc.Stream()),
		  _tx_desc(tx_desc), _rx_desc(rx_desc)
		{};
	~Interface_DMA(){};
protected:
	DMA_Sx_ns::DMA_Sx      dma_tx;
	DMA_Sx_ns::DMA_Sx      dma_rx;
	DMA_Sx_ns::StreamSettings tx_settings;
	DMA_Sx_ns::StreamSettings rx_settings;
	DMA_Sx_ns::DMA_Desc    _tx_desc;
	DMA_Sx_ns::DMA_Desc    _rx_desc;

	enum class TXRX_Type
	{
		none,
		TXRX,
		TX,
		RX
	};
};

// template <typename data_typedef>
// class Buffer
// {
// public:
// 	Buffer(
// 		uint32_t tx_buffer_size,
// 		uint32_t rx_buffer_size) :
// 									tx_buffer_size_max(tx_buffer_size),
// 									rx_buffer_size_max(rx_buffer_size) {};
// 	~Buffer(){};

// 	const uint32_t tx_buffer_size_max;
// 	const uint32_t rx_buffer_size_max;

// 	std::list<data_typedef> tx;
// 	std::list<data_typedef> rx;

// 	uint32_t GetRxDataFirstSize() { return rx.front().size;};
// 	uint32_t GetRxDataCount() { return rx.size();};

// 	bool rx_buffer_full;

// };

class Interface_USART : public Interface_DMA
{
public:

#if defined(STM32F4) || defined(STM32F7)
	struct USART_1 {
		struct TX { static constexpr DMA_Sx_ns::DMA_Desc DMA2_S7 = { DMA2_Stream7_BASE, 4, USART1_BASE }; };
		struct RX {
			static constexpr DMA_Sx_ns::DMA_Desc DMA2_S2 = { DMA2_Stream2_BASE, 4, USART1_BASE };
			static constexpr DMA_Sx_ns::DMA_Desc DMA2_S5 = { DMA2_Stream5_BASE, 4, USART1_BASE };
		};
	};
	struct USART_2 {
		struct TX { static constexpr DMA_Sx_ns::DMA_Desc DMA1_S6 = { DMA1_Stream6_BASE, 4, USART2_BASE }; };
		struct RX { static constexpr DMA_Sx_ns::DMA_Desc DMA1_S5 = { DMA1_Stream5_BASE, 4, USART2_BASE }; };
	};
	struct USART_3 {
		struct TX {
			static constexpr DMA_Sx_ns::DMA_Desc DMA1_S3 = { DMA1_Stream3_BASE, 4, USART3_BASE };
			static constexpr DMA_Sx_ns::DMA_Desc DMA1_S4 = { DMA1_Stream4_BASE, 7, USART3_BASE };
		};
		struct RX { static constexpr DMA_Sx_ns::DMA_Desc DMA1_S1 = { DMA1_Stream1_BASE, 4, USART3_BASE }; };
	};
	struct UART_4 {
		struct TX { static constexpr DMA_Sx_ns::DMA_Desc DMA1_S4 = { DMA1_Stream4_BASE, 4, UART4_BASE }; };
		struct RX { static constexpr DMA_Sx_ns::DMA_Desc DMA1_S2 = { DMA1_Stream2_BASE, 4, UART4_BASE }; };
	};
	struct UART_5 {
		struct TX { static constexpr DMA_Sx_ns::DMA_Desc DMA1_S7 = { DMA1_Stream7_BASE, 4, UART5_BASE }; };
		struct RX { static constexpr DMA_Sx_ns::DMA_Desc DMA1_S0 = { DMA1_Stream0_BASE, 4, UART5_BASE }; };
	};
	struct USART_6 {
		struct TX {
			static constexpr DMA_Sx_ns::DMA_Desc DMA2_S6 = { DMA2_Stream6_BASE, 5, USART6_BASE };
			static constexpr DMA_Sx_ns::DMA_Desc DMA2_S7 = { DMA2_Stream7_BASE, 5, USART6_BASE };
		};
		struct RX {
			static constexpr DMA_Sx_ns::DMA_Desc DMA2_S1 = { DMA2_Stream1_BASE, 5, USART6_BASE };
			static constexpr DMA_Sx_ns::DMA_Desc DMA2_S2 = { DMA2_Stream2_BASE, 5, USART6_BASE };
		};
	};
#endif

	Interface_USART(
		USART *_usart,
		DMA_Sx_ns::DMA_Desc tx_desc,
		DMA_Sx_ns::DMA_Desc rx_desc,
		uint32_t tx_buffer_size,
		uint32_t rx_buffer_size);
	~Interface_USART(){};

	SysInitStatus Init();

	SysStatus Send(uint8_t* data, uint16_t data_size);
	SysStatus Receive(uint8_t* data, uint16_t data_size, bool cont);
	SysStatus Receive(uint8_t* data, uint16_t data_size);
	SysStatus SendBuffered(uint8_t* data, uint16_t data_size);
	SysStatus ReceiveBuffered(uint16_t data_size, bool cont = false);
	SysStatus ReadBufferedRx(uint8_t* data, uint16_t data_size);
	uint32_t GetBufferedRxCount() const { return static_cast<uint32_t>(rx_ready_queue.size()); }

	inline SysStatus GetRxStatus() const { return status_rx; }
	inline SysStatus GetTxStatus() const { return status_tx; }

	void Stop_Receive();

	void IRQHandler();
	bool IsDataReceived;
	uint16_t Receive_Count;
protected:
	USART *usart;

	SysStatus status_tx, status_rx;

	bool ContReceive;
	uint16_t Count_To_Receive;
	const uint32_t tx_buffer_size_max;
	const uint32_t rx_buffer_size_max;
	uint32_t tx_buffered_bytes;
	uint32_t rx_buffered_bytes;

	typedef struct _data
	{
		uint8_t *data_ptr;
		uint16_t size;
	}data_typedef;

	struct buffered_packet
	{
		uint8_t data[PACKET_SIZE];
		uint16_t size;
	};

	std::deque<buffered_packet> tx_pending_queue;
	std::deque<buffered_packet> rx_pending_queue;
	std::deque<buffered_packet> rx_ready_queue;
	bool tx_buffered_mode;
	bool rx_buffered_mode;
	bool tx_buffered_active;
	bool rx_buffered_active;

	// Buffer<data_typedef> buffer;

	inline void Change_baud(uint32_t new_baud) {usart->SetBaud(new_baud);};
	void StartBufferedTx();
	void StartBufferedRx();
};

class Interface_SPI : public Interface_DMA
{
public:

#if defined(STM32F4) || defined(STM32F7)
	struct SPI_1 {
		struct TX {
			static constexpr DMA_Sx_ns::DMA_Desc DMA2_S3 = { DMA2_Stream3_BASE, 3, SPI1_BASE };
			static constexpr DMA_Sx_ns::DMA_Desc DMA2_S5 = { DMA2_Stream5_BASE, 3, SPI1_BASE };
		};
		struct RX {
			static constexpr DMA_Sx_ns::DMA_Desc DMA2_S0 = { DMA2_Stream0_BASE, 3, SPI1_BASE };
			static constexpr DMA_Sx_ns::DMA_Desc DMA2_S2 = { DMA2_Stream2_BASE, 3, SPI1_BASE };
		};
	};
	struct SPI_2 {
		struct TX { static constexpr DMA_Sx_ns::DMA_Desc DMA1_S4 = { DMA1_Stream4_BASE, 0, SPI2_BASE }; };
		struct RX { static constexpr DMA_Sx_ns::DMA_Desc DMA1_S3 = { DMA1_Stream3_BASE, 0, SPI2_BASE }; };
	};
	struct SPI_3 {
		struct TX {
			static constexpr DMA_Sx_ns::DMA_Desc DMA1_S5 = { DMA1_Stream5_BASE, 0, SPI3_BASE };
			static constexpr DMA_Sx_ns::DMA_Desc DMA1_S7 = { DMA1_Stream7_BASE, 0, SPI3_BASE };
		};
		struct RX {
			static constexpr DMA_Sx_ns::DMA_Desc DMA1_S0 = { DMA1_Stream0_BASE, 0, SPI3_BASE };
			static constexpr DMA_Sx_ns::DMA_Desc DMA1_S2 = { DMA1_Stream2_BASE, 0, SPI3_BASE };
		};
	};
#ifdef SPI4_BASE
	struct SPI_4 {
		struct TX {
			static constexpr DMA_Sx_ns::DMA_Desc DMA2_S1 = { DMA2_Stream1_BASE, 4, SPI4_BASE };
			static constexpr DMA_Sx_ns::DMA_Desc DMA2_S4 = { DMA2_Stream4_BASE, 5, SPI4_BASE };
		};
		struct RX {
			static constexpr DMA_Sx_ns::DMA_Desc DMA2_S0 = { DMA2_Stream0_BASE, 4, SPI4_BASE };
			static constexpr DMA_Sx_ns::DMA_Desc DMA2_S3 = { DMA2_Stream3_BASE, 5, SPI4_BASE };
		};
	};
#endif
#endif

	Interface_SPI(
		SPI *_spi,
		SPI::Init_struct_Typedef _init_data,
		DMA_Sx_ns::DMA_Desc tx_desc,
		DMA_Sx_ns::DMA_Desc rx_desc,
		uint32_t tx_buffer_size,
		uint32_t rx_buffer_size);
	~Interface_SPI(){};

	SysInitStatus Init();

	SysStatus Send_Receive(uint8_t* tx_data, uint8_t* rx_data, uint16_t data_size);
	SysStatus Send(uint8_t* tx_data,uint16_t data_size);
	SysStatus Receive(uint8_t* rx_data, uint16_t data_size);

	// SYS_StatusTypeDef GetData(uint8_t* data, uint16_t size)
	// {
	// 	if(buffer.rx.size() == 0)
	// 		return SYS_ERROR;
	// 	memcpy(data, buffer.rx.front().rx_data_ptr, size);
	// 	delete [] buffer.rx.front().rx_data_ptr;
	// 	buffer.rx.erase(buffer.rx.begin());

	// 	return SYS_OK;
	// };

	// void GetReceivedData(uint8_t *data, uint16_t data_size)
	// {
	// 	if(data_size != buffer.GetRxDataFirstSize())
	// 		return;
	// 	GetData(data, data_size);
	// };

	void IRQHandler();
	bool IsDataReceived;
	inline SysStatus GetStatus(){return status;};
protected:
	SPI *spi;
	SPI::Init_struct_Typedef spi_init_data;

	uint8_t tmp_data[1];

	SysStatus status;

	typedef struct _rxtx_data
	{
		uint8_t *tx_data_ptr;
		uint8_t *rx_data_ptr;
		uint16_t size;
		TXRX_Type type;
	}rxtx_data_typedef;

	// Buffer<rxtx_data_typedef> buffer;

	inline SysStatus TXRX(uint8_t* tx_data, uint8_t* rx_data, uint16_t data_size, TXRX_Type type);
};

#if defined(STM32F7)
class Interface_I2C : public Interface_DMA
{
public:
	Interface_I2C(I2C *_i2c, DMA_Stream_TypeDef *_dma_tx, DMA_Stream_TypeDef *_dma_rx, uint32_t buffer_size);
	~Interface_I2C(){};

	SYS_StatusTypeDef Init();

	SYS_StatusTypeDef Send(uint8_t slave_addr, uint8_t* data, uint16_t data_size);
	SYS_StatusTypeDef Send(uint8_t slave_addr, uint8_t* reg_addr, uint8_t reg_addr_size, uint8_t* data, uint16_t data_size);
	SYS_StatusTypeDef Receive(uint8_t slave_addr, uint8_t* data, uint16_t data_size);
	SYS_StatusTypeDef Receive(uint8_t slave_addr, uint8_t* reg_addr, uint8_t reg_addr_size, uint8_t* data, uint16_t data_size);
	void IRQHandler();
	bool IsDataReceived;
	inline SYS_StatusTypeDef GetStatus(){return status;};
protected:
	I2C *i2c;
	
	SYS_StatusTypeDef status;

	TXRX_Type txrx;
	uint16_t transfer_count;
	uint8_t _slave_addr;
	uint8_t* data_addr;
	bool need_reload_dma;

	typedef struct _data
	{
		uint8_t slave_addr;
		bool use_reg_addr;
		uint8_t *reg_addr_ptr;
		uint16_t reg_addr_size;
		uint8_t *data_ptr;
		uint16_t size;
		TXRX_Type type;
	}data_typedef;

	// Buffer<data_typedef> buffer;
	inline SYS_StatusTypeDef Send_Receive(uint8_t slave_addr, uint8_t* reg_addr, uint8_t reg_addr_size, uint8_t* data, uint16_t data_size, TXRX_Type type);
	inline SYS_StatusTypeDef TXRX(uint8_t slave_addr, uint8_t* reg_addr, uint8_t reg_addr_size, uint8_t* data, uint16_t data_size, TXRX_Type type);
};

#endif

#endif /* INTERFACE_HPP_ */
