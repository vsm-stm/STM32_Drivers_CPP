/**
 * @file irq_registry.cpp
 * @brief Registry insertion/removal and dispatch; no locks or NVIC manipulation.
 * Mutations are a main-loop operation with the affected IRQ masked. The caller
 * must never mutate the table/pool during Dispatch or one of its callbacks.
 */
#include "irq_registry_config.h"   // defines IRQ_TABLE_SIZE/table/ISRs; includes the common irq_registry.hpp

SysInitStatus IRQ_Registry::Register(IRQn_Type irqn, IIRQHandler* handler)
{
	int idx = static_cast<int>(irqn);

	if (idx < 0 || idx >= IRQ_TABLE_SIZE || handler == nullptr)
		return SysInitStatus::InitError;

	// Search the entire row for duplicates before occupying the first free slot.
	int free_slot = -1;
	for (int s = 0; s < MAX_PER_IRQ; ++s) {
		if (_table[idx][s] == handler) return SysInitStatus::InitError;
		if (_table[idx][s] == nullptr && free_slot < 0) free_slot = s;
	}
	if (free_slot >= 0) {
		_table[idx][free_slot] = handler;
		return SysInitStatus::InitOK;
	}

	return SysInitStatus::InitError;
}

IRQ_Registry::FunctionHandler IRQ_Registry::_function_pool[IRQ_MAX_FUNCTION_HANDLERS];

SysInitStatus IRQ_Registry::Register(IRQn_Type irqn, IRQHandlerFn fn, void* ctx)
{
	const int idx = static_cast<int>(irqn);
	if (idx < 0 || idx >= IRQ_TABLE_SIZE || fn == nullptr)
		return SysInitStatus::InitError;
	if (FindFunction(idx, fn) != nullptr) return SysInitStatus::InitError;

	for (FunctionHandler& adapter : _function_pool) {
		if (!adapter.IsFree()) continue;
		adapter.Bind(fn, ctx);
		const SysInitStatus result = Register(irqn, &adapter);
		// A failed row insertion must not leak a function adapter from the pool.
		if (result != SysInitStatus::InitOK) adapter.Bind(nullptr, nullptr);
		return result;
	}
	return SysInitStatus::InitError;
}

void IRQ_Registry::Unregister(IRQn_Type irqn)
{
	int idx = static_cast<int>(irqn);

	if (idx < 0 || idx >= IRQ_TABLE_SIZE) return;
	for (int s = 0; s < MAX_PER_IRQ; ++s) {
		ReleaseFunction(_table[idx][s]);
		_table[idx][s] = nullptr;
	}
}

void IRQ_Registry::Unregister(IRQn_Type irqn, IIRQHandler* handler)
{
	const int idx = static_cast<int>(irqn);
	if (idx < 0 || idx >= IRQ_TABLE_SIZE || handler == nullptr) return;
	for (int s = 0; s < MAX_PER_IRQ; ++s) {
		if (_table[idx][s] != handler) continue;
		_table[idx][s] = nullptr;
		ReleaseFunction(handler);
	}
}

bool IRQ_Registry::HasHandlers(IRQn_Type irqn)
{
	const int idx = static_cast<int>(irqn);
	if (idx < 0 || idx >= IRQ_TABLE_SIZE) return false;
	for (int s = 0; s < MAX_PER_IRQ; ++s)
		if (_table[idx][s] != nullptr) return true;
	return false;
}

void IRQ_Registry::Unregister(IRQn_Type irqn, IRQHandlerFn fn)
{
	const int idx = static_cast<int>(irqn);
	if (idx < 0 || idx >= IRQ_TABLE_SIZE || fn == nullptr) return;
	FunctionHandler* adapter = FindFunction(idx, fn);
	if (adapter != nullptr) Unregister(irqn, static_cast<IIRQHandler*>(adapter));
}

IRQ_Registry::FunctionHandler* IRQ_Registry::FindFunction(int idx, IRQHandlerFn fn)
{
	for (FunctionHandler& adapter : _function_pool) {
		if (!adapter.Matches(fn)) continue;
		for (int s = 0; s < MAX_PER_IRQ; ++s)
			if (_table[idx][s] == &adapter) return &adapter;
	}
	return nullptr;
}

void IRQ_Registry::ReleaseFunction(IIRQHandler* handler)
{
	if (handler == nullptr) return;
	for (FunctionHandler& adapter : _function_pool) {
		if (&adapter != handler) continue;
		adapter.Bind(nullptr, nullptr);
		return;
	}
}

// Shared vectors call every occupied slot. The peripheral handler, not this
// dispatcher, decides which of its enabled sources actually raised the IRQ.
void IRQ_Registry::Dispatch(IRQn_Type irqn)
{
	int idx = static_cast<int>(irqn);

	if (idx < 0 || idx >= IRQ_TABLE_SIZE) return;
	for (int s = 0; s < MAX_PER_IRQ; ++s) {
		IIRQHandler* handler = _table[idx][s];
		if (handler != nullptr) handler->HandleIRQ();
	}
}
