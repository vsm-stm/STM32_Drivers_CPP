#include "irq_registry.hpp"
#include "irq_registry_config.h"

bool IRQ_Registry::Register(IRQn_Type irqn, IIRQHandler* handler)
{
	int idx = static_cast<int>(irqn);

	if (idx < 0 || idx >= IRQ_TABLE_SIZE)
		return false;

	if (_table[idx] != nullptr)
	{
		__BKPT(0);
		while(1);
		return false;
	}

	_table[idx] = handler;
	return true;
}

void IRQ_Registry::Unregister(IRQn_Type irqn)
{
	int idx = static_cast<int>(irqn);

	if (idx >= 0 && idx < IRQ_TABLE_SIZE)
		_table[idx] = nullptr;
}

void IRQ_Registry::Dispatch(IRQn_Type irqn)
{
	int idx = static_cast<int>(irqn);

	if (idx >= 0 && idx < IRQ_TABLE_SIZE && _table[idx] != nullptr)
		_table[idx]->HandleIRQ();
}
