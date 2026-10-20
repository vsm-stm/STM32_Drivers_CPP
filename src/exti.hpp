/**
 * @file exti.hpp
 * @brief One external GPIO interrupt line, STM32F4/F7/G0.
 *
 * PIN and Rising/Falling/Both selection are fixed by the constructor. After
 * System/RCC initialization, SetUp configures the GPIO input/pull, selects its
 * EXTI port, clears old pending flags and enables the interrupt. One live owner
 * per line number: PA0 and PB0 cannot own EXTI0 simultaneously.
 *
 * Each object registers separately in IRQ_Registry, including on shared vectors.
 * IRQ_MAX_SHARED defaults to 3 in irq_registry_settings.hpp. G0 lines 4..15 use
 * one vector: at the default capacity, a fourth object on it returns InitError.
 * F4/F7 lines 5..9 and 10..15 also share vectors and have the same row limit.
 * No capacity changes or separate callback tables are made by this driver.
 *
 * SetUp/DeInit run in main; callbacks must not change registration or setup.
 * Serialize main-side shared-register writes with the affected EXTI IRQs.
 * The handler checks its own enabled source and acknowledges only its flags
 * before calling OnInterrupt. There are no hardware waits or GPIO reads in IRQ.
 * Both means notification of either edge, not a reliable edge counter/direction.
 * G0 coalesces simultaneous rising/falling pending bits into one notification.
 *
 * @code
 * #include "exti.hpp"
 * class Button final : public EXTI_Line {
 * public:
 *     Button() : EXTI_Line(PIN(GPIOC, 13), Edge::Falling) {}
 *     volatile bool pressed = false;
 * protected:
 *     void OnInterrupt() override { pressed = true; }
 * };
 * Button button;
 * // After System/RCC initialization, in main:
 * // if (button.SetUp(PIN::TYPE::INPUT_PullUp) != SysInitStatus::InitOK) ...;
 * // button.IRQ_en(DISABLE); // Mask this line only; NVIC/other lines stay enabled.
 * // button.IRQ_en(ENABLE);  // Previously pending events are not discarded.
 * // button.DeInit();        // Explicit teardown before destroying button.
 * @endcode
 */
#ifndef EXTI_HPP_
#define EXTI_HPP_

#include "gpio.hpp"
#include "irq_registry.hpp"

#if !defined(STM32F4) && !defined(STM32F7) && !defined(STM32G0)
#error "EXTI_Line supports STM32F4/F7/G0 only."
#endif

// Control-register operations only. Pending flags are always direct W1C writes.
#ifndef EXTI_USE_ATOMIC
#define EXTI_USE_ATOMIC 0
#endif
#if EXTI_USE_ATOMIC
#define EXTI_SET_BIT ATOMIC_SET_BIT
#define EXTI_CLEAR_BIT ATOMIC_CLEAR_BIT
#define EXTI_MODIFY_REG ATOMIC_MODIFY_REG
#else
#define EXTI_SET_BIT SET_BIT
#define EXTI_CLEAR_BIT CLEAR_BIT
#define EXTI_MODIFY_REG MODIFY_REG
#endif

class EXTI_Line : public IIRQHandler
{
public:
    enum class Edge : uint8_t { Rising = 1, Falling = 2, Both = 3 };

    /**
     * @brief Bind an external GPIO pin and edge selection, without MMIO access.
     * Absent pins, numbers outside 0..15 and invalid edges cause DebugTrap.
     * Use a valid CMSIS port pointer, or the compile-time checked PIN(GPIOx_BASE,
     * number) form. The selected device/package must expose that physical pin.
     */
    EXTI_Line(PIN pin, Edge edge) : _pin(pin), _edge(edge)
    {
        if (!pin.IsValid() || pin.GetNumber() >= 16U
            || static_cast<uint32_t>(edge) < 1U || static_cast<uint32_t>(edge) > 3U)
            System::DebugTrap("EXTI: invalid pin/edge");
    }
    EXTI_Line(const EXTI_Line&) = delete;
    EXTI_Line(EXTI_Line&&) = delete;
    EXTI_Line& operator=(const EXTI_Line&) = delete;
    EXTI_Line& operator=(EXTI_Line&&) = delete;

    /**
     * @brief Main-only input/pull setup, registration and interrupt enable.
     * type must be INPUT_NO_Pull, INPUT_PullUp or INPUT_PullDown.
     * InitError: repeated setup, a used line, invalid type or registry failure.
     * These errors leave GPIO/EXTI unchanged and restore the previous NVIC mask.
     * InitOK: source and NVIC are enabled. Repeat configuration needs DeInit.
     */
    SysInitStatus SetUp(PIN::TYPE type = PIN::TYPE::INPUT_NO_Pull);

    /// Gate only this source after setup; preserve pending flags and shared NVIC.
    void IRQ_en(FunctionalState en);
    /// The physical IRQ, for caller-controlled priority/serialization.
    IRQn_Type GetIRQn() const;

    /**
     * @brief Main-only release of this line and its registry entry, no callbacks.
     * Mask source, disable edge detection, acknowledge own flags, free ownership.
     * Other lines/internal sources/handlers survive. NVIC is disabled only while
     * changing the registry, and stays disabled if no registered handlers remain.
     * GPIO input/pull configuration and shared GPIO/SYSCFG clocks are retained.
     * Constructor bindings survive. Call explicitly before object destruction;
     * the destructor performs no hardware teardown.
     */
    void DeInit();

protected:
    /// IRQ notification, after acknowledgement. Default implementation is empty.
    virtual void OnInterrupt() {}

private:
    PIN _pin; // PIN's binding fields are immutable; SetUp configures its hardware.
    const Edge _edge;
    bool _initialized = false;
    static uint16_t _used_lines;

    void HandleIRQ() override final;
};

#endif // EXTI_HPP_
