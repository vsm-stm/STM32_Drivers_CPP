/**
 * @file    i2c.hpp
 * @brief   I2C master driver for STM32 (G0/F7 — I2C v2 IP; F4 — I2C v1 IP).
 *
 * One transfer engine serves every mode: blocking calls run it by polling,
 * _IRQ calls run it from the I2C interrupt, DMA calls let the DMA move the
 * data while the I2C interrupt handles addressing, reloads and the STOP.
 *
 * - 7-bit addresses, passed unshifted (0x50, not 0xA0).
 * - Any length up to 65535 bytes (the 255-byte NBYTES limit is handled
 *   internally with RELOAD).
 * - Register access (WriteReg / ReadReg) with 8- or 16-bit register address;
 *   ReadReg uses a repeated START between the register write and the read.
 * - Interrupts are off until a non-blocking transfer is started; the
 *   driver registers itself in IRQ_Registry (EV and ER lines on F7, one line
 *   on G0 — shared between I2C2 and I2C3 on G0B0/G0B1/G0C1).
 * - G0/F7 DMA transfers do not use the DMA interrupt: completion is the
 *   STOP detected by the I2C interrupt. F4 uses the RX DMA TC interrupt to
 *   generate the STOP after the last byte.
 * - F4: STANDARD and FAST only (FAST_PLUS -> InitError; FMPI2C1 is another IP).
 *
 * ============================================================================
 * Usage
 * ============================================================================
 * @code
 *   I2C i2c(I2C1, I2C::_1::SCL::PB8, I2C::_1::SDA::PB9);
 *   i2c.SetUp(I2C::SPEED::FAST);                          // 400 kHz
 *
 *   uint8_t id;
 *   i2c.ReadReg(0x68, 0x75, &id, 1);                      // blocking
 *   i2c.WriteReg(0x50, 0x0010, page, 16, I2C::REG::_16BIT);
 *
 *   i2c.Receive_IRQ(0x40, buf, 6);                        // interrupt-driven
 *   while (i2c.IsBusy()) {}
 *   if (i2c.GetError() != I2C::ERR::None) { ... }
 *
 *   DMA_Sx dma_tx(DMA1_Channel1, DMA_Sx::Req::I2c1::TX);  // G0
 *   DMA_Sx dma_rx(DMA1_Channel2, DMA_Sx::Req::I2c1::RX);
 *   i2c.AttachDMA(&dma_tx, &dma_rx);
 *   i2c.SetDoneCallback(on_done);
 *   i2c.ReadReg_DMA(0x68, 0x3B, buf, 14);
 * @endcode
 */

#ifndef I2C_HPP_
#define I2C_HPP_

#include "system.hpp"
#include "rcc.hpp"
#include "gpio.hpp"
#include "dma.hpp"
#include "irq_registry.hpp"

class I2C : public IIRQHandler
{
public:
	/** @brief SCL frequency. FAST_PLUS also enables the Fm+ drive (G0/F7). */
	enum class SPEED : uint32_t {
		STANDARD  = 100000,
		FAST      = 400000,
		FAST_PLUS = 1000000,
	};

	/** @brief Size of the register address sent by WriteReg / ReadReg. */
	enum class REG : uint8_t {
		_8BIT  = 1,
		_16BIT = 2,
	};

	/** @brief Result of the last transfer. */
	enum class ERR : uint8_t {
		None,
		Nack,       ///< Address or data byte not acknowledged
		BusError,   ///< Misplaced START/STOP on the bus
		ArbLost,    ///< Arbitration lost to another master
		Timeout,    ///< Blocking call did not finish in time
		Aborted,    ///< Stopped by Abort()
		DmaError,   ///< DMA transfer error (stream stopped)
	};

	// -----------------------------------------------------------------------
	// Pin tables: I2C::_N::SCL::<PXn>, I2C::_N::SDA::<PXn>
	// -----------------------------------------------------------------------
#include "i2c_defs.hpp"

	/**
	 * @brief Binds the driver to a peripheral and its pins.
	 * A pin from another I2C's table is a programming error (DebugTrap).
	 */
	I2C(I2C_TypeDef* i2cx, PIN scl, PIN sda)
		: I2Cx(i2cx), _scl(scl), _sda(sda)
	{
		if (scl.periph_base != (uint32_t)i2cx)
			System::DebugTrap("I2C: SCL pin belongs to wrong peripheral");
		if (sda.periph_base != (uint32_t)i2cx)
			System::DebugTrap("I2C: SDA pin belongs to wrong peripheral");
	}

