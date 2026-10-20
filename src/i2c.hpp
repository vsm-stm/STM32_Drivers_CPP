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
 * - Interrupt sources are off until a non-blocking transfer is started;
 *   SetUp registers the driver in IRQ_Registry (EV and ER on F4/F7, one line
 *   on G0 — shared between I2C2 and I2C3 on G0B0/G0B1/G0C1).
 * - I2C and each DMA direction have separate registered handlers. DMA TC
 *   confirms the buffer transfer; I2C STOP (F4 TX: BTF then STOP command)
 *   confirms the bus phase. No handler waits for hardware.
 * - Buffers stay owned until IsBusy() becomes false / OnComplete or OnError.
 *   All IRQs of one object must not preempt each other. The caller serializes
 *   starts, Abort and DeInit against those IRQs; there are no internal locks.
 * - SetUp is allowed once until DeInit. Pins and DMA selection are fixed by
 *   construction; there is no AttachDMA or run-time replacement.
 * - Recovery of a slave-held bus belongs to the application, not this driver.
 * - F4: STANDARD and FAST only (FAST_PLUS -> InitError; FMPI2C1 is another IP).
 *
 * ============================================================================
 * Usage
 * ============================================================================
 * @code
 *   I2C i2c(I2C1, I2C::_1::SCL::PB8, I2C::_1::SDA::PB9);
 *   i2c.SetUp<16000000, I2C::SPEED::FAST>();              // PCLK=16 MHz, bus=400 kHz
 *
 *   uint8_t id;
 *   i2c.ReadReg(0x68, 0x75, &id, 1);                      // blocking
 *   i2c.WriteReg(0x50, 0x0010, page, 16, I2C::REG::_16BIT);
 *
 *   i2c.Receive_IRQ(0x40, buf, 6);                        // interrupt-driven
 *   // Bound the wait in main. Abort() generates no callback.
 *   const uint32_t started = System::GetTick();
 *   while (i2c.IsBusy() && System::GetTick() - started < 100u) {}
 *   if (i2c.IsBusy()) i2c.Abort();
 *   // Check i2c.GetError() and handle failure in the application.
 *
 *   class SensorBus : public I2C {
 *   public:
 *       using I2C::I2C;
 *   protected:
 *       void OnComplete() override { done = true; }
 *       void OnError(ERR error) override { last_error = error; }
 *   private:
 *       volatile bool done = false;
 *       volatile ERR last_error = ERR::None;
 *   };
 *   // G0: the driver chooses the I2C1 TX/RX request sources itself.
 *   SensorBus bus(I2C1, I2C::_1::SCL::PB8, I2C::_1::SDA::PB9,
 *                 DMA1_Channel1, DMA1_Channel2);
 *   // F4/F7: pass named routes instead of the physical G0 channels:
 *   // DMA_Sx::Req::I2c1::TX_DMA1_Stream6,
 *   // DMA_Sx::Req::I2c1::RX_DMA1_Stream0.
 *   if (bus.SetUp<16000000, I2C::SPEED::FAST>() == SysInitStatus::InitOK)
 *       bus.ReadReg_DMA(0x68, 0x3B, buf, 14);
 *   // Check the start status; keep buf alive through completion.
 *   // On F7 the application owns all cache lines touched by this buffer.
 * @endcode
 */

#ifndef I2C_HPP_
#define I2C_HPP_

#include "system.hpp"
#include "rcc.hpp"
#include "gpio.hpp"
#include "dma.hpp"
#include "irq_registry.hpp"
#if defined(STM32G0) || defined(STM32F7)
#include "tools/i2c_timing_utility.hpp"
#endif

// Change here and rebuild I2C and its users. Ordinary control RMW by default.
#ifndef I2C_USE_ATOMIC
#define I2C_USE_ATOMIC 0
#endif
#if I2C_USE_ATOMIC
#define I2C_SET_BIT ATOMIC_SET_BIT
#define I2C_CLEAR_BIT ATOMIC_CLEAR_BIT
#define I2C_MODIFY_REG ATOMIC_MODIFY_REG
#else
#define I2C_SET_BIT SET_BIT
#define I2C_CLEAR_BIT CLEAR_BIT
#define I2C_MODIFY_REG MODIFY_REG
#endif

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


#if defined(STM32G0)
	using DMAReq = DMA_Channel_TypeDef*;
#else
	using DMAReq = DMA_Sx::DMAReq;
