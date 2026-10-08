/**
 * @file    tim_trig_defs.hpp
 * @brief   Trigger requests for TIM_TriggerGenerator — what a timer triggers.
 *
 * Included inside the TIM_TriggerGenerator class body, the same way
 * dma_requests.hpp is included inside DMA_Sx. A request names the timer that
 * produces the trigger, the peripheral that consumes it, which timer event is
 * the trigger (TRGO, TRGO2 or a compare channel) and the consumer's EXTSEL
 * value:
 *
 * @code
 *   TIM_TriggerGenerator trg(TIM3, TIM_TriggerGenerator::Req::Adc::TIM3_TRGO);
 * @endcode
 *
 * The constructor checks that the request belongs to the given timer; the
 * consumer (ADC_N::AttachTrig()) checks that the request is meant for it.
 *
 * EXTSEL values verified against the ST LL headers (LL_ADC_REG_TRIG_EXT_*).
 * The trigger tables differ between G0, F4 and F7; a request exists only
 * where its timer does.
 */

#ifndef TIM_TRIG_DEFS_H_
#define TIM_TRIG_DEFS_H_

/** @brief Peripheral that consumes the trigger. */
enum class TrigTarget : uint8_t {
	Adc,
};

/** @brief Timer event used as the trigger. */
enum class TrigSource : uint8_t {
	TRGO,    ///< Update event -> TRGO (MMS = 010)
	TRGO2,   ///< Update event -> TRGO2 (MMS2 = 0010), TIM1/TIM8 only
	CC1,     ///< Compare match on channel 1
	CC2,     ///< Compare match on channel 2
	CC3,     ///< Compare match on channel 3
	CC4,     ///< Compare match on channel 4
};

/** @brief One trigger: producing timer, consumer, event, consumer's EXTSEL value. */
struct TrigReq {
	uint32_t   tim_base;
	TrigTarget target;
	TrigSource source;
	uint8_t    extsel;
};

struct Req {

#if defined(STM32G0)

	/// ADC1 regular group, CFGR1.EXTSEL[2:0]
	struct Adc {
		static constexpr TrigReq TIM1_TRGO2 = { TIM1_BASE,  TrigTarget::Adc, TrigSource::TRGO2, 0 };
		static constexpr TrigReq TIM1_CC4   = { TIM1_BASE,  TrigTarget::Adc, TrigSource::CC4,   1 };
#if defined(TIM2_BASE)
		static constexpr TrigReq TIM2_TRGO  = { TIM2_BASE,  TrigTarget::Adc, TrigSource::TRGO,  2 };
#endif
		static constexpr TrigReq TIM3_TRGO  = { TIM3_BASE,  TrigTarget::Adc, TrigSource::TRGO,  3 };
#if defined(TIM15_BASE)
		static constexpr TrigReq TIM15_TRGO = { TIM15_BASE, TrigTarget::Adc, TrigSource::TRGO,  4 };
#endif
#if defined(TIM6_BASE)
		static constexpr TrigReq TIM6_TRGO  = { TIM6_BASE,  TrigTarget::Adc, TrigSource::TRGO,  5 };
#endif
#if defined(TIM4_BASE)
		static constexpr TrigReq TIM4_TRGO  = { TIM4_BASE,  TrigTarget::Adc, TrigSource::TRGO,  6 };
#endif
	};

#elif defined(STM32F7)