	I2C() = delete;
	I2C(I2C const&)            = delete;
	I2C& operator=(I2C const&) = delete;

	/**
	 * @brief Clock, reset, pins, timing. The kernel clock is PCLK (APB1).
	 * @return InitError if the timing cannot be met from the current PCLK
	 *         (Fm+ needs PCLK >= ~16 MHz) or the peripheral is unknown.
	 */
	SysInitStatus SetUp(SPEED speed = SPEED::STANDARD);

	// -----------------------------------------------------------------------
	// Blocking transfers. Return OK, Busy, Error (see GetError()) or Timeout.
	// -----------------------------------------------------------------------

	SysStatus Send(uint8_t addr, const uint8_t* data, uint16_t len, uint32_t timeout = 100);
	SysStatus Receive(uint8_t addr, uint8_t* data, uint16_t len, uint32_t timeout = 100);
	SysStatus WriteReg(uint8_t addr, uint16_t reg, const uint8_t* data, uint16_t len,
					   REG reg_size = REG::_8BIT, uint32_t timeout = 100);
	SysStatus ReadReg(uint8_t addr, uint16_t reg, uint8_t* data, uint16_t len,
					  REG reg_size = REG::_8BIT, uint32_t timeout = 100);

	/** @brief Address-only probe: OK if a device acknowledges @p addr. */
	SysStatus IsReady(uint8_t addr, uint32_t timeout = 10);

	// -----------------------------------------------------------------------
	// Interrupt-driven transfers. Return at once; poll IsBusy() or use the
	// callbacks. The buffer must stay valid until the transfer ends.
	// -----------------------------------------------------------------------

	SysStatus Send_IRQ(uint8_t addr, const uint8_t* data, uint16_t len);
	SysStatus Receive_IRQ(uint8_t addr, uint8_t* data, uint16_t len);
	SysStatus WriteReg_IRQ(uint8_t addr, uint16_t reg, const uint8_t* data, uint16_t len,
						   REG reg_size = REG::_8BIT);
	SysStatus ReadReg_IRQ(uint8_t addr, uint16_t reg, uint8_t* data, uint16_t len,
						  REG reg_size = REG::_8BIT);

	// -----------------------------------------------------------------------
	// DMA transfers
	// -----------------------------------------------------------------------

	/**
	 * @brief Sets the DMA channels of this I2C (call after SetUp()).
	 * nullptr = no DMA in that direction (a channel attached before is
	 * released). Replacing a channel moves the interrupt registration.
	 * @return InitError while a transfer runs or if a channel cannot be set up.
	 */
	SysInitStatus AttachDMA(DMA_Sx* tx, DMA_Sx* rx);

	/**
	 * @return Error if the needed DMA channel is not attached, or (F7 with
	 *         D-cache on, receive) the buffer is not 32-byte aligned with a
	 *         length multiple of 32 — see DMA_Sx::CacheRxOk().
	 */
	SysStatus Send_DMA(uint8_t addr, const uint8_t* data, uint16_t len);
	SysStatus Receive_DMA(uint8_t addr, uint8_t* data, uint16_t len);
	SysStatus WriteReg_DMA(uint8_t addr, uint16_t reg, const uint8_t* data, uint16_t len,
						  REG reg_size = REG::_8BIT);
	SysStatus ReadReg_DMA(uint8_t addr, uint16_t reg, uint8_t* data, uint16_t len,
						 REG reg_size = REG::_8BIT);

	// -----------------------------------------------------------------------
	// Status and control
	// -----------------------------------------------------------------------

	inline bool IsBusy()   const { return _state != State::Idle; }
	inline ERR  GetError() const { return _err; }

	/** @brief Called from the interrupt when a non-blocking transfer ends without error. */
	inline void SetDoneCallback(void (*cb)(void))  { _done_cb = cb; }
	/** @brief Called from the interrupt when a non-blocking transfer ends with an error. */
	inline void SetErrorCallback(void (*cb)(void)) { _err_cb = cb; }

	/** @brief Stops a running non-blocking transfer (peripheral reset, no callback). */
	void Abort();

