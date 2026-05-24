#include "irq_registry.hpp"
#include "irq_registry_config.h"   // определяет IRQ_TABLE_SIZE (CMake-generated)

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

	// все слоты заняты — ошибка конфигурации
	__BKPT(0);
	while (1);
	return false;
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
