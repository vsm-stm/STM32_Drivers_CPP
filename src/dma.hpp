#ifndef DMA_H_
#define DMA_H_

#include "system.hpp"
#include "rcc.hpp"

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
		uint32_t channel;			///< DMA channel number
		uint32_t peripheral_address;			///< Peripheral address
		Per_Type peripheral_type;				///< Type of peripheral
		DIR direction;					///< Data transfer direction
		SIZE data_size;					///< Data transfer size
	};

	DMA_Stream_TypeDef *DMA_Stream_X;

	/**
	 * @brief Constructor with individual parameters.
	 */
	explicit DMA_Sx(DMA_Stream_TypeDef *dma_sx) : DMA_Stream_X(dma_sx){};

	/**
	 * @brief Function to set up DMA stream.
	 * @return Status of the setup operation.
	 */
	SysInitStatus SetUp(StreamSettings settings);
	SysInitStatus SetUp(
		uint32_t channel,
		uint32_t peripheral_address,
		Per_Type peripheral_type,
		DIR direction,
		SIZE data_size);

	/**
	 * @brief Overloaded function to set memory address and size for the DMA stream.
	 * @param addr Memory address.
	 * @param size Size of the memory transfer.
	 * @return Status of the memory address and size setting operation.
	 */
	SysStatus SetMemAddr(uint32_t addr, uint16_t size);

	/**
	 * @brief Function to clear DMA flags.
	 */
	inline void ClearFlags()
	{
		*DMA.cfr =	(DMA_LISR_TCIF0 << DMA.cfr_offset) |
					(DMA_LISR_HTIF0 << DMA.cfr_offset) |
					(DMA_LISR_FEIF0 << DMA.cfr_offset) |
					(DMA_LISR_TEIF0 << DMA.cfr_offset);
	};

	inline bool GetTC_Flag()
	{ return ((*DMA.sr & (DMA_LISR_TCIF0_Msk << DMA.cfr_offset)) >> DMA.cfr_offset);};

	/**
	 * @brief Enable the specified USART IRQ.
	 * @param irq The IRQ to enable.
	 */
	inline void Enable_IRQ(IRQ irq)
	{
		DMA_Stream_X->CR |= static_cast<uint32_t>(irq);

		if (!(NVIC_GetEnableIRQ(DMA.irqn)))
		{
			NVIC_EnableIRQ(DMA.irqn);
		}
	}

	/**
	 * @brief Disable the specified USART IRQ.
	 * @param irq The IRQ to disable.
	 */
	inline void Disable_IRQ(IRQ irq)
	{
		DMA_Stream_X->CR &= ~(static_cast<uint32_t>(irq));
		if (!(DMA_Stream_X->CR & (DMA_SxCR_TCIE | DMA_SxCR_HTIE)))
		{
			NVIC_DisableIRQ(DMA.irqn);
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
	inline void Stream(FunctionalState en)
	{
		if(en)
			DMA_Stream_X->CR |= DMA_SxCR_EN;
		else
			DMA_Stream_X->CR &= ~DMA_SxCR_EN;
	};

	typedef struct _dma_sets
	{
		bool used;
		DMA_TypeDef *ctrl;
		uint32_t *cfr;
		uint32_t *sr;
		uint32_t cfr_offset;
		IRQn_Type irqn;
	}dma_sets_typedef;
private:


	dma_sets_typedef DMA;
};

#endif
