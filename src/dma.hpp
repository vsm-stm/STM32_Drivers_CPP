#ifndef DMA_H_
#define DMA_H_

#include <system_f4.hpp>
#include <rcc.hpp>


class DMA_Sx
{
public:

	enum class DIR
	{
		From_Per = 0,
		To_Per,
		Mem_To_Mem
	};
	enum class SIZE
	{
		Byte = 0,
		Half_Word,
		Word
	};

	enum class IRQ
	{
		NO = 0,
		TC = DMA_SxCR_TCIE,
		HTC = DMA_SxCR_HTIE,
		TE = DMA_SxCR_TEIE,
		DME = DMA_SxCR_DMEIE
	};

	enum class Per_Type
	{
		usart = 0,
		spi,
		i2c
	};

	struct StreamSettings
	{
		DMA_Stream_TypeDef *dma_sx;
		uint32_t channel;
		uint32_t per_addr;
		Per_Type type;
		DIR dir;
		SIZE size;
	};

	DMA_Stream_TypeDef *DMA_Stream_X;
	
	explicit DMA_Sx(StreamSettings setup) :
			DMA_Stream_X(setup.dma_sx),
			_direction(setup.dir),
			_channel(setup.channel),
			_paddr(setup.per_addr),
			_per_type(setup.type),
			_size(setup.size)
	{
	};

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
			_per_type(type)
	{
		_size = SIZE::Byte;
	};

	SYS_StatusTypeDef SetUp();
	SYS_StatusTypeDef SetMemAddr(uint32_t addr);
	SYS_StatusTypeDef SetMemAddr(uint32_t addr, uint16_t size);

	void ClearFlags()
	{
		*DMA_CFR = (DMA_LISR_TCIF0 << cfr_offset) |
				   (DMA_LISR_HTIF0 << cfr_offset) |
				   (DMA_LISR_TEIF0 << cfr_offset);
	};

	void Enable_MINC()
	{
		DMA_Stream_X->CR |= DMA_SxCR_MINC;
	};

	void Disable_MINC()
	{
		DMA_Stream_X->CR &= ~DMA_SxCR_MINC;
	};

	void Enable_Stream()
	{
		DMA_Stream_X->CR |= DMA_SxCR_EN;
	};

	void Disable_Stream()
	{
		DMA_Stream_X->CR &= ~DMA_SxCR_EN;
	};

private:
	DMA_TypeDef *DMA_controller;
	uint32_t *DMA_CFR;
	uint32_t cfr_offset;
	DIR _direction;
	uint32_t _channel;
	uint32_t _paddr;
	Per_Type _per_type;
	SIZE _size;

	
	IRQn_Type IRQ_vector;

};

#endif
