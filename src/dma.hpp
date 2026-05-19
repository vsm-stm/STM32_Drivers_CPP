#ifndef DMA_H_
#define DMA_H_

#include "system.hpp"
#include "rcc.hpp"


namespace DMA_Sx_ns {
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

	struct dma_sets_typedef {
		DMA_TypeDef* ctrl;			// контроллер DMA1 или DMA2
		volatile uint32_t* isr;		// ISR (статус)
		volatile uint32_t* ifcr;	// IFCR (очистка)
		uint8_t if_offset;			// смещение флагов
		IRQn_Type irqn;				// IRQ
		bool used;					// занят ли поток
	};

	struct StreamSettings {
		uint32_t channel{0};
		DIR direction{DIR::From_Per};
		SIZE data_size{SIZE::Byte};
		bool minc{0};
		bool pinc{0};
		bool circ{0};
		uint32_t per_address{0};
		uint32_t mem_address{0};
		uint32_t count{0};
	};

	struct DMA_Desc {
		uintptr_t stream_base{};
		uint32_t  channel{};
		uintptr_t periph_base{};

		DMA_Stream_TypeDef* Stream() const noexcept {
			return reinterpret_cast<DMA_Stream_TypeDef*>(stream_base);
		}
		constexpr bool IsValid() const noexcept { return stream_base != 0; }
	};

// ------------------- КЛАСС DMA STREAM -----------------------

class DMA_Sx
{
private:
	DMA_Stream_TypeDef *DMA_Stream_X;

	// Данные о стриме
	dma_sets_typedef* DMA = nullptr;

	static int32_t GetStreamIndex(DMA_Stream_TypeDef* s);

public:
	/**
	 * @brief Constructor with individual parameters.
	 */
	DMA_Sx(DMA_Stream_TypeDef *dma_sx) : DMA_Stream_X(dma_sx){};


	/**
	 * @brief Function to set up DMA stream.
	 * @return Status of the setup operation.
	 */
	SysInitStatus SetUp(const StreamSettings &settings);

	/**
	 * @brief Overloaded function to set memory address and size for the DMA stream.
	 * @param addr Memory address.
	 * @param size Size of the memory transfer.
	 * @return Status of the memory address and size setting operation.
	 */
	inline SysStatus SetMemAddr(uint32_t addr) { 
		if(addr == 0)
			return SysStatus::Error;
		DMA_Stream_X->M0AR = addr;
		return SysStatus::OK;
	};

	inline SysStatus SetPerAddr(uint32_t addr) { 
		if(addr == 0)
			return SysStatus::Error;
		DMA_Stream_X->PAR = addr;
		return SysStatus::OK;
	};

	inline SysStatus SetCount(uint32_t n) { 
		if(n == 0)
			return SysStatus::Error;
		DMA_Stream_X->NDTR = n;
		return SysStatus::OK;
	};

	inline uint32_t GetCount() {
		return DMA_Stream_X->NDTR;
	};


	/**
	 * @brief Function to clear DMA flags.
	 */
	inline void ClearFlags()
	{
		*DMA->ifcr =	(DMA_LIFCR_CTCIF0 | 
						 DMA_LIFCR_CHTIF0 |
						 DMA_LIFCR_CFEIF0 |
						 DMA_LIFCR_CTEIF0) << DMA->if_offset;
	};

	inline bool GetTC_Flag()
	{ return ((*DMA->isr & (DMA_LISR_TCIF0_Msk << DMA->if_offset)) >> DMA->if_offset);};

	/**
	 * @brief Enable the specified USART IRQ.
	 * @param irq The IRQ to enable.
	 */
	inline void Enable_IRQ(IRQ irq)
	{
		DMA_Stream_X->CR |= static_cast<uint32_t>(irq);

		if (!(NVIC_GetEnableIRQ(DMA->irqn)))
		{
			NVIC_EnableIRQ(DMA->irqn);
		}
	}

	/**
	 * @brief Disable the specified USART IRQ.
	 * @param irq The IRQ to disable.
	 */
	inline void Disable_IRQ(IRQ irq)
	{
		DMA_Stream_X->CR &= ~(static_cast<uint32_t>(irq));
	}

	inline void MINC(FunctionalState en) {
		if(en)
			DMA_Stream_X->CR |=  DMA_SxCR_MINC;
		else
			DMA_Stream_X->CR &= ~DMA_SxCR_MINC;
	};

	inline void PINC(FunctionalState en) {
		if(en)
			DMA_Stream_X->CR |=  DMA_SxCR_PINC;
		else
			DMA_Stream_X->CR &= ~DMA_SxCR_PINC;
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
	inline void Stream_EN(FunctionalState en)
	{
		if(en)
			DMA_Stream_X->CR |= DMA_SxCR_EN;
		else
			DMA_Stream_X->CR &= ~DMA_SxCR_EN;
	};
};

} // namespace DMA_Sx

#endif
