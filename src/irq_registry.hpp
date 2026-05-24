#ifndef IRQ_REGISTRY_HPP
#define IRQ_REGISTRY_HPP

#include "system.hpp"
#include <cstdint>

/**
 * @brief Base interface for any object that handles a peripheral interrupt.
 *
 * Derive from this and implement HandleIRQ(), then register the object
 * with IRQ_Registry::Register().  HandleIRQ() may be private in the
 * derived class — virtual dispatch bypasses access checks.
 */
class IIRQHandler
{
public:
	virtual void HandleIRQ() = 0;
};

/**
 * @brief Static dispatch table mapping IRQn numbers to IIRQHandler objects.
 *
 * Each IRQ slot holds up to MAX_PER_IRQ handlers.
 * MAX_PER_IRQ is taken from IRQ_MAX_SHARED defined in irq_registry_config.h
 * (CMake-generated, family-specific).  When this header is included without
 * the config (e.g. from peripheral drivers), the fallback of 3 applies:
 *
 *   G0  — 3: DMA1_Ch4_5_DMAMUX1_OVR = ch4 + ch5 + DMAMUX overrun
 *   F4/F7 — 2: TIM paired vectors; DMA streams have dedicated vectors
 *
 * The first dimension of _table is left incomplete here and completed in
 * irq_registry_config.h as _table[IRQ_TABLE_SIZE][IRQ_MAX_SHARED].
 *
 * All methods are static — no instance is created or needed.
 */

// IRQ_MAX_SHARED is defined before this header in irq_registry_config.h.
// Provide a fallback when included directly by peripheral drivers.
#ifndef IRQ_MAX_SHARED
#  define IRQ_MAX_SHARED 3
#endif

class IRQ_Registry
{
public:
	IRQ_Registry() = delete;

	/**
	 * Max handlers sharing one IRQ line.
	 * Value comes from IRQ_MAX_SHARED (CMake-generated per MCU family).
	 */
	static constexpr int MAX_PER_IRQ = IRQ_MAX_SHARED;

	/**
	 * @brief Registers a handler for the given IRQ number.
	 *
	 * Fills the first free slot for this IRQn.
	 * If all MAX_PER_IRQ slots are taken, traps in debug (BKPT + loop).
	 *
	 * @param irqn    Peripheral IRQ number (non-negative, from IRQn_Type).
	 * @param handler Pointer to the handler object; must remain valid forever.
	 * @return true on success, false if irqn is out of range.
	 */
	static bool Register(IRQn_Type irqn, IIRQHandler* handler);

	/**
	 * @brief Unregisters ALL handlers for the given IRQ number.
	 * @param irqn Peripheral IRQ number to clear.
	 */
	static void Unregister(IRQn_Type irqn);

	/**
	 * @brief Calls all handlers registered for irqn, in registration order.
	 *
	 * Called from the auto-generated extern "C" ISR stubs.
	 * Safe to call with no handler registered — does nothing.
	 *
	 * @param irqn IRQ number supplied by the ISR stub.
	 */
	static void Dispatch(IRQn_Type irqn);

private:
	// First dimension intentionally incomplete — completed in irq_registry_config.h:
	//   IIRQHandler* _table[IRQ_TABLE_SIZE][IRQ_MAX_SHARED] = {}
	static IIRQHandler* _table[][MAX_PER_IRQ];
};

#endif /* IRQ_REGISTRY_HPP */
