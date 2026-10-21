/**
 * @file irq_registry.hpp
 * @brief Fixed-capacity dispatch of shared peripheral IRQs to objects/functions.
 *
 * The generated irq_registry_config.h defines the device-sized table and vector
 * stubs. Each stub calls Dispatch(irqn); application code does not duplicate it.
 * Every registered handler must test its own enabled hardware sources and clear
 * them. Registration neither enables peripheral sources nor changes NVIC.
 */
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
 * static pool; no heap allocation is performed. Change capacity in that header
 * and rebuild every translation unit using the registry; not through CMakeLists.
 *
 * Object registration example (peripheral setup/flag acknowledgement omitted):
 * @code
 *   struct Handler : IIRQHandler {
 *       void HandleIRQ() override {
 *           // Check enabled sources of the owned peripheral and acknowledge them.
 *       }
 *   };
 *   static Handler handler;
 *   bool enabled = NVIC_GetEnableIRQ(USART1_IRQn) != 0;
 *   NVIC_DisableIRQ(USART1_IRQn);
 *   SysInitStatus result = IRQ_Registry::Register(USART1_IRQn, &handler);
 *   if (result == SysInitStatus::InitOK || enabled) NVIC_EnableIRQ(USART1_IRQn);
 * @endcode
 *
 * Removing only that owner, preserving other handlers on the shared vector:
 * @code
 *   // Stop this peripheral's sources first, in main.
 *   bool enabled = NVIC_GetEnableIRQ(USART1_IRQn) != 0;
 *   NVIC_DisableIRQ(USART1_IRQn);
 *   IRQ_Registry::Unregister(USART1_IRQn, &handler);
 *   if (enabled && IRQ_Registry::HasHandlers(USART1_IRQn))
 *       NVIC_EnableIRQ(USART1_IRQn);
 * @endcode
 * Peripheral drivers that register themselves must not be registered again by
 * application code. See IRQ_Registry.md for shared-vector capacity and ownership.
 */
class IRQ_Registry
{
public:
	IRQ_Registry() = delete;

	/// Maximum registrations on one IRQ, default 3 in the settings header.
	static constexpr int MAX_PER_IRQ = IRQ_MAX_SHARED;

	/**
	 * @brief Non-member callback signature; ctx is passed unchanged on each IRQ.
	 * It can point to application state, or be nullptr. The registry does not own
	 * that storage. Function identity is fn alone on a given IRQ, not fn+ctx.
	 */
	using IRQHandlerFn = void (*)(void* ctx);

	/**
	 * @brief Register an object in the first free slot of irqn.
	 * @return InitOK on insertion; InitError for an invalid IRQ, null pointer,
	 *         duplicate object on this IRQ or a full row. No debug trap.
	 * @param irqn External device IRQ; negative core exception numbers are rejected.
	 * @param handler Live object pointer; its lifetime covers the registration.
	 * @note To replace a registration, first Unregister(irqn, handler).
	 *       The same object may be registered once on each of several IRQs.
	 */
	static SysInitStatus Register(IRQn_Type irqn, IIRQHandler* handler);

	/**
	 * @brief Register a plain function using a fixed-pool adapter.
	 * @return InitOK on insertion; InitError for an invalid IRQ, null fn,
	 *         duplicate fn on this IRQ, a full row or an exhausted pool.
	 * @note A different ctx does not permit registering the same fn twice
	 *       on one IRQ. Change fn/ctx through Unregister followed by Register.
	 *       ctx may be nullptr; pointed-to data must remain valid while registered.
	 *
	 * @code
	 *   static uint32_t count = 0;
	 *   auto callback = +[](void* context) {
	 *       // Test and acknowledge the peripheral source before counting it.
	 *       auto* value = static_cast<uint32_t*>(context);
	 *       ++*value;
	 *   };
	 *   // In main, with the affected IRQ masked:
	 *   auto status = IRQ_Registry::Register(USART1_IRQn, callback, &count);
	 *   // A new ctx still requires Unregister(USART1_IRQn, callback) first.
	 * @endcode
	 */
	static SysInitStatus Register(IRQn_Type irqn, IRQHandlerFn fn, void* ctx = nullptr);

	/**
	 * @brief Remove every handler on this IRQ and release its function adapters.
	 * Use only when deliberately owning the entire vector. For a shared line,
	 * use an object/function overload to avoid removing another peripheral.
	 * Invalid IRQ is ignored; hardware/NVIC are not changed.
	 */
	static void Unregister(IRQn_Type irqn);

	/**
	 * @brief Remove only the matching object from this IRQ.
	 * Other handlers remain. Null/missing object or invalid IRQ is ignored.
	 * Removing one registration does not remove this object from its other IRQs.
	 */
	static void Unregister(IRQn_Type irqn, IIRQHandler* handler);

	/**
	 * @brief Remove only this function and release its adapter on this IRQ.
	 * ctx is not an identity and is not required to unregister. Other vectors
	 * using the same fn retain their registrations. Missing fn is ignored.
	 */
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