#endif

	/**
	 * @brief Fix peripheral, pins and optional TX/RX DMA at construction.
	 * F4/F7: named DMAReq includes stream, channel selection, owner and direction.
	 * G0: pass physical DMA channels; request sources follow I2Cx and direction.
	 * Wrong pin/route ownership or a shared TX/RX resource causes DebugTrap.
	 * Empty DMA selections keep blocking and IRQ methods available.
	 */
	I2C(I2C_TypeDef* i2cx, PIN scl, PIN sda, DMAReq dma_tx = {}, DMAReq dma_rx = {})
		: I2Cx(i2cx), _scl(scl), _sda(sda),
#if defined(STM32G0)
		  _dma_tx(dma_tx, DmaRequest(i2cx, true)), _dma_rx(dma_rx, DmaRequest(i2cx, false)),
#else
		  _dma_tx(dma_tx), _dma_rx(dma_rx),
#endif
		  _dma_tx_irq(*this), _dma_rx_irq(*this)
	{
		if (!scl.IsValid() || scl.periph_base != reinterpret_cast<uintptr_t>(i2cx))
			System::DebugTrap("I2C: SCL pin belongs to wrong peripheral");
		if (!sda.IsValid() || sda.periph_base != reinterpret_cast<uintptr_t>(i2cx))
			System::DebugTrap("I2C: SDA pin belongs to wrong peripheral");
#if defined(STM32G0)
		if (dma_tx && dma_tx == dma_rx)
			System::DebugTrap("I2C: TX and RX require different DMA channels");
#else
		if ((dma_tx.stream || dma_tx.ch || dma_tx.periph_base)
			&& (!dma_tx.IsValid() || dma_tx.periph_base != reinterpret_cast<uintptr_t>(i2cx)
				|| dma_tx.direction != DMA_Sx::DIR::To_Per))
			System::DebugTrap("I2C: invalid TX DMA request");
		if ((dma_rx.stream || dma_rx.ch || dma_rx.periph_base)
			&& (!dma_rx.IsValid() || dma_rx.periph_base != reinterpret_cast<uintptr_t>(i2cx)
				|| dma_rx.direction != DMA_Sx::DIR::From_Per))
			System::DebugTrap("I2C: invalid RX DMA request");
		if (dma_tx.stream && dma_tx.stream == dma_rx.stream)
			System::DebugTrap("I2C: TX and RX require different DMA streams");
#endif
	}

	I2C() = delete;
	I2C(I2C const&) = delete;
	I2C& operator=(I2C const&) = delete;
	I2C(I2C&&) = delete;
	I2C& operator=(I2C&&) = delete;
	/// Call DeInit from main before destroying an initialized object.
	~I2C() = default;

	/**
	 * @brief Configure clock, pins, timing, owned DMA and IRQ registrations once.
	 * Kernel clock is PCLK (APB1). No source generator is started by this driver.
	 * @tparam pclk_hz Actual APB1/kernel clock configured by the application RCC.
	 * @tparam speed Nominal bus speed. Timing calculation and validation are
	 *         compile-time, including F4 CCR/TRISE. Invalid timing is a compile error.
	 *         F7/G0 retain the ST calculator's rise/fall/filter assumptions.
	 * @return InitError on repeated setup, unsupported peripheral, a busy DMA
	 *         resource or failed IRQ registration; partial acquisition is rolled back.
	 * @pre System/RCC initialized, actual PCLK equals pclk_hz; main only.
	 *         No runtime clock check or timing search. DeInit before another SetUp.
	 *         Example: bus.SetUp<16000000, SPEED::FAST>() with PCLK=16 MHz.
	 */
	template<uint32_t pclk_hz, SPEED speed = SPEED::STANDARD>
	SysInitStatus SetUp()
	{
		static_assert(pclk_hz > 0, "I2C: PCLK must be nonzero");
		static_assert(speed == SPEED::STANDARD || speed == SPEED::FAST || speed == SPEED::FAST_PLUS,
			"I2C: unsupported bus speed");
#if defined(STM32F4)
		static_assert(speed != SPEED::FAST_PLUS, "I2C F4: Fast-mode Plus requires a different IP");
		static_assert(pclk_hz >= 2000000u && pclk_hz <= 50000000u,
			"I2C F4: PCLK outside supported range");
		static_assert(speed != SPEED::FAST || pclk_hz >= 4000000u,
			"I2C F4: Fast mode requires PCLK >= 4 MHz");
#endif
		static constexpr uint32_t timing = CalculateTiming(pclk_hz, speed);
		static_assert(timing != 0, "I2C: no valid timing for this PCLK and speed");
		return Apply(timing, speed);
	}

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
	 * @brief Transfer through constructor-bound DMA; no run-time attachment.
	 * @return Error if the direction has no DMA, data/length/register size is
	 *         invalid, or DMA preparation fails; NotInit before SetUp; Busy while
	 *         the driver or bus is occupied; OK once started.
	 * TX is cleaned before DMA. RX is cleaned/invalidated before DMA and
	 * invalidated after successful completion. On F7 the caller owns every
	 * touched 32-byte cache line, including padding; len need not be a multiple
	 * of 32. System skips maintenance for configured non-cacheable DMA regions.
	 * Error/aborted buffers are not valid data and must not be inspected.
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

	/**
	 * @brief Stop a pending asynchronous transfer from main, without callbacks.
	 * Disables requests, confirms DMA stop and resets this peripheral.
	 * Retains DMA ownership and IRQ registrations; GetError returns Aborted.
	 * @pre Serialize with this object's I2C/DMA IRQs. Never call from a callback.
	 */
	void Abort();

	/**
	 * @brief Main-only release of this object's IRQ entries, DMA and pins.
	 * Other handlers sharing a vector and other peripherals remain registered.
	 * Stops an active transfer without callbacks; repeated DeInit is harmless.
	 */
	void DeInit();

