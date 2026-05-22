#include "irq_registry.hpp"
#include "irq_registry_config.h"

bool IRQ_Registry::Register(IRQn_Type irqn, IIRQHandler* handler)
{
	int idx = static_cast<int>(irqn);

	if (idx < 0 || idx >= IRQ_TABLE_SIZE)
		return false;

	if (_table[idx] == nullptr) {
		_table[idx] = handler;
	} else {
		IIRQHandler* tail = _table[idx];
		while (tail->_irq_next) tail = tail->_irq_next;
		tail->_irq_next = handler;
	}
	return true;
}

void IRQ_Registry::Unregister(IRQn_Type irqn)
{
	int idx = static_cast<int>(irqn);

	if (idx < 0 || idx >= IRQ_TABLE_SIZE) return;
	_table[idx] = nullptr;  // clears the whole chain; acceptable for exclusive-use lines
}

void IRQ_Registry::Dispatch(IRQn_Type irqn)
{
	int idx = static_cast<int>(irqn);

	if (idx < 0 || idx >= IRQ_TABLE_SIZE) return;
	for (IIRQHandler* h = _table[idx]; h; h = h->_irq_next)
		h->HandleIRQ();
}
