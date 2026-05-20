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
 * All methods are static — no instance is created or needed.
 * The internal table is private and sized automatically from the
 * generated irq_registry_config.h (IRQ_TABLE_SIZE).
 */
class IRQ_Registry
{
public:
	IRQ_Registry() = delete;

	/**
	 * @brief Registers a handler for the given IRQ number.
	 *
	 * Traps (BKPT + infinite loop) in debug if the slot is already taken.
	 *
	 * @param irqn    Peripheral IRQ number (non-negative, from IRQn_Type).
	 * @param handler Pointer to the handler object; must remain valid forever.
	 * @return true on success, false if irqn is out of range.
	 */
	static bool Register(IRQn_Type irqn, IIRQHandler* handler);

	/**
	 * @brief Unregisters the handler for the given IRQ number.
	 * @param irqn Peripheral IRQ number to clear.
	 */
	static void Unregister(IRQn_Type irqn);

	/**
	 * @brief Calls the handler registered for irqn, if any.
	 *
	 * Called from the auto-generated extern "C" ISR stubs.
	 * Safe to call with no handler registered — does nothing.
	 *
	 * @param irqn IRQ number supplied by the ISR stub.
	 */
	static void Dispatch(IRQn_Type irqn);

private:
	static IIRQHandler* _table[];
};

#endif /* IRQ_REGISTRY_HPP */
