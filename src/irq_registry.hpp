#ifndef IRQ_REGISTRY_HPP
#define IRQ_REGISTRY_HPP

#include "system.hpp"
#include "irq_registry_settings.hpp"
#include <cstdint>

/**
 * @brief Interface for a peripheral's single IRQ handler object.
 * @note The object must remain valid while registered. HandleIRQ() checks
 *       its peripheral's enabled sources, clears flags and calls callbacks.
 */
class IIRQHandler
{
public:
	virtual void HandleIRQ() = 0;
};

/**
 * @brief Dispatch peripheral IRQs through a fixed table of handler pointers.
 *
 * Generated ISR stubs call Dispatch(irqn). One object can handle multiple
 * sources of its peripheral and can be registered on separate IRQ lines
 * (for example, the peripheral IRQ and its DMA IRQ). Shared lines can hold
 * several distinct objects or functions, up to MAX_PER_IRQ.
 *
 * Register/Unregister must be called from main with the affected IRQ masked.
 * They must not be called from HandleIRQ() or any callback it invokes, even
 * to change a different line. This is an API contract, without runtime guards
 * or locks. Unregister removes pointers; it does not change NVIC or hardware.
 *
 * Settings are in irq_registry_settings.hpp. Function adapters use a separate
 * static pool; no heap allocation is performed. See IRQ_Registry.md.
 */
class IRQ_Registry
{
public:
	IRQ_Registry() = delete;

	/// Maximum registrations on one IRQ, default 3 in the settings header.
	static constexpr int MAX_PER_IRQ = IRQ_MAX_SHARED;

	/// Plain function; ctx is passed unchanged on every invocation.
	using IRQHandlerFn = void (*)(void* ctx);

	/**
	 * @brief Register an object in the first free slot of irqn.
	 * @return InitOK on insertion; InitError for an invalid IRQ, null pointer,
	 *         duplicate object on this IRQ or a full row. No debug trap.
	 * @note To replace a registration, first Unregister(irqn, handler).
	 */
	static SysInitStatus Register(IRQn_Type irqn, IIRQHandler* handler);

	/**
	 * @brief Register a plain function using a fixed-pool adapter.
	 * @return InitOK on insertion; InitError for an invalid IRQ, null fn,
	 *         duplicate fn on this IRQ, a full row or an exhausted pool.
	 * @note A different ctx does not permit registering the same fn twice
	 *       on one IRQ. Change fn/ctx through Unregister followed by Register.
	 *       ctx may be nullptr; pointed-to data must remain valid while registered.
	 */
	static SysInitStatus Register(IRQn_Type irqn, IRQHandlerFn fn, void* ctx = nullptr);

	/// Explicitly clear the whole IRQ row; also release its function adapters.
	static void Unregister(IRQn_Type irqn);

	/// Remove only this object; the other handlers on irqn remain registered.
	static void Unregister(IRQn_Type irqn, IIRQHandler* handler);

	/// Remove only this function and release its adapter; ctx is not an identity.
	static void Unregister(IRQn_Type irqn, IRQHandlerFn fn);

	/// Return whether any handler remains on irqn; invalid IRQs return false.
	static bool HasHandlers(IRQn_Type irqn);

	/**
	 * @brief Call each non-null entry in slot order; an empty row does nothing.
	 * @note Removed slots are reused, so slot order is not registration chronology.
	 *       The table and pool must not change during dispatch or its callbacks.
	 */
	static void Dispatch(IRQn_Type irqn);

private:
	/// Static adapter: Bind(nullptr, nullptr) releases a pool slot.
	class FunctionHandler final : public IIRQHandler
	{
	public:
		void Bind(IRQHandlerFn fn, void* ctx) { _fn = fn; _ctx = ctx; }
		bool IsFree() const { return _fn == nullptr; }
		bool Matches(IRQHandlerFn fn) const { return _fn == fn; }
		void HandleIRQ() override { if (_fn) _fn(_ctx); }

	private:
		IRQHandlerFn _fn = nullptr;
		void* _ctx = nullptr;
	};

	static FunctionHandler _function_pool[IRQ_MAX_FUNCTION_HANDLERS];
	static FunctionHandler* FindFunction(int idx, IRQHandlerFn fn);
	static void ReleaseFunction(IIRQHandler* handler);

	// First dimension is supplied by the generated IRQ table definition.
	static IIRQHandler* _table[][MAX_PER_IRQ];
};

#endif /* IRQ_REGISTRY_HPP */