	/**
	 * @brief Frees a bus held by a slave stuck in the middle of a byte:
	 *        up to 9 SCL pulses until SDA is released, then a STOP.
	 * @return OK if SDA is high afterwards.
	 */
	SysStatus Recover();

private:
	struct PeriphInfo {
		I2C_TypeDef*        periph;
		volatile uint32_t*  clk_reg;
		uint32_t            clk_bit;
		volatile uint32_t*  rst_reg;
		uint32_t            rst_bit;
		volatile uint32_t*  ksel_reg;   ///< Kernel clock selector (nullptr: none)
		uint32_t            ksel_mask;  ///< Cleared -> PCLK
		IRQn_Type           irq_ev;
		IRQn_Type           irq_er;     ///< Same as irq_ev on G0
		DMA_Sx::RequestSet  dma_tx;
		DMA_Sx::RequestSet  dma_rx;
		uint32_t            fmp_bit;    ///< SYSCFG Fm+ enable (0: none)
	};
	static const PeriphInfo i2c_table[];

	enum class Mode : uint8_t { Poll, Irq, Dma };

	/**
	 * @brief Transfer state machine. Each state waits for one event; G0/F7
	 *        skip Start/Addr/RxTail (START, address and the byte count are
	 *        one CR2 write there), F4 skips Stop (no STOPF in master mode).
	 */
	enum class State : uint8_t {
		Idle,
		Config,     ///< Reserved: a transfer is being set up, or SetUp/AttachDMA/Recover runs
		Start,      ///< F4: SB -> address
		Addr,       ///< F4: ADDR -> ACK/POS for the length, ADDR cleared
		TxReg,      ///< TXE/TXIS -> register byte
		Restart,    ///< Register sent (F4: BTF, G0/F7: TC) -> repeated START, read
		TxData,     ///< TXE/TXIS -> data byte (or DMA); end: F4 BTF, G0/F7 STOPF
		RxData,     ///< RXNE -> data byte (or DMA)
		RxTail,     ///< F4: last 1-3 bytes, ACK/STOP timing on BTF/RXNE
		Stop,       ///< G0/F7: waiting for STOPF only (IsReady, after NACK)
	};

	/** @brief Checks, fills the transfer state and generates the START. */
	SysStatus Begin(Mode mode, uint8_t addr, uint16_t reg, uint8_t reg_len,
					uint8_t* buf, uint16_t len, bool read);
	/** @brief Begin() in Poll mode, then runs Process() until done or @p timeout. */
	SysStatus Run(uint8_t addr, uint16_t reg, uint8_t reg_len,
				  uint8_t* buf, uint16_t len, bool read, uint32_t timeout);
	/** @brief G0/F7: CR2 = @p cr2 + next chunk of at most 255 bytes, RELOAD or AUTOEND. */
	void LoadNBytes(uint32_t cr2);
	/** @brief One step of the state machine; shared by polling and the interrupt. */
	void Process();
	/**
	 * @brief Ends the transfer: interrupts and DMA off, Idle. With @p notify
	 *        (non-blocking modes) the callback is queued in _pending_cb; the
	 *        caller runs it after leaving its IrqLock.
	 */
	void Finish(bool notify);
	/** @brief Atomically Idle -> Config; false if busy. */
	bool Reserve();
	/** @brief Config -> Idle. */
	void Release();
	/** @brief Moves this object's registration from the old DMA lines to those of @p tx / @p rx. */
	void BindDmaIrqs(DMA_Sx* tx, DMA_Sx* rx);
	/** @brief Aborts whatever the peripheral was doing (G0/F7: PE off/on, F4: SWRST + timing). */
	void ResetPeriph();
	/** @brief Registers this object on the I2C interrupt line(s) once. */
	void EnableIRQ();

	void HandleIRQ() override final;

	I2C_TypeDef*      I2Cx;
	PIN               _scl;
	PIN               _sda;
	const PeriphInfo* _info = nullptr;
	uint32_t _timing = 0;   ///< G0/F7: TIMINGR; F4: CCR | TRISE << 16 | FREQ << 24

	DMA_Sx*  _dma_tx = nullptr;
	DMA_Sx*  _dma_rx = nullptr;
	bool     _irq_registered = false;

	// Transfer state
	uint8_t  _addr    = 0;
	uint8_t  _reg[2]  = {};
	uint8_t  _reg_len = 0;
	uint8_t  _reg_idx = 0;
	uint8_t* _buf     = nullptr;
	uint16_t _len     = 0;
	uint16_t _idx     = 0;
	uint32_t _left    = 0;      ///< G0/F7: bytes not yet given to NBYTES
	bool     _read    = false;
	Mode     _mode    = Mode::Poll;

	volatile State _state = State::Idle;
	volatile ERR   _err   = ERR::None;

	void (*_done_cb)(void) = nullptr;
	void (*_err_cb)(void)  = nullptr;
	void (*_pending_cb)(void) = nullptr;   ///< Set by Finish(), run by HandleIRQ after its lock
};

#endif /* I2C_HPP_ */