	/// ADC1/2/3 regular group, CR2.EXTSEL[3:0]
	struct Adc {
		static constexpr TrigReq TIM1_CC1   = { TIM1_BASE, TrigTarget::Adc, TrigSource::CC1,   0 };
		static constexpr TrigReq TIM1_CC2   = { TIM1_BASE, TrigTarget::Adc, TrigSource::CC2,   1 };
		static constexpr TrigReq TIM1_CC3   = { TIM1_BASE, TrigTarget::Adc, TrigSource::CC3,   2 };
		static constexpr TrigReq TIM2_CC2   = { TIM2_BASE, TrigTarget::Adc, TrigSource::CC2,   3 };
		static constexpr TrigReq TIM5_TRGO  = { TIM5_BASE, TrigTarget::Adc, TrigSource::TRGO,  4 };
		static constexpr TrigReq TIM4_CC4   = { TIM4_BASE, TrigTarget::Adc, TrigSource::CC4,   5 };
		static constexpr TrigReq TIM3_CC4   = { TIM3_BASE, TrigTarget::Adc, TrigSource::CC4,   6 };
		static constexpr TrigReq TIM8_TRGO  = { TIM8_BASE, TrigTarget::Adc, TrigSource::TRGO,  7 };
		static constexpr TrigReq TIM8_TRGO2 = { TIM8_BASE, TrigTarget::Adc, TrigSource::TRGO2, 8 };
		static constexpr TrigReq TIM1_TRGO  = { TIM1_BASE, TrigTarget::Adc, TrigSource::TRGO,  9 };
		static constexpr TrigReq TIM1_TRGO2 = { TIM1_BASE, TrigTarget::Adc, TrigSource::TRGO2, 10 };
		static constexpr TrigReq TIM2_TRGO  = { TIM2_BASE, TrigTarget::Adc, TrigSource::TRGO,  11 };
		static constexpr TrigReq TIM4_TRGO  = { TIM4_BASE, TrigTarget::Adc, TrigSource::TRGO,  12 };
		static constexpr TrigReq TIM6_TRGO  = { TIM6_BASE, TrigTarget::Adc, TrigSource::TRGO,  13 };
	};

#elif defined(STM32F4)

	/// ADC1/2/3 regular group, CR2.EXTSEL[3:0]
	struct Adc {
		static constexpr TrigReq TIM1_CC1   = { TIM1_BASE, TrigTarget::Adc, TrigSource::CC1,  0 };
		static constexpr TrigReq TIM1_CC2   = { TIM1_BASE, TrigTarget::Adc, TrigSource::CC2,  1 };
		static constexpr TrigReq TIM1_CC3   = { TIM1_BASE, TrigTarget::Adc, TrigSource::CC3,  2 };
#if defined(TIM2_BASE)
		static constexpr TrigReq TIM2_CC2   = { TIM2_BASE, TrigTarget::Adc, TrigSource::CC2,  3 };
		static constexpr TrigReq TIM2_CC3   = { TIM2_BASE, TrigTarget::Adc, TrigSource::CC3,  4 };
		static constexpr TrigReq TIM2_CC4   = { TIM2_BASE, TrigTarget::Adc, TrigSource::CC4,  5 };
		static constexpr TrigReq TIM2_TRGO  = { TIM2_BASE, TrigTarget::Adc, TrigSource::TRGO, 6 };
#endif
#if defined(TIM3_BASE)
		static constexpr TrigReq TIM3_CC1   = { TIM3_BASE, TrigTarget::Adc, TrigSource::CC1,  7 };
		static constexpr TrigReq TIM3_TRGO  = { TIM3_BASE, TrigTarget::Adc, TrigSource::TRGO, 8 };
#endif
#if defined(TIM4_BASE)
		static constexpr TrigReq TIM4_CC4   = { TIM4_BASE, TrigTarget::Adc, TrigSource::CC4,  9 };
#endif
		static constexpr TrigReq TIM5_CC1   = { TIM5_BASE, TrigTarget::Adc, TrigSource::CC1,  10 };
		static constexpr TrigReq TIM5_CC2   = { TIM5_BASE, TrigTarget::Adc, TrigSource::CC2,  11 };
		static constexpr TrigReq TIM5_CC3   = { TIM5_BASE, TrigTarget::Adc, TrigSource::CC3,  12 };
#if defined(TIM8_BASE)
		static constexpr TrigReq TIM8_CC1   = { TIM8_BASE, TrigTarget::Adc, TrigSource::CC1,  13 };
		static constexpr TrigReq TIM8_TRGO  = { TIM8_BASE, TrigTarget::Adc, TrigSource::TRGO, 14 };
#endif
	};

#endif

}; // struct Req

#endif // TIM_TRIG_DEFS_H_
