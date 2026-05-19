#ifndef IRQ_REGISTRY_HPP
#define IRQ_REGISTRY_HPP

#include "system.hpp"
#include <cstdint>

// Базовый интерфейс для объектов, обрабатывающих прерывания
class IIRQHandler
{
public:
	virtual void HandleIRQ() = 0;
	virtual ~IIRQHandler() = default;
};

namespace IRQ_Registry
{
	// Регистрирует обработчик для IRQn
	// Возвращает false если уже зарегистрирован
	bool Register(IRQn_Type irqn, IIRQHandler* handler);

	// Отменяет регистрацию
	void Unregister(IRQn_Type irqn);

	// Вызывает зарегистрированный обработчик
	void Dispatch(IRQn_Type irqn);
}

#endif /* IRQ_REGISTRY_HPP */
