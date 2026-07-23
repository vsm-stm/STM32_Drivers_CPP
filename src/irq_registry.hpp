#ifndef IRQ_REGISTRY_HPP
#define IRQ_REGISTRY_HPP

#include "system.hpp"
#include <cstdint>

/**
 * @brief IRQ_Registry — a single runtime dispatcher for every peripheral interrupt.
 *
 * === Why ===
 *
 * The plain CMSIS way is one extern "C" ISR per vector, hard-wired to one object:
 *
 *   extern "C" void USART1_IRQHandler(void) { my_usart.HandleIRQ(); }
 *
 * That has three problems: (1) the handler can't be swapped at runtime without
 * editing code; (2) two peripherals sharing one vector (e.g. a DMA stream pair,
 * or G0's DMAMUX overrun line) have no built-in way to both run — you end up
 * hand-rolling if/else inside one ISR; (3) ISR bodies end up scattered across
 * whichever driver file happens to own that vector.
 *
 * IRQ_Registry replaces every such ISR with one identical stub per vector:
 *
 *   extern "C" void USART1_IRQHandler(void) { IRQ_Registry::Dispatch(USART1_IRQn); }
 *
 * These stubs are generated once by CMake into irq_registry_config.h and never
 * change. Who actually handles the interrupt is decided at runtime by
 * Register()/Unregister() — including "more than one object", see below.
 *
 * === Two ways to become a handler ===
 *
 * 1) Derive from IIRQHandler (the normal path for drivers — see USART, SPI,
 *    TIM, RTC, ADC in this directory):
 *
 *      class USART : public IIRQHandler { void HandleIRQ() override { ... } };
 *      IRQ_Registry::Register(USART1_IRQn, &my_usart);
 *
 *    Dispatch() calls HandleIRQ() through the object's vtable (a virtual call).
 *
 * 2) A plain function or non-capturing lambda, no class needed — see
 *    Register(IRQn_Type, IRQHandlerFn, void*) below. Under the hood this is
 *    still (1): a hidden FunctionHandler adapter (private nested class, see
 *    below) implements IIRQHandler and forwards HandleIRQ() to your function.
 *    Adapters come from a fixed-size static pool (IRQ_MAX_FUNCTION_HANDLERS
 *    slots) — no new/malloc, same "must stay valid forever" contract as (1).
 *
 * === The dispatch table ===
 *
 * _table[IRQ_TABLE_SIZE][IRQ_MAX_SHARED] holds IIRQHandler* — first index is
 * the IRQn (0-based; IRQ_TABLE_SIZE is computed per-MCU by CMake from its
 * vector table), second index is a slot for the rare case where several
 * peripherals share one vector (IRQ_MAX_SHARED, also family-specific — see
 * below). The whole table lives in .bss, zero-initialized, so Register() /
 * Unregister() are just a pointer store / clear — no locking, no allocation.
 * Dispatch() walks every non-null slot of one row and calls HandleIRQ() on it.
 *
 * === Full path of one interrupt ===
 *
 *   NVIC (hardware: ~12 cycles to stack 8 registers + load PC from the vector table)
 *     -> ISR stub, generated in irq_registry_config.h: IRQ_Registry::Dispatch(XXX_IRQn)
 *          -> bounds check: idx < IRQ_TABLE_SIZE
 *          -> for each slot s in [0, IRQ_MAX_SHARED): if _table[idx][s] != nullptr
 *               -> _table[idx][s]->HandleIRQ()   (virtual call through the vtable)
 *
 * === Overhead ===
 *
 * The stub call + Dispatch() (bounds check, table indexing, null check) plus
 * one virtual call adds roughly 6-14 cycles (~0.003-0.02% of a typical 1 ms
 * task period) over a hardwired direct virtual call with no table at all —
 * negligible except for interrupts firing in the hundreds of kHz on Cortex-M0.
 * For the full per-core (G0/F4/G4/F7) cycle-by-cycle breakdown, and worked
 * usage examples (inheritance vs. the plain-function Register() below), see
 * Drivers/IRQ_Registry.md, sections 7-8 and the examples at the end.
 */
class IIRQHandler
{
public:
	virtual void HandleIRQ() = 0;
};

// IRQ_MAX_SHARED is defined before this header in irq_registry_config.h.
// Provide a fallback when included directly by peripheral drivers.
#ifndef IRQ_MAX_SHARED
#  define IRQ_MAX_SHARED 3
#endif

// Pool size for Register(IRQn_Type, IRQHandlerFn, void*) — independent of
// IRQ_MAX_SHARED, since function handlers are drawn from one shared pool
// across all IRQ lines rather than per-line slots. Override before including
// this header (or irq_registry_config.h) if more concurrent registrations
// are needed.
#ifndef IRQ_MAX_FUNCTION_HANDLERS
#  define IRQ_MAX_FUNCTION_HANDLERS 8
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

	/** @brief Signature for a plain-function IRQ handler; ctx is whatever was passed to Register(). */
	using IRQHandlerFn = void (*)(void* ctx);

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
	 * @brief Registers a plain function (or non-capturing lambda) as an IRQ
	 *        handler, without deriving a class from IIRQHandler.
	 *
	 * Internally binds an IIRQHandler adapter drawn from a fixed-size pool
	 * (IRQ_MAX_FUNCTION_HANDLERS slots, shared across all IRQs) and registers
	 * it exactly like Register(IRQn_Type, IIRQHandler*) — same per-line slot
	 * and BKPT rules apply there. The adapter pool has its own, independent
	 * limit and traps (BKPT + loop) when exhausted.
	 *
	 * @param irqn    Peripheral IRQ number (non-negative, from IRQn_Type).
	 * @param fn      Function to call from the ISR; must remain valid forever.
	 * @param ctx     Opaque pointer passed back to fn on every call (may be nullptr).
	 * @return true on success, false if irqn is out of range.
	 */
	static bool Register(IRQn_Type irqn, IRQHandlerFn fn, void* ctx = nullptr);

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
	/**
	 * @brief IIRQHandler adapter that forwards HandleIRQ() to a bound plain function.
	 *
	 * Instances live only in the static _function_pool below — never heap- or
	 * stack-allocated — so the pointer handed to Register(IIRQHandler*) stays
	 * valid forever, same as any other handler object.
	 */
	class FunctionHandler final : public IIRQHandler
	{
	public:
		void Bind(IRQHandlerFn fn, void* ctx) { _fn = fn; _ctx = ctx; }
		void HandleIRQ() override { if (_fn) _fn(_ctx); }

	private:
		IRQHandlerFn _fn = nullptr;
		void* _ctx = nullptr;
	};

	// Fixed-size pool backing Register(IRQn_Type, IRQHandlerFn, void*).
	// Slots are handed out once, in order, and never reclaimed (Unregister()
	// only clears the _table slot, matching the IIRQHandler overload).
	static FunctionHandler _function_pool[IRQ_MAX_FUNCTION_HANDLERS];
	static int _function_pool_used;

	// First dimension intentionally incomplete — completed in irq_registry_config.h:
	//   IIRQHandler* _table[IRQ_TABLE_SIZE][IRQ_MAX_SHARED] = {}
	static IIRQHandler* _table[][MAX_PER_IRQ];
};

#endif /* IRQ_REGISTRY_HPP */
