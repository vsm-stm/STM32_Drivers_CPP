#ifndef DMA_H_
#define DMA_H_

#include <system_f4.hpp>
#include <rcc.hpp>
#include <vector>


class DMA_Sx
{
private:
	/* data */
public:

	enum class DIR
	{
		Per_To_Mem = 0,
		Mem_To_Per = DMA_SxCR_DIR_0,
		Mem_To_Mem = DMA_SxCR_DIR_1
	};
	enum class PSIZE
	{
		BYTE = 0,
		Half_WORD = DMA_SxCR_PSIZE_0,
		WORD = DMA_SxCR_PSIZE_1,
	};
	enum class MINC
	{
		OFF = 0,
		EN = DMA_SxCR_MINC
	};
	enum class PINC
	{
		OFF = 0,
		EN = DMA_SxCR_PINC
	};
	enum class CIRC
	{
		OFF = 0,
		EN = DMA_SxCR_CIRC
	};

	enum class TYPE
	{
		Per_To_Mem = 0,
		Mem_To_Per = DMA_SxCR_DIR_0,
		Mem_To_Mem = DMA_SxCR_DIR_1,
		Size_Byte = 0,
		Size_HWord = DMA_SxCR_PSIZE_0,
		Soze_Word = DMA_SxCR_PSIZE_1,
		MINC = DMA_SxCR_MINC,
		PINC = DMA_SxCR_PINC,
		CIRC = DMA_SxCR_CIRC,
	};
	
	enum class IRQ
	{
		NO = 0,
		TC = DMA_SxCR_TCIE,
		HTC = DMA_SxCR_HTIE,
		TE = DMA_SxCR_TEIE,
		DME = DMA_SxCR_DMEIE
	};

	struct StreamSettings
	{
		DMA_TypeDef *dma;
		DMA_Stream_TypeDef *dma_sx;
		uint32_t channel;
		TYPE settings;
	};

	DMA_TypeDef *DMA_controller;
	DMA_Stream_TypeDef *DMA_Stream_X;
	
	DMA_Sx(StreamSettings setup) :
			DMA_controller(setup.dma),
			DMA_Stream_X(setup.dma_sx)
	{
		
	}







};




#endif