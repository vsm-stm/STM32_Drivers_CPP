#ifndef DMA_H_
#define DMA_H_

#include <system.hpp>
#include <rcc.hpp>


class DMA_Sx
{
public:
	/**
	 * @brief Enumeration for data transfer direction.
	 */
	enum class DIR
	{
		From_Per,	///< Data transfer from peripheral to memory
		To_Per,		///< Data transfer from memory to peripheral
		Mem_To_Mem	///< Memory to memory data transfer
	};

	/**
	 * @brief Enumeration for data transfer size.
	 */
	enum class SIZE
	{
		Byte,		///< Data transfer size in bytes
		Half_Word,	///< Data transfer size in half-words
		Word 		///< Data transfer size in words
	};

	/**
	 * @brief Enumeration for DMA interrupts.
	 */
	enum class IRQ
	{
		TC = DMA_SxCR_TCIE, 	///< Transfer Complete interrupt
		HTC = DMA_SxCR_HTIE,	///< Half Transfer Complete interrupt
		TE = DMA_SxCR_TEIE, 	///< Transfer Error interrupt
		DME = DMA_SxCR_DMEIE 	///< Direct Mode Error interrupt
	};

	/**
	 * @brief Enumeration for peripheral types.
	 */
	enum class Per_Type
	{
		usart,	///< Peripheral type: USART
		spi,	///< Peripheral type: SPI
		i2c, 	///< Peripheral type: I2C
		adc		///< Peripheral type: ADC
	};

	/**
	 * @brief Structure to hold DMA stream settings.
	 */
	struct StreamSettings
	{
		DMA_Stream_TypeDef *dma_sx;	///< Pointer to the DMA stream
		uint32_t channel;			///< DMA channel number
		uint32_t per_addr;			///< Peripheral address
		Per_Type type;				///< Type of peripheral
		DIR dir;					///< Data transfer direction
		SIZE size;					///< Data transfer size
	};

	DMA_Stream_TypeDef *DMA_Stream_X;

	/**
	 * @brief Constructor using initialization list.
	 */
	explicit DMA_Sx(StreamSettings setup) :
			DMA_Stream_X(setup.dma_sx),
			_direction(setup.dir),
			_channel(setup.channel),
			_paddr(setup.per_addr),
			_per_type(setup.type),
			_size(setup.size)
	{
	};

	/**
	 * @brief Constructor with individual parameters.
	 */
	explicit DMA_Sx(DMA_Stream_TypeDef *dma_sx,
		   uint32_t channel,
		   uint32_t per_addr,
		   Per_Type type,
		   DIR dir,
		   SIZE size) :
			DMA_Stream_X(dma_sx),
			_direction(dir),
			_channel(channel),
			_paddr(per_addr),
			_per_type(type),
			_size(size)
	{
	};

	explicit DMA_Sx(DMA_Stream_TypeDef *dma_sx,
		   uint32_t channel,
		   uint32_t per_addr,
		   Per_Type type,
		   DIR dir) :
			DMA_Stream_X(dma_sx),
			_direction(dir),
			_channel(channel),
			_paddr(per_addr),
			_per_type(type),
			_size(SIZE::Byte)
	{
	};

	/**
	 * @brief Function to set up DMA stream.
	 * @return Status of the setup operation.
	 */
	SYS_StatusTypeDef SetUp();
	/**
	 * @brief Function to set memory address for the DMA stream.
	 * @param addr Memory address.
	 * @return Status of the memory address setting operation.
	 */
	SYS_StatusTypeDef SetMemAddr(uint32_t addr);

	/**
	 * @brief Overloaded function to set memory address and size for the DMA stream.
	 * @param addr Memory address.
	 * @param size Size of the memory transfer.
	 * @return Status of the memory address and size setting operation.
	 */
	SYS_StatusTypeDef SetMemAddr(uint32_t addr, uint16_t size);

	/**
	 * @brief Function to clear DMA flags.
	 */
	inline void ClearFlags()
	{
		*DMA_CFR =	(DMA_LISR_TCIF0 << cfr_offset) |
					(DMA_LISR_HTIF0 << cfr_offset) |
					(DMA_LISR_FEIF0 << cfr_offset) |
					(DMA_LISR_TEIF0 << cfr_offset);
	};

	bool GetTC_Flag()
	{ return ((*DMA_SR & (DMA_LISR_TCIF0_Msk << cfr_offset)) >> cfr_offset);};

	/**
	 * @brief Enable the specified USART IRQ.
	 * @param irq The IRQ to enable.
	 */
	void Enable_IRQ(IRQ irq)
	{
		DMA_Stream_X->CR |= static_cast<uint32_t>(irq);

		if (!(NVIC_GetEnableIRQ(IRQ_vector)))
		{
			NVIC_EnableIRQ(IRQ_vector);
		}
	}

	/**
	 * @brief Disable the specified USART IRQ.
	 * @param irq The IRQ to disable.
	 */
	void Disable_IRQ(IRQ irq)
	{
		DMA_Stream_X->CR &= ~(static_cast<uint32_t>(irq));
		if (!(DMA_Stream_X->CR & (DMA_SxCR_TCIE | DMA_SxCR_HTIE)))
		{
			NVIC_DisableIRQ(IRQ_vector);
		}
	}

	inline void MINC(FunctionalState en)
	{	if(en)
			DMA_Stream_X->CR |=  DMA_SxCR_MINC;
		else
			DMA_Stream_X->CR &= ~DMA_SxCR_MINC;
	};

	inline void CIRC(FunctionalState en)
	{
		if(en)
			DMA_Stream_X->CR |=  DMA_SxCR_CIRC;
		else
			DMA_Stream_X->CR &= ~DMA_SxCR_CIRC;
	};

	/**
	 * @brief Function to enable the DMA stream.
	 */
	inline void Enable_Stream()
	{	DMA_Stream_X->CR |= DMA_SxCR_EN;};

	/**
	 * @brief Function to disable the DMA stream.
	 */
	inline void Disable_Stream()
	{	DMA_Stream_X->CR &= ~DMA_SxCR_EN;};

private:
	DMA_TypeDef *DMA_controller;	///< Pointer to the DMA controller
	uint32_t *DMA_CFR;				///< Pointer to the DMA CLear Flags Register
	uint32_t *DMA_SR;				///< Pointer to the DMA Status Flags Register
	uint32_t cfr_offset;			///< Offset for the Configuration Register
	DIR _direction;					///< Data transfer direction
	uint32_t _channel;				///< DMA channel number
	uint32_t _paddr;				///< Peripheral address
	Per_Type _per_type;				///< Type of peripheral
	SIZE _size;						///< Data transfer size
	IRQn_Type IRQ_vector;			///< Interrupt vector for DMA stream

};

#endif
