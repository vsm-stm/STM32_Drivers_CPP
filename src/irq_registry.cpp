#include "irq_registry_config.h"   // defines IRQ_MAX_SHARED + IRQ_TABLE_SIZE, includes irq_registry.hpp

bool IRQ_Registry::Register(IRQn_Type irqn, IIRQHandler* handler)
{
	int idx = static_cast<int>(irqn);

	if (idx < 0 || idx >= IRQ_TABLE_SIZE)
		return false;

	for (int s = 0; s < MAX_PER_IRQ; ++s) {
		if (_table[idx][s] == nullptr) {
			_table[idx][s] = handler;
			return true;
		}
	}

	// all slots are taken - a configuration error
	__BKPT(0);
	while (1);
	return false;
}

IRQ_Registry::FunctionHandler IRQ_Registry::_function_pool[IRQ_MAX_FUNCTION_HANDLERS];
int IRQ_Registry::_function_pool_used = 0;

bool IRQ_Registry::Register(IRQn_Type irqn, IRQHandlerFn fn, void* ctx)
{
	if (_function_pool_used >= IRQ_MAX_FUNCTION_HANDLERS) {
		// the adapter pool is exhausted - increase IRQ_MAX_FUNCTION_HANDLERS
		__BKPT(0);
		while (1);
		return false;
	}

	FunctionHandler* adapter = &_function_pool[_function_pool_used++];
	adapter->Bind(fn, ctx);
	return Register(irqn, adapter);
}

void IRQ_Registry::Unregister(IRQn_Type irqn)
{
	int idx = static_cast<int>(irqn);

	if (idx < 0 || idx >= IRQ_TABLE_SIZE) return;
	for (int s = 0; s < MAX_PER_IRQ; ++s)
		_table[idx][s] = nullptr;
}

void IRQ_Registry::Dispatch(IRQn_Type irqn)
{
	int idx = static_cast<int>(irqn);

	if (idx < 0 || idx >= IRQ_TABLE_SIZE) return;
	for (int s = 0; s < MAX_PER_IRQ; ++s) {
		if (_table[idx][s])
			_table[idx][s]->HandleIRQ();
	}
}