protected:
	/// Non-blocking success, after DMA/cache handover. Executes in I2C or DMA IRQ.
	virtual void OnComplete() {}
	/// Non-blocking failure; the buffer is unusable. Executes in I2C or DMA IRQ.
	virtual void OnError(ERR error) { (void)error; }
	// Callbacks may publish flags; no blocking calls, Abort/DeInit or registry changes.

private:
	/// Compiler-only register calculation; no access to mutable System clocks.
	static consteval uint32_t CalculateTiming(uint32_t pclk, SPEED speed)
	{
#if defined(STM32F4)
		if (pclk < 2000000u || pclk > 50000000u
			|| (speed != SPEED::STANDARD && speed != SPEED::FAST)
			|| (speed == SPEED::FAST && pclk < 4000000u)) return 0;
		const uint32_t freq = pclk / 1000000u;
		const uint32_t hz = static_cast<uint32_t>(speed);
		uint32_t ccr, trise;
		if (speed == SPEED::STANDARD) {
			ccr = (pclk + 2u * hz - 1u) / (2u * hz);
			if (ccr < 4u) ccr = 4u;
			trise = freq + 1u;
		} else {
			ccr = (pclk + 3u * hz - 1u) / (3u * hz);
			trise = freq * 300u / 1000u + 1u;
		}
		if (ccr > (I2C_CCR_CCR >> I2C_CCR_CCR_Pos)) return 0;
		if (speed == SPEED::FAST) ccr |= I2C_CCR_FS;
		return ccr | (trise << 16) | (freq << 24);
#else
		return I2C_Timing::Calculate(pclk, static_cast<uint32_t>(speed));
#endif
	}
	/// Main-only application of compiler-produced timing, ownership and IRQ setup.
	SysInitStatus Apply(uint32_t timing, SPEED speed);
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
		DmaStop,    ///< Bus phase ended; DMA completion/stop IRQ still owns the buffer
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
	void Process(uint32_t status);
	inline uint32_t StatusReg() const {
#if defined(STM32F4)
		return I2Cx->SR1;
#else
		return I2Cx->ISR;
#endif
	}
	/// Bus phase ended: disable sources, then complete now or on DMA TC/TE.
	void Finish();
	/// Final handover after confirmed DMA completion/stop. No waits or ISR reads.
	void Complete(bool notify);
	/// Reset this I2C; F4 restores its timing after SWRST.
	void ResetPeriph();
	SysInitStatus SetUpDMA();

	void HandleIRQ() override final;
	void HandleDmaTxIRQ();
	void HandleDmaRxIRQ();
#if defined(STM32G0)
	static uint32_t DmaRequest(I2C_TypeDef* i2c, bool tx);
#endif

	class DmaTxIRQ final : public IIRQHandler {
	public:
		explicit DmaTxIRQ(I2C& i2c) : _i2c(i2c) {}
		void HandleIRQ() override { _i2c.HandleDmaTxIRQ(); }
	private:
		I2C& _i2c;
	};
	class DmaRxIRQ final : public IIRQHandler {
	public:
		explicit DmaRxIRQ(I2C& i2c) : _i2c(i2c) {}
		void HandleIRQ() override { _i2c.HandleDmaRxIRQ(); }
	private:
		I2C& _i2c;
	};

	I2C_TypeDef* const I2Cx;
	PIN               _scl;
	PIN               _sda;
	const PeriphInfo* _info = nullptr;
	uint32_t _timing = 0;   ///< G0/F7: TIMINGR; F4: CCR | TRISE << 16 | FREQ << 24

	DMA_Sx _dma_tx;
	DMA_Sx _dma_rx;
	DmaTxIRQ _dma_tx_irq;
	DmaRxIRQ _dma_rx_irq;

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

};

#endif /* I2C_HPP_ */
