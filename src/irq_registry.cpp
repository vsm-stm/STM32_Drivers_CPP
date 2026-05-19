#include "irq_registry.hpp"
#include "irq_registry_config.h"

namespace IRQ_Registry
{
	bool Register(IRQn_Type irqn, IIRQHandler* handler)
	{
		int idx = static_cast<int>(irqn);

		// Проверка границ
		if (idx < 0 || idx >= IRQ_TABLE_SIZE)
			return false;

		// Проверка, не занято ли уже
		if (_irq_table[idx] != nullptr)
		{
			__BKPT(0);  // Ошибка: IRQ уже зарегистрирован
			while(1);
			return false;
		}

		_irq_table[idx] = handler;
		return true;
	}

	void Unregister(IRQn_Type irqn)
	{
		int idx = static_cast<int>(irqn);

		if (idx >= 0 && idx < IRQ_TABLE_SIZE)
			_irq_table[idx] = nullptr;
	}

	void Dispatch(IRQn_Type irqn)
	{
		int idx = static_cast<int>(irqn);

		if (idx >= 0 && idx < IRQ_TABLE_SIZE && _irq_table[idx] != nullptr)
			_irq_table[idx]->HandleIRQ();
	}
}
