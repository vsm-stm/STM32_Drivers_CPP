#include "exti.hpp"

uint16_t EXTI_Line::_used_lines = 0;

#if defined(STM32G0)
#define EXTI_INPUT_MASK EXTI->IMR1
#define EXTI_EVENT_MASK EXTI->EMR1
#define EXTI_RISING EXTI->RTSR1
#define EXTI_FALLING EXTI->FTSR1
#else
#define EXTI_INPUT_MASK EXTI->IMR
#define EXTI_EVENT_MASK EXTI->EMR
#define EXTI_RISING EXTI->RTSR
#define EXTI_FALLING EXTI->FTSR
#endif

IRQn_Type EXTI_Line::GetIRQn() const
{
    const uint32_t pin = _pin.GetNumber();
#if defined(STM32G0)
    if (pin < 2U) return EXTI0_1_IRQn;
    if (pin < 4U) return EXTI2_3_IRQn;
    return EXTI4_15_IRQn;
#else
    static constexpr IRQn_Type irqs[] = {
        EXTI0_IRQn, EXTI1_IRQn, EXTI2_IRQn, EXTI3_IRQn, EXTI4_IRQn,
        EXTI9_5_IRQn, EXTI15_10_IRQn
    };
    return irqs[pin < 5U ? pin : pin < 10U ? 5U : 6U];
#endif
}

SysInitStatus EXTI_Line::SetUp(PIN::TYPE type)
{
    const uint32_t pin = _pin.GetNumber();
    const uint32_t mask = 1U << pin;
    if (_initialized || (_used_lines & mask)
        || (type != PIN::TYPE::INPUT_NO_Pull && type != PIN::TYPE::INPUT_PullUp
            && type != PIN::TYPE::INPUT_PullDown)) return SysInitStatus::InitError;

    const IRQn_Type irq = GetIRQn();
    const bool enabled = NVIC_GetEnableIRQ(irq) != 0;
    NVIC_DisableIRQ(irq);
    if (IRQ_Registry::Register(irq, this) != SysInitStatus::InitOK) {
        if (enabled) NVIC_EnableIRQ(irq);
        return SysInitStatus::InitError;
    }

    EXTI_CLEAR_BIT(EXTI_INPUT_MASK, mask);
    EXTI_CLEAR_BIT(EXTI_EVENT_MASK, mask); // This owner uses interrupts, not events.
    _pin.SetUp(type);
    const uint32_t port = (reinterpret_cast<uintptr_t>(_pin.PORT()) - GPIOA_BASE)
        / (GPIOB_BASE - GPIOA_BASE);
#if defined(STM32G0)
    // Four 8-bit selector slots per register, in EXTI itself (not SYSCFG).
    const uint32_t shift = (pin & 3U) * 8U;
    EXTI_MODIFY_REG(EXTI->EXTICR[pin >> 2], EXTI_EXTICR1_EXTI0 << shift, port << shift);
#else
    EXTI_SET_BIT(RCC->APB2ENR, RCC_APB2ENR_SYSCFGEN);
    (void)RCC->APB2ENR;
    const uint32_t shift = (pin & 3U) * 4U;
    EXTI_MODIFY_REG(SYSCFG->EXTICR[pin >> 2], SYSCFG_EXTICR1_EXTI0 << shift, port << shift);
#endif
    const uint32_t edges = static_cast<uint32_t>(_edge);
    EXTI_MODIFY_REG(EXTI_RISING, mask, (edges & 1U) ? mask : 0U);
    EXTI_MODIFY_REG(EXTI_FALLING, mask, (edges & 2U) ? mask : 0U);
#if defined(STM32G0)
    EXTI->RPR1 = mask;
    EXTI->FPR1 = mask;
#else
    EXTI->PR = mask;
#endif
    _used_lines |= static_cast<uint16_t>(mask);
    _initialized = true;
    EXTI_SET_BIT(EXTI_INPUT_MASK, mask);
    NVIC_EnableIRQ(irq);
    return SysInitStatus::InitOK;
}

void EXTI_Line::IRQ_en(FunctionalState en)
{
    if (!_initialized) return;
    const uint32_t mask = 1U << _pin.GetNumber();
    if (en) EXTI_SET_BIT(EXTI_INPUT_MASK, mask);
    else EXTI_CLEAR_BIT(EXTI_INPUT_MASK, mask);
}

void EXTI_Line::HandleIRQ()
{
    const uint32_t mask = 1U << _pin.GetNumber();
    if (!(EXTI_INPUT_MASK & mask)) return;
#if defined(STM32G0)
    const uint32_t rising = EXTI->RPR1 & mask;
    const uint32_t falling = EXTI->FPR1 & mask;
    if (!(rising | falling)) return;
    if (rising) EXTI->RPR1 = rising;
    if (falling) EXTI->FPR1 = falling;
#else
    const uint32_t pending = EXTI->PR & mask;
    if (!pending) return;
    EXTI->PR = pending;
#endif
    OnInterrupt();
}

void EXTI_Line::DeInit()
{
    if (!_initialized) return;
    const uint32_t mask = 1U << _pin.GetNumber();
    const IRQn_Type irq = GetIRQn();
    const bool enabled = NVIC_GetEnableIRQ(irq) != 0;
    NVIC_DisableIRQ(irq);
    EXTI_CLEAR_BIT(EXTI_INPUT_MASK, mask);
    EXTI_CLEAR_BIT(EXTI_RISING, mask);
    EXTI_CLEAR_BIT(EXTI_FALLING, mask);
#if defined(STM32G0)
    EXTI->RPR1 = mask;
    EXTI->FPR1 = mask;
#else
    EXTI->PR = mask;
#endif
    IRQ_Registry::Unregister(irq, this);
    _used_lines &= static_cast<uint16_t>(~mask);
    _initialized = false;
    if (IRQ_Registry::HasHandlers(irq)) {
        if (enabled) NVIC_EnableIRQ(irq);
    } else NVIC_ClearPendingIRQ(irq);
}
