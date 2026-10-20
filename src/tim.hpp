#ifndef TIM_HPP_
#define TIM_HPP_

#include "system.hpp"
#include "irq_registry.hpp"
#include "gpio.hpp"
#include "dma.hpp"
#include <initializer_list>

// Change here and rebuild this driver and its consumers.
#ifndef TIM_USE_ATOMIC
#define TIM_USE_ATOMIC 0
#endif

#if TIM_USE_ATOMIC
#define TIM_SET_BIT ATOMIC_SET_BIT
#define TIM_CLEAR_BIT ATOMIC_CLEAR_BIT
#define TIM_MODIFY_REG ATOMIC_MODIFY_REG
#else
#define TIM_SET_BIT SET_BIT
#define TIM_CLEAR_BIT CLEAR_BIT
#define TIM_MODIFY_REG MODIFY_REG
#endif

/**
 * @brief Compile-time checked timer pin descriptor; also exposed as TIM::TIM_PIN.
 * Stores the existing port/pin/AF/channel/timer metadata. Named table entries
 * are constexpr; the GPIO port, pin, AF, channel range and timer presence are
 * checked by the constructor. Package bonding and exact AF routing are caller
 * obligations. Copies are used to map constructor channel descriptions.
 */
struct TIM_PIN {
	uint32_t port = 0;
	uint8_t pin = 0;
	uint8_t af = 0;
	uint8_t channel = 0;
	uint32_t tim_base = 0;

	TIM_PIN() = default;
	consteval TIM_PIN(uint32_t gpio, uint32_t number, uint32_t alternate,
		uint32_t ch, uint32_t timer)
		: port(gpio), pin(static_cast<uint8_t>(number)), af(static_cast<uint8_t>(alternate)),
		  channel(static_cast<uint8_t>(ch)), tim_base(timer)
	{
		(void)PIN(static_cast<uintptr_t>(gpio), number, alternate, timer);
		if (ch >= 4) __builtin_unreachable();
		switch (timer) {
#if defined(TIM1_BASE)
		case TIM1_BASE:
#endif
#if defined(TIM2_BASE)
		case TIM2_BASE:
#endif
#if defined(TIM3_BASE)
		case TIM3_BASE:
#endif
#if defined(TIM4_BASE)
		case TIM4_BASE:
#endif
#if defined(TIM5_BASE)
		case TIM5_BASE:
#endif
#if defined(TIM8_BASE)
		case TIM8_BASE:
#endif
#if defined(TIM9_BASE)
		case TIM9_BASE:
#endif
#if defined(TIM10_BASE)
		case TIM10_BASE:
#endif
#if defined(TIM11_BASE)
		case TIM11_BASE:
#endif
#if defined(TIM12_BASE)
		case TIM12_BASE:
#endif
#if defined(TIM13_BASE)
		case TIM13_BASE:
#endif
#if defined(TIM14_BASE)
		case TIM14_BASE:
#endif
#if defined(TIM15_BASE)
		case TIM15_BASE:
#endif
#if defined(TIM16_BASE)
		case TIM16_BASE:
#endif
#if defined(TIM17_BASE)
		case TIM17_BASE:
#endif
			break;
		default: __builtin_unreachable();
		}
	}
	constexpr bool IsValid() const { return port != 0; }
	/// Preserve this entry's AF when configuring its GPIO through the shared PIN API.
	PIN ToPin() const { return PIN(reinterpret_cast<GPIO_TypeDef*>(port), pin, af); }
};

/**
 * @brief One hardware TIM with fixed pins/DMA and independently configured channels.
 *
 * Configure System/RCC, construct TIM, call SetUp once, configure channels and
 * trigger output, then Start. Whole-timer modes use their initializer instead;
 * quadrature is started by GenPulse. Configuration and DeInit belong to main. Channels
 * cannot change their role until DeInit. Start/Stop and PWM values remain usable.
 * Callbacks are function pointers supplied only by initialization functions.
 *
 * Timer IRQ and each channel's DMA IRQ have separate registered objects. Each
 * DMA handler reads only its own flags. No IRQ waits for hardware or changes
 * the registry. A shared DMA vector needs one registry slot per attached DMA.
 * The caller masks this TIM's timer/DMA IRQs around main-side operations that
 * modify DIER (including SendDMA and IRQ_en). No internal locks/masking exist.
 * Timer/DMA handlers for the same object must not preempt one another.
 *
 * @code
 *   constinit TIM timer(TIM::_3::instance, {TIM::_3::CH1::PA6}, {TIM::_3::CH2::PA7});
 *   if (timer.SetUp(1000, &Tick, 1000) == SysInitStatus::InitOK) {
 *       timer.SetUpPWM(TIM::Channel::CH1, 250); // Check InitOK.
 *       timer.SetCompareIRQ(TIM::Channel::CH3, 500, &HalfTick); // No GPIO needed.
 *       timer.Start();
 *   }
 * @endcode
 *
 * TIM owns its channel DMA objects. TIM and active buffers must stay alive. DeInit is explicit and
 * releases only acquired resources; the destructor does not touch hardware.
 * Whole-timer encoder, quadrature and burst/counter modes are initialization
 * functions of this class. Old TIM_* subclasses below are reference comments.
 */
class TIM : public IIRQHandler
{
public:
    enum class Channel : uint8_t { CH1, CH2, CH3, CH4 };
    enum class EncoderMode : uint32_t {
        X2_CH1 = TIM_SMCR_SMS_0, X2_CH2 = TIM_SMCR_SMS_1,
        X4 = TIM_SMCR_SMS_0 | TIM_SMCR_SMS_1
    };
    enum class SlaveMode : uint32_t {
        Reset = TIM_SMCR_SMS_2,
        Gated = TIM_SMCR_SMS_2 | TIM_SMCR_SMS_0,
        Trigger = TIM_SMCR_SMS_2 | TIM_SMCR_SMS_1,
        ExternalClock = TIM_SMCR_SMS_2 | TIM_SMCR_SMS_1 | TIM_SMCR_SMS_0
    };
    enum class IRQ : uint32_t {
        UE = TIM_DIER_UIE, CC1 = TIM_DIER_CC1IE, CC2 = TIM_DIER_CC2IE,
        CC3 = TIM_DIER_CC3IE, CC4 = TIM_DIER_CC4IE
    };
    /// Hardware MMS field. ComparePulse is CC1's event; OCxRef needs a PWM channel.
    enum class Trigger : uint32_t {
        Update = 2U << TIM_CR2_MMS_Pos,
        ComparePulse = 3U << TIM_CR2_MMS_Pos,
        OC1Ref = 4U << TIM_CR2_MMS_Pos, OC2Ref = 5U << TIM_CR2_MMS_Pos,
        OC3Ref = 6U << TIM_CR2_MMS_Pos, OC4Ref = 7U << TIM_CR2_MMS_Pos
    };
    using TIM_PIN = ::TIM_PIN;
    /** Fixed device metadata from tim_defs.hpp; no runtime lookup. */
    struct Instance {
        uintptr_t periph;
        uintptr_t clk_reg;
        uint32_t clk_bit;
        uintptr_t rst_reg;
        uint32_t rst_bit;
        const uint32_t* bus_clk;
        IRQn_Type irq_up, irq_cc;
        bool has_bdtr;
        uint8_t channel_count;
        uint32_t arr_max;
    };
#include "tim_defs.hpp"

#if defined(STM32G0)
    using DMAReq = uintptr_t;
#else
    using DMAReq = DMA_Sx::DMAReq;
#endif
    /** Constructor parameters, not channel objects. Pin determines the channel. */
    struct ChannelConfig {
        TIM_PIN pin;
        DMAReq dma;
        constexpr ChannelConfig(TIM_PIN p = {}, DMAReq d = {}) : pin(p), dma(d) {}
    };
    /**
     * @brief Bind hardware, up to four {pin, dma} descriptions and an optional counter TIM.
     * No MMIO access or DMA acquisition. Bindings have no reassignment API.
     * Use constinit: wrong/duplicate channels and DMA without a pin are compile errors.
     * Named pins are consteval-checked; package bonding remains a caller check.
     * F4/F7 take a named Req; G0 takes a physical channel and derives its request.
     * Route validation is done in the constructor; the existing channel map is shared.
     * counter is the second TIM for finite quadrature generation. It is fixed
     * here, not attached later. Construct it first and keep it alive throughout
     * the generator's use. Init checks the normal TRGO/ITR connection;
     * no hardware or registry resources of either TIM are acquired here.
     * SetUpQuadrature initializes/reserves that counter; generator DeInit
     * releases it. Do not configure/use it independently during this interval.
     * TIM_USE_ATOMIC selects ordinary (0, default) or ST atomic (1) control updates.
     *
     * @code
     *   constexpr TIM::DMAReq dma = TIM::_1::DMA::CH1_DMA2_Stream1; // F4/F7 example
     *   constinit TIM pwm(TIM::_1::instance, {TIM::_1::CH1::PA8, dma});
     *   // G0: constexpr TIM::DMAReq dma = DMA1_Channel3_BASE;
     * @endcode
     */
    constexpr explicit TIM(const Instance& instance, ChannelConfig c0 = {}, ChannelConfig c1 = {},
                 ChannelConfig c2 = {}, ChannelConfig c3 = {}, TIM* counter = nullptr)
        : _instance(instance), _counter(counter),
          _channels{{instance.periph, ChannelBinding(0, c0, c1, c2, c3)},
                    {instance.periph, ChannelBinding(1, c0, c1, c2, c3)},
                    {instance.periph, ChannelBinding(2, c0, c1, c2, c3)},
                    {instance.periph, ChannelBinding(3, c0, c1, c2, c3)}},
          _dma_irq0(*this), _dma_irq1(*this), _dma_irq2(*this), _dma_irq3(*this)
    {
        uint8_t seen = 0;
        for (const ChannelConfig& cfg : {c0, c1, c2, c3}) {
#if defined(STM32G0)
            const bool has_dma = cfg.dma != 0;
#else
            const bool has_dma = cfg.dma.stream || cfg.dma.ch || cfg.dma.periph_base;
#endif
            if (!cfg.pin.IsValid()) {
                if (has_dma) __builtin_unreachable();
                continue;
            }
            if (cfg.pin.channel >= instance.channel_count || cfg.pin.tim_base != instance.periph)
                __builtin_unreachable();
            if (seen & (1U << cfg.pin.channel))
                __builtin_unreachable();
            seen |= static_cast<uint8_t>(1U << cfg.pin.channel);
            for (const ChannelConfig& other : {c0, c1, c2, c3})
                if (other.pin.IsValid() && other.pin.channel != cfg.pin.channel
                    && DMA_Sx::SameResource(cfg.dma, other.dma))
                    __builtin_unreachable();
#if defined(STM32G0)
            if (has_dma && (!DMA_Sx::ValidResource(cfg.dma)
                || !DmaRequest(instance.periph, cfg.pin.channel)))
#else
            if (has_dma && !DmaMatches(instance.periph, cfg.pin.channel, cfg.dma))
#endif
                __builtin_unreachable();
        }
    }
    TIM() = delete;
    TIM(const TIM&) = delete;
    TIM(TIM&&) = delete;
    TIM& operator=(const TIM&) = delete;
    TIM& operator=(TIM&&) = delete;

    /**
     * @brief Configure the base once; Start is explicit.
     * @param freq Update/PWM frequency in Hz, as in the previous periodic API.
     * @param cb Update IRQ callback; nullptr leaves Update IRQ disabled.
     * @param arr Period in CNT ticks, register ARR=arr-1; 0 selects it automatically.
     * Capture/counter use requires an explicit period. The actual CNT clock is
     * GetCounterClock(); freq is not the CNT clock. Integer division is retained:
     * actual Update frequency can differ from freq when divisors are not exact.
     * Repeated setup, invalid period/dividers, a bad counter TIM/ITR route or IRQ registration
     * failure return InitError. Failed initial setup releases its acquisitions.
     */
    SysInitStatus SetUp(uint32_t freq, void (*cb)(void) = nullptr, uint32_t arr = 0);

    /**
     * @brief Whole-timer quadrature reader, on constructor-bound CH1 and CH2.
     * Call instead of SetUp. No DMA, IRQ, software accumulation or auto-start.
     * period is mandatory, ARR=period-1; Count reads CNT without sign conversion.
     * X2_CH1/X2_CH2 count both edges of that input; X4 counts both inputs.
     * The other channel roles and SMCR are unavailable until DeInit.
     * @code
     * constinit TIM encoder(TIM::_3::instance, {TIM::_3::CH1::PA6}, {TIM::_3::CH2::PA7});
     * if (encoder.SetUpEncoder(65536) == SysInitStatus::InitOK) encoder.Start();
     * const uint32_t position = encoder.Count();
     * @endcode
     */
    SysInitStatus SetUpEncoder(uint32_t period, EncoderMode mode = EncoderMode::X4);

    /**
     * @brief Whole-timer finite quadrature generator with a constructor-bound counter.
     * Call instead of SetUp; a/b are distinct bound output channels, without DMA.
     * freq is full A/B cycles/s. period is CNT ticks per half cycle, even and >=4;
     * two toggle channels produce a 90-degree phase displacement. Integer base
     * division is the same as SetUp, so actual frequency can differ from freq.
     * The second TIM must be free. This function configures it over ITR with
     * PSC=1; its CNT counts full A/B cycles, without per-cycle IRQ or RAM copies.
     * Its range is 65535 cycles on 16-bit hardware; all int32_t magnitudes fit
     * a 32-bit counter. cb is optional, fixed here, called from the counter IRQ
     * after stopping both timers. Source TIM IRQ stays disabled.
     * Repeated setup is an error. DeInit releases both acquired timers.
     * @code
     * constinit TIM counter(TIM::_2::instance);
     * constinit TIM generator(TIM::_3::instance, {TIM::_3::CH1::PA6}, {TIM::_3::CH2::PA7}, {}, {}, &counter);
     * if (generator.SetUpQuadrature(TIM::Channel::CH1, TIM::Channel::CH2, 1000, 1000)
     *     == SysInitStatus::InitOK)
     *     generator.GenPulse(100); // Check SysStatus. Count via counter.Count().
     * @endcode
     */
    SysInitStatus SetUpQuadrature(Channel a, Channel b, uint32_t freq,
        uint32_t period, void (*cb)(void) = nullptr);
    /**
     * @brief Main-only start/restart of a finite number of full A/B cycles.
     * Positive: A leads B; negative: B leads A; zero leaves a stopped pair idle.
     * INT32_MIN is supported when the counter range permits it. Busy while either
     * timer runs, Error for an out-of-range magnitude. Use this, not plain Start.
     * Both CNT and prescaler phases reset before arming the counter and starting
     * the generator. Stop stops both; it does not force the A/B outputs LOW.
     * Completion is an IRQ stop: excess edges are possible if service is late.
     * The caller serializes main operations with both timers' IRQs. Do not call
     * this or DeInit from callbacks, or independently reconfigure the counter.
     */
    SysStatus GenPulse(int32_t cycles);

    /**
     * @brief Whole-timer finite PWM2 burst using hardware RCR + OPM.
     * Call instead of SetUp; one bound channel, no DMA. period>=2, high time
     * floor(period/2), initial/final level LOW. freq is the pulse rate.
     * RunPulses accepts 1..RCR_max+1 (0 leaves a stopped timer unchanged).
     * F4: max256; F7: max65536; G0 TIM1: max65536, TIM15/16/17: max256.
     * cb is optional: the hardware stops even without IRQ. No IRQ counting.
     * Do not use SetDuty/SetCCR to change this mode's fixed pulse shape.
     */
    SysInitStatus SetUpPulseBurst(Channel ch, uint32_t freq, uint32_t period,
        void (*cb)(void) = nullptr);
    SysStatus RunPulses(uint32_t count);

    /**
     * @brief Whole-timer hardware counter, clocked by source TIM TRGO over ITR.
     * Call instead of SetUp, explicit period (ARR=period-1), PSC=0. source must
     * be initialized/stopped with TRGO Update already selected. Both objects
     * must live throughout use; no source reconfiguration is performed here.
     * Count reads CNT. No per-pulse IRQ. CH1 is reserved for optional StopAfter/
     * StopAt, which arm a single compare IRQ even with cb=nullptr.
     * cb is fixed here and optional. Start counter before starting source.
     */
    SysInitStatus SetUpCounter(TIM* source, uint32_t period, void (*cb)(void) = nullptr);
    /**
     * Arm while both timers are stopped. Target must be ahead of current CNT
     * without wrapping this period. IRQ disarms itself and stops both counters,
     * then notifies cb. Source can emit extra/partial pulses during IRQ latency;
     * clearing CEN does not force its output LOW. Use RCR for hardware-exact bursts.
     */
    SysStatus StopAfter(uint32_t count);
    SysStatus StopAt(uint32_t target);

    /**
     * @brief PWM-input period/width measurement using CH1+CH2 and slave reset.
     * After ordinary SetUp with an explicit period, before Start. input must
     * be a bound CH1 or CH2; both channels are reserved, no DMA on either.
     * Rising edges capture period/reset CNT, falling edges capture HIGH width.
     * Optional cb is a period-edge notification; there is no IRQ data copy.
     * GetCapture(input) reads period; GetCapture(other channel) reads width.
     * Reads are raw ticks, not an atomic pair. The first cycle, wraps, missing
     * edges and overcapture are interpreted by the caller. SMCR cannot also
     * be assigned to encoder/slave control. Other channels still share this base.
     */
    SysInitStatus SetUpPulseMeasure(Channel input, void (*cb)(void) = nullptr);

    /**
     * @brief Select an ITR slave function once after ordinary SetUp, stopped.
     * Source must already have TRGO configured; it is not changed here.
     * Reset/Gated/Trigger retain PSC/ARR and channel roles. ExternalClock also
     * retains PSC: one CNT step per PSC+1 source edges. GetCounterClock then
     * describes the internal time base, not the incoming event frequency.
     * Only normal TIM TRGO routes are supported; OC-only/remapped ITR and ETR
     * are not selected. Unsupported connections return InitError.
     */
    SysInitStatus SetUpSlave(TIM* source, SlaveMode mode);
    /// Fix OPM before Start, once after ordinary SetUp; it affects the whole base.
    SysInitStatus SetUpOnePulse();

    /**
     * @brief Configure one PWM1 channel while stopped; do not start the counter.
     * Initial ccr is raw ticks, 0..period. Period must fit CCR, so a 16-bit PWM
     * accepts at most 65535 ticks (ARR <= 65534). No forced-active switching.
     * Optional constructor DMA is acquired and registered here, not via AttachDMA.
     * Its callback reports this channel's DMA completion/error; read GetDMAStatus.
     * Failure leaves this channel free and other configured channels untouched.
     */
    SysInitStatus SetUpPWM(Channel ch, uint32_t ccr = 0, void (*dma_callback)(void) = nullptr);

    /**
     * @brief Direct rising-edge capture on a constructor-bound pin; stopped only.
     * SetUp must have received an explicit period. All channels share PSC/ARR.
     * cb is fixed here and optional. A callback enables the source; otherwise
     * capture works without IRQ. IRQ only acknowledges/notifies the event.
     * GetCapture always reads hardware CCR; there is no RAM copy.
     * Capture DMA is not added in this step; pass no DMA for this channel.
     * Values are raw CNT ticks modulo the configured period. The application
     * handles counter wraps, overcapture and interpretation of successive edges.
     *
     * @code
     *   constinit TIM input(TIM::_3::instance, {TIM::_3::CH1::PA6});
     *   input.SetUp(100, nullptr, 10000); // Check status. Target CNT clock: 1 MHz.
     *   input.SetUpCapture(TIM::Channel::CH1, &Edge); // Check status.
     *   input.Start();
     *   // In Edge(), read input.GetCapture(TIM::Channel::CH1).
     * @endcode
     */
    SysInitStatus SetUpCapture(Channel ch, void (*cb)(void) = nullptr);

    /**
     * @brief Reserve a pin-free compare channel once, before Start.
     * compare is 0..ARR. cb != nullptr enables CC IRQ; nullptr configures only
     * hardware compare (for a trigger) and still owns the channel until DeInit.
     * Peripheral IRQ_en may later gate a source; it cannot replace the callback.
     */
    SysInitStatus SetCompareIRQ(Channel ch, uint32_t compare, void (*cb)(void) = nullptr);

    /**
     * @brief Select hardware TRGO once while stopped, without enabling an IRQ.
     * Requires a master-capable TIM. ComparePulse needs configured CH1;
     * OCxRef needs that channel configured as PWM. Direct ADC CCx triggers use
     * SetCompareIRQ(ch, compare, nullptr), not a TRGO remapping. ADC selects its
     * own device-specific source during its migration; TIM stores no ADC EXTSEL.
     * Configure before arming ADC: setup UG must not be interpreted as a sample.
     */
    SysInitStatus SetUpTrigger(Trigger trigger = Trigger::Update);

    /// Change only this timer's interrupt source; registry and NVIC belong to setup.
    /// A generator-owned counter's completion source is managed by GenPulse.
    void IRQ_en(IRQ irq, FunctionalState en);
    void Start() { if (_info) TIM_SET_BIT(TIMx->CR1, TIM_CR1_CEN); }
    void Stop() {
        if (!_info) return;
        TIM_CLEAR_BIT(TIMx->CR1, TIM_CR1_CEN);
        if (_mode == Mode::Quadrature) _counter->Stop();
    }
    void SetEnable(bool on) { if (on) Start(); else Stop(); }
    TIM_TypeDef* GetTIMx() const { return reinterpret_cast<TIM_TypeDef*>(_instance.periph); }
    uint32_t GetPeriod() const { return _info ? TIMx->ARR + 1U : 0U; }
    uint32_t GetCounterClock() const { return _info ? *_info->bus_clk / (TIMx->PSC + 1U) : 0U; }
    uint32_t Count() const { return _info ? TIMx->CNT : 0U; }
    void Reset() { if (_info) TIMx->CNT = 0; }

    /// PWM values only; NotInit/Error/Busy or OK. Percent is clamped at 100.
    SysStatus SetDuty(Channel ch, uint32_t percent);
    SysStatus SetCCR(Channel ch, uint32_t value);
    /// Read hardware CCR directly; the next capture may replace it, with or without IRQ.
    uint32_t GetCapture(Channel ch) const;

    /**
     * @brief Feed PWM CCR preload from a finite buffer, one item per Update.
     * Requires SetUpPWM with constructor-bound DMA. 1..65535 aligned elements:
     * uint16_t/Half_Word for 16-bit timers, uint32_t/Word for 32-bit TIM2/TIM5.
     * The overload must match hardware counter width. Buffer and contents stay
     * unchanged until GetDMAStatus leaves Busy. F7 cleans the source through
     * System; project non-cacheable regions are skipped, F4/G0 helpers are empty.
     * Every channel uses TIMx_CHx, CCDS=1 and its own CCxDE request gate.
     * TC means DMA consumed the buffer, not that the final PWM period ended:
     * CCR preload is applied by a later Update. Keep this distinction when
     * ending a waveform. WS2812B buffers include their LOW reset trailer.
     * Start is separate; to arm several channels, call SendDMA while stopped.
     * Caller masks this TIM's timer/DMA IRQs around main-side request-gate writes;
     * callbacks do not replace that serialization contract.
     */
    SysStatus SendDMA(Channel ch, const uint16_t* data, uint32_t len);
    SysStatus SendDMA(Channel ch, const uint32_t* data, uint32_t len);
    SysStatus GetDMAStatus(Channel ch) const {
        const uint32_t i = static_cast<uint32_t>(ch);
        return i < 4 ? _dma_status[i] : SysStatus::Error;
    }
    bool IsDMABusy(Channel ch) const { return GetDMAStatus(ch) == SysStatus::Busy; }
    bool IsAnyDMABusy() const {
        return IsDMABusy(Channel::CH1) || IsDMABusy(Channel::CH2)
            || IsDMABusy(Channel::CH3) || IsDMABusy(Channel::CH4);
    }

    /**
     * @brief Main-only teardown, without callbacks; constructor bindings survive.
     * Stop timer requests first, stop/release acquired DMA, remove only this
     * object's registrations, reset/disable TIM and clear channel/callback state.
     * Other handlers on shared vectors remain. May wait for DMA stop here;
     * there is no such wait in timer/DMA IRQs. Call before object destruction.
     * Quadrature also tears down the counter acquired by SetUpQuadrature;
     * the constructor's pointer remains fixed for a later fresh setup.
     */
    void DeInit();

private:
    using PeriphInfo = Instance;
    const Instance& _instance;
	TIM_TypeDef* TIMx = nullptr;
    TIM* const _counter;
    const PeriphInfo* _info = nullptr;
    static constexpr ChannelConfig ChannelBinding(uint32_t ch, ChannelConfig c0, ChannelConfig c1,
                                        ChannelConfig c2, ChannelConfig c3) {
        for (const ChannelConfig& cfg : {c0, c1, c2, c3})
            if (cfg.pin.IsValid() && cfg.pin.channel == ch) return cfg;
        return {};
    }
#if defined(STM32G0)
    static constexpr uint32_t DmaRequest(uintptr_t timer, uint32_t ch);
#else
    static constexpr bool DmaMatches(uintptr_t timer, uint32_t ch, DMAReq req);
#endif
    struct ChannelState {
        TIM_PIN pin;
        DMA_Sx dma;
        constexpr ChannelState(uintptr_t timer, ChannelConfig cfg)
            : pin(cfg.pin),
#if defined(STM32G0)
              dma(cfg.dma, cfg.dma ? DmaRequest(timer, cfg.pin.channel) : 0U)
#else
              dma(cfg.dma)
#endif
        { (void)timer; }
    };
    ChannelState _channels[4]; // Pins and owned DMA; no retained copy of request metadata.
    uint8_t _channels_used = 0;
    uint8_t _pwm_channels = 0;
    uint8_t _capture_channels = 0;
    uint8_t _dma_owned = 0;
    enum class Mode : uint8_t { Base, Encoder, Quadrature, Burst, Counter };
    Mode _mode = Mode::Base;
    bool _slave_set = false;
    bool _opm_set = false;
    uint8_t _quadrature_a = 0, _quadrature_b = 0;
    TIM* _master = nullptr; // Only hardware-counter mode needs a retained source.
    volatile bool _dma_error[4]{};
    bool _period_explicit = false;
    bool _trigger_set = false;
    void (*_update_cb)(void) = nullptr;
    void (*_cc_cb[4])(void) = {};
    void (*_dma_cb[4])(void) = {};
    volatile SysStatus _dma_status[4] = {
        SysStatus::NotInit, SysStatus::NotInit, SysStatus::NotInit, SysStatus::NotInit
    };

    // Four fixed adapters: no DMA scan and no run-time channel index in the adapter.
    template<unsigned N> class DmaIRQ final : public IIRQHandler {
    public:
        constexpr explicit DmaIRQ(TIM& tim) : _tim(tim) {}
        void HandleIRQ() override { _tim.HandleDmaIRQ(N); }
    private:
        TIM& _tim;
    };
    DmaIRQ<0> _dma_irq0;
    DmaIRQ<1> _dma_irq1;
    DmaIRQ<2> _dma_irq2;
    DmaIRQ<3> _dma_irq3;
    IIRQHandler* DmaHandler(uint32_t i) {
        IIRQHandler* const handlers[4] = {&_dma_irq0, &_dma_irq1, &_dma_irq2, &_dma_irq3};
        return handlers[i];
    }
    SysInitStatus Init();
    SysInitStatus SetFrequency(uint32_t freq, uint32_t arr);
    SysInitStatus SetUpDMA(uint32_t ch);
    struct ITRInfo { TIM_TypeDef* slave; TIM_TypeDef* source[4]; };
    static const ITRInfo itr_table[];
    static int FindITR(TIM_TypeDef* source, TIM_TypeDef* slave);
    bool CanUseSource(const TIM* source) const;
    SysStatus StartDMA(Channel ch, const void* data, uint32_t len, DMA_Sx::SIZE width);
    bool CanSetUpChannel(uint32_t ch) const {
        return _info && _mode == Mode::Base && ch < _info->channel_count && !(TIMx->CR1 & TIM_CR1_CEN)
            && !(_channels_used & (1U << ch));
    }
    volatile uint32_t* ccr(uint32_t ch) const {
        return reinterpret_cast<volatile uint32_t*>(reinterpret_cast<uintptr_t>(&TIMx->CCR1)
            + ch * sizeof(uint32_t));
    }
    void HandleIRQ() override final;
    void HandleDmaIRQ(uint32_t ch);
};

#define TIM_DMA_DEFS
#include "tim_defs.hpp"
#undef TIM_DMA_DEFS

// Previous mode API, retained only for reference; not compiled.
// // ---------------------------------------------------------------------------
// // TIM_PeriodicIRQ — periodic update interrupt
// // ---------------------------------------------------------------------------
//
// /**
//  * @brief Fires a callback at a fixed rate via the timer's Update interrupt —
//  * plus, on timers that have compare channels, any number of additional
//  * pin-free callbacks at specific counts within that same period.
//  *
//  * The base SetUp() needs nothing but PSC/ARR sized for @p freq and the
//  * Update IRQ wired to a callback — no GPIO pins, no channels required.
//  * SetCompareIRQ() is optional extra scheduling on top of that, for timers
//  * that implement at least one CC channel (see PeriphInfo::channel_count —
//  * TIM6/TIM7 on F4 are pure "basic" timers with none at all).
//  *
//  * @code
//  *   void Tick() { ... }        // called from the Update ISR
//  *   void HalfTick() { ... }    // called from the CC1 ISR, mid-period
//  *
//  *   TIM_PeriodicIRQ tick(TIM3);
//  *   tick.SetUp(1000, &Tick);        // 1 kHz Update — SetUp() picks PSC/ARR itself
//  *   tick.SetCompareIRQ(TIM::Channel::CH1, tick.GetPeriod() / 2, &HalfTick);
//  *   tick.Start();                   // SetUp() does not start it
//  * @endcode
//  */
// class TIM_PeriodicIRQ : public TIM
// {
// public:
// 	explicit TIM_PeriodicIRQ(TIM_TypeDef* timx) : TIM(timx) {}
//
// 	/**
// 	 * @brief Configures the timer for periodic update interrupts.
// 	 * Does NOT call Start() — the caller must do so explicitly.
// 	 *
// 	 * @param freq  Interrupt rate in Hz.
// 	 * @param arr   Desired period, in ticks (the counter counts 0..arr-1;
// 	 *              register ARR ends up as arr-1) — same convention as
// 	 *              GetPeriod().
// 	 * @param cb    Callback fixed by this setup (nullptr = no callback).
// 	 * @return InitError for repeat setup, invalid frequency/period or registration
// 	 *         failure. Failed frequency setup is rolled back; Start is explicit.
// 	 */
// 	SysInitStatus SetUp(uint32_t freq, void (*cb)(void) = nullptr, uint32_t arr = 0);
//
// 	/**
// 	 * @brief Changes the interrupt rate, at rest or while running — same
// 	 * PSC/ARR computation as SetUp(), without touching DIER/callbacks.
// 	 *
// 	 * Re-exposed as-is from the (protected) base: unlike TIM_StepGenerator
// 	 * or TIM_PWM, plain periodic mode has no extra state (no CCR, no OPM)
// 	 * that a frequency change could leave stale, so there's nothing for a
// 	 * wrapper here to do beyond what TIM::SetFrequency() already does.
// 	 *
// 	 * @note If you've called SetCompareIRQ() on any channel, its @p compare
// 	 * value keeps its old raw-tick meaning against the new period — check
// 	 * it's still < GetPeriod() after changing frequency, same caveat as
// 	 * SetCompareIRQ() has against the period from SetUp().
// 	 */
// 	using TIM::SetFrequency;
//
// 	/**
// 	 * @brief Timer period in ticks (ARR+1), as SetUp() actually configured it.
// 	 *
// 	 * SetUp() picks PSC/ARR itself to maximise resolution for the requested
// 	 * frequency, so there is no way to predict ARR from @p freq alone — read
// 	 * it back through here (e.g. to compute a SetCompareIRQ() value as a
// 	 * fraction of the period) instead of guessing or reaching into TIMx->ARR
// 	 * directly.
// 	 */
// 	inline uint32_t GetPeriod() const { return TIMx->ARR + 1; }
//
// 	/**
// 	 * @brief Schedules an additional callback at a specific counter value on
// 	 * one of this timer's compare channels, alongside the Update callback —
// 	 * without touching any GPIO pin.
// 	 *
// 	 * The channel's output compare is left in Frozen mode (OCxM stays at its
// 	 * reset value of 0): CCxIF still sets purely from the CNT-vs-CCRx match,
// 	 * independently of OCxM/CCxE, so no pin ever needs to be configured.
// 	 * Several channels can each carry their own compare value/callback,
// 	 * letting one timer raise multiple independent scheduled events per
// 	 * period instead of needing one timer each.
// 	 *
// 	 * @param ch       Channel to use for this interrupt.
// 	 * @param compare  CNT value to trigger at; must be < GetPeriod() (i.e.
// 	 *                 <= the current ARR) or the callback will never fire —
// 	 *                 see GetPeriod() for why this can't be predicted from
// 	 *                 the frequency passed to SetUp().
// 	 * @param cb       Bound during initialization and called from the CC ISR
// 	 *                 (nullptr leaves the interrupt/compare set without a callback —
// 	 *                 pass IRQ_en(<that channel's IRQ>, DISABLE) to fully
// 	 *                 turn it off).
// 	 * Call during initialization, before Start. A configured channel cannot be
// 	 * initialized again until DeInit, including when its callback is null.
// 	 * @return InitError while running, for an already configured channel, if SetUp()
// 	 *         has not been called, if @p compare is
// 	 *         out of range, or if this timer doesn't implement channel
// 	 *         @p ch at all (see PeriphInfo::channel_count).
// 	 */
// 	SysInitStatus SetCompareIRQ(Channel ch, uint32_t compare, void (*cb)(void));
// };
//
// // ===========================================================================
// // Generators — produce an output signal
// // ===========================================================================
//
// // ---------------------------------------------------------------------------
// // TIM_PWM — PWM output on 1–4 channels
// // ---------------------------------------------------------------------------
//
// /** @brief PWM output driver, up to four channels with optional DMA-driven CCR updates. */
// class TIM_PWM : public TIM
// {
// public:
// 	/**
// 	 * @brief Constructs a PWM driver with up to four output channels.
// 	 *
// 	 * Pins are passed as TIM::_N::CHx::Pyz constants from tim_defs.hpp.
// 	 * Each pin encodes its own channel index, so they can be given in any
// 	 * order and channels can be skipped without counting empty arguments.
// 	 *
// 	 * @param timx  Timer peripheral (TIM1, TIM3, …).
// 	 * @param p0–p3 Channel pins from the TIM::_N::CHx tables; omit unused ones.
// 	 *
// 	 * Example:
// 	 * @code
// 	 *   // WS2812B on TIM1 CH1+CH2+CH3:
// 	 *   TIM_PWM ws_tim(TIM1, TIM::_1::CH1::PA8,
// 	 *                        TIM::_1::CH2::PA9,
// 	 *                        TIM::_1::CH3::PA10);
// 	 *
// 	 *   // Anode dimmer on TIM3 CH3+CH4 only (no empty PIN{} needed):
// 	 *   TIM_PWM Anode_tim(TIM3, TIM::_3::CH3::PB0,
// 	 *                           TIM::_3::CH4::PB1);
// 	 * @endcode
// 	 */
// 	TIM_PWM(TIM_TypeDef* timx,
// 	        TIM_PIN p0 = {}, TIM_PIN p1 = {},
// 	        TIM_PIN p2 = {}, TIM_PIN p3 = {}) : TIM(timx), _ch{}
// 	{
// 		// Validate and route each pin to its declared channel slot.
// 		for (const TIM_PIN& p : {p0, p1, p2, p3}) {
// 			if (!p.IsValid()) continue;
// 			if (p.tim_base && p.tim_base != reinterpret_cast<uint32_t>(timx))
// 				System::DebugTrap("TIM_PWM: pin belongs to wrong timer");
// 			_ch[p.channel] = p;
// 		}
// 	}
//
// 	/**
// 	 * @brief Configures PWM on all valid pins and starts the timer.
// 	 *
// 	 * @param freq  PWM frequency in Hz.
// 	 * @param arr   Auto-reload value; PSC is derived automatically.
// 	 *              SetDuty() scales percent by the actual arr, so any value
// 	 *              works; default 1000 gives 0.1 % resolution.
// 	 *              For protocols like WS2812B pass an explicit value
// 	 *              (e.g. arr = 79 at 64 MHz / 800 kHz) and use SetCCR().
// 	 */
// 	SysInitStatus SetUp(uint32_t freq, uint32_t arr = 1000);
//
// 	/**
// 	 * @brief Changes the PWM frequency, at rest or while running — without
// 	 * touching channel mode, pins or BDTR the way calling SetUp() again would.
// 	 *
// 	 * @param freq  Target frequency in Hz.
// 	 * @param arr   Desired period in ticks; 0 (the default) keeps whatever
// 	 *              period is currently configured (from SetUp() or the last
// 	 *              call here) and only recomputes PSC — pass an explicit
// 	 *              value to also change the period/resolution.
// 	 * @return InitError if SetUp() hasn't been called yet, or if the
// 	 *         requested freq/arr combination doesn't fit.
// 	 *
// 	 * @note Existing CCR values keep their raw-tick meaning: if you drive
// 	 * duty via SetDuty() (percent of the period), call it again after this
// 	 * to get the same percentage against the new period — SetCCR() users
// 	 * already work in raw ticks and aren't otherwise affected.
// 	 * @note PSC's shadow register only takes effect on the next Update
// 	 * event, while ARR (no preload enabled here) applies immediately — so
// 	 * for up to one period after calling this while running, PSC and ARR
// 	 * can briefly be mismatched. Harmless for most uses; avoid this if a
// 	 * glitch-free change mid-run matters.
// 	 */
// 	inline SysInitStatus SetFrequency(uint32_t freq, uint32_t arr = 0) {
// 		return TIM::SetFrequency(freq, arr ? arr : (TIMx->ARR + 1));
// 	}
//
// 	/** @brief Sets duty cycle in percent (0–100). Clamps at 100. */
// 	void SetDuty(Channel ch, uint32_t percent);
//
// 	/** @brief Writes CCRx directly (raw value). */
// 	void SetCCR(Channel ch, uint32_t val);
//
// 	/**
// 	 * @brief Attaches a DMA channel to a PWM output channel.
// 	 *
// 	 * Configures the DMA stream to write 16-bit CCR values from a user buffer
// 	 * to the CCRx register of @p ch on every timer Update event.  Registers
// 	 * this object in IRQ_Registry for the DMA TC interrupt.
// 	 *
// 	 * Multiple channels can be attached independently; each gets its own DMA
// 	 * stream.  Must be called after SetUp().
// 	 *
// 	 * @param dma  DMA stream (pre-constructed, not yet SetUp'd).
// 	 * @param ch   Timer output channel whose CCR this DMA will drive.
// 	 */
// 	void AttachDMA(DMA_Sx* dma, Channel ch);
//
// 	/**
// 	 * @brief Starts a DMA transfer on one PWM channel.
// 	 *
// 	 * Each element of @p data is written to CCRx on the next timer Update
// 	 * event.  All attached channels share the same TIM_DIER_UDE trigger; call
// 	 * SendDMA() for every channel you want active before the next update fires.
// 	 * Returns Busy if this channel's transfer is already in progress.
// 	 *
// 	 * @param ch    Output channel to drive.
// 	 * @param data  Array of 16-bit CCR values (must stay valid until complete).
// 	 * @param len   Number of values (= PWM periods = WS2812B bits).
// 	 */
// 	SysStatus SendDMA(Channel ch, const uint16_t* data, uint32_t len);
//
// 	/** @brief True if the specified channel's DMA is still running. */
// 	inline bool IsDMABusy(Channel ch) const {
// 		return (_dma_busy >> static_cast<uint8_t>(ch)) & 1u;
// 	}
// 	/** @brief True if any channel's DMA is still running. */
// 	inline bool IsAnyDMABusy() const { return _dma_busy != 0; }
//
// 	/** @brief Sets the callback invoked once all in-flight DMA channels finish. */
// 	inline void SetDMACallback(void (*cb)(void)) { _dma_cb = cb; }
//
// 	/**
// 	 * @brief Handle bound to one specific output channel of the TIM_PWM that
// 	 * created it — remembers both which channel and which TIM_PWM instance,
// 	 * so (unlike passing a bare Channel around) it can never end up applied
// 	 * to the wrong PWM object. Get one via GetOutput(); trivially copyable
// 	 * and cheap to keep around (just a pointer + an enum), so it's fine to
// 	 * store one per channel for as long as you need to keep calling SetDuty().
// 	 */
// 	class Output
// 	{
// 	public:
// 		/** @brief Sets duty cycle in percent (0–100) on this channel. Clamps at 100. */
// 		inline void SetDuty(uint32_t percent) { _pwm->SetDuty(_ch, percent); }
// 		/** @brief Writes CCRx directly (raw value) on this channel. */
// 		inline void SetCCR(uint32_t val) { _pwm->SetCCR(_ch, val); }
// 		/** @brief True if this channel's DMA is still running. */
// 		inline bool IsDMABusy() const { return _pwm->IsDMABusy(_ch); }
// 		/** @brief Attaches a DMA channel to this output. */
// 		inline void AttachDMA(DMA_Sx* dma) { _pwm->AttachDMA(dma, _ch); }
// 		/** @brief Starts a DMA transfer on this channel. */
// 		inline SysStatus SendDMA(const uint16_t* data, uint32_t len) { return _pwm->SendDMA(_ch, data, len); }
//
// 	private:
// 		friend class TIM_PWM;
// 		Output(TIM_PWM* pwm, Channel ch) : _pwm(pwm), _ch(ch) {}
//
// 		TIM_PWM* _pwm;
// 		Channel  _ch;
// 	};
//
// 	/**
// 	 * @brief Returns a handle bound to one of this TIM_PWM's output channels,
// 	 * so calling code doesn't have to keep track of a bare Channel value (and
// 	 * risk applying it to the wrong TIM_PWM instance) to call
// 	 * SetDuty()/SetCCR()/etc. on it repeatedly.
// 	 *
// 	 * @code
// 	 *   TIM_PWM pwm(TIM3, TIM::_3::CH1::PA6, TIM::_3::CH2::PA7);
// 	 *   pwm.SetUp(1000);
// 	 *
// 	 *   TIM_PWM::Output red = pwm.GetOutput(TIM::_3::CH2::PA7);
// 	 *   red.SetDuty(50);              // no channel to mix up from here on
// 	 *
// 	 *   pwm.GetOutput(TIM::_3::CH1::PA6).SetDuty(10);  // one-shot, no need to store it
// 	 * @endcode
// 	 *
// 	 * @param pin  The same TIM::_N::CHx::Pyz constant passed to the
// 	 *             constructor for this channel — reused rather than a bare
// 	 *             Channel so the channel is never re-typed by hand.
// 	 * @return A handle for @p pin's channel. Traps if that channel wasn't
// 	 *         actually configured in the constructor (nothing valid at
// 	 *         _ch[pin.channel]).
// 	 */
// 	inline Output GetOutput(TIM_PIN pin) {
// 		if (pin.channel >= 4 || !_ch[pin.channel].IsValid())
// 			System::DebugTrap("TIM_PWM::GetOutput: channel was not configured in the constructor");
// 		return Output(this, static_cast<Channel>(pin.channel));
// 	}
//
// private:
// 	TIM_PIN  _ch[4];
// 	DMA_Sx*  _dma[4]        = {};    ///< One DMA stream per channel (null = unused).
// 	uint8_t  _dma_busy      = 0;    ///< Bitmask: bit i set while _dma[i] is running.
// 	void   (*_dma_cb)(void) = nullptr;
//
// 	void HandleIRQ() override;
// };
//
// // ---------------------------------------------------------------------------
// // TIM_EncoderGenerator — quadrature step-pulse generator for stepper motors
// // ---------------------------------------------------------------------------
//
// /** @brief Generates a fixed-count quadrature (A/B) pulse burst for stepper motors. */
// class TIM_EncoderGenerator : public TIM
// {
// public:
// 	/**
// 	 * @brief Constructs a quadrature step-pulse generator on two channels.
// 	 * @param a  Step pulse channel (A phase).
// 	 * @param b  Direction channel  (B phase, 90° shifted).
// 	 */
// 	TIM_EncoderGenerator(TIM_TypeDef* timx, Line a, Line b) :
// 		TIM(timx), _a(a), _b(b)
// 	{
// 		if ((a.pin.tim_base && a.pin.tim_base != reinterpret_cast<uint32_t>(timx)) ||
// 		    (b.pin.tim_base && b.pin.tim_base != reinterpret_cast<uint32_t>(timx)))
// 			System::DebugTrap("TIM_EncoderGenerator: pin belongs to wrong timer");
// 	}
//
// 	/**
// 	 * @brief Configures both channels for toggle-mode quadrature output.
// 	 * @param freq       Pulse frequency in Hz.
// 	 * @param period     ARR value (determines CCR resolution).
// 	 * @param ch1_width  CCR value for channel A.
// 	 * @param ch2_width  CCR value for channel B.
// 	 */
// 	SysInitStatus SetUp(uint32_t freq, uint32_t period,
// 						uint32_t ch1_width, uint32_t ch2_width);
//
// 	/**
// 	 * @brief Generates @p pulses steps.  Positive = forward, negative = reverse.
// 	 * Starts the timer automatically; stops after the last pulse.
// 	 */
// 	void GenPulse(int32_t pulses);
//
// 	/** @brief Callback invoked when the last pulse has been generated. */
// 	inline void SetCompleteCallback(void (*cb)(void)) { _on_complete = cb; }
//
// private:
// 	Line             _a, _b;                       ///< A/B phase pins + channels.
// 	volatile int32_t _pulses     = 0;               ///< Pulses remaining in the current GenPulse() burst.
// 	void           (*_on_complete)(void) = nullptr; ///< Called from HandleIRQ() when _pulses reaches 0.
//
// 	void HandleIRQ() override;
// };
//
// // ---------------------------------------------------------------------------
// // TIM_StepGenerator — continuous 50%-duty STEP pulse train (STEP/DIR drivers)
// // ---------------------------------------------------------------------------
//
// /**
//  * @brief Generates the STEP pulse train for a STEP/DIR stepper driver.
//  *
//  * DIR is deliberately not handled here — it is a plain GPIO toggled by the
//  * caller (e.g. Motor::Driver::set_direction), independent of this timer.
//  *
//  * Uses PWM mode 1 with CCR fixed at ARR/2 (not toggle mode): one full ARR
//  * period is exactly one STEP pulse (HIGH first half, LOW second half), so
//  * the counter's own Update-event rate is 1:1 with the step rate — no
//  * doubling/halving anywhere (SetFrequency(freq) directly, RCR = steps-1,
//  * one Update IRQ per step). A pin-toggle scheme would need 2 Update events
//  * per pulse instead, complicating every one of those without giving up
//  * anything here: PWM mode's 50% duty comes from ARR/2 exactly, same
//  * precision toggle mode would have given.
//  *
//  * Two ways to know how many steps have gone out, chosen via SetUp()'s
//  * @p count_in_isr:
//  *  - Software (count_in_isr = true): the Update IRQ fires once per step,
//  *    GetStepCount() reads a RAM counter. Simplest, costs one ISR per output
//  *    pulse — fine up to a few tens of kHz of step rate.
//  *  - Hardware (count_in_isr = false): no IRQ at all. Pair this timer with a
//  *    TIM_HWCounter chained to its TRGO (see that class) and read steps
//  *    straight from TIM_HWCounter::Count() (1:1, no /2 needed) instead —
//  *    needed at step rates where an ISR per pulse would load the CPU
//  *    noticeably.
//  *
//  * @code
//  *   // Software-counted, up to ~20 kHz steps:
//  *   TIM_StepGenerator step(TIM3, TIM::_3::CH1::PA6);
//  *   step.SetUp(1000, true);   // 1 kHz, counted via IRQ
//  *   step.Start();
//  *   uint32_t done = step.GetStepCount();
//  *
//  *   // Hardware-counted, high step rates, zero ISR overhead:
//  *   TIM_StepGenerator step(TIM1, TIM::_1::CH1::PA8);
//  *   step.SetUp(200000, false);       // 200 kHz, no IRQ
//  *   TIM_HWCounter counter(TIM3);
//  *   counter.SetUp(step);              // TIM3 counts TIM1's TRGO via ITR
//  *   step.Start();
//  *   uint32_t done = counter.Count();
//  * @endcode
//  *
//  * @note Position moves: for a fixed target at constant or ramped speed, use
//  * RunSteps(N) instead of Start() — it stops itself in hardware after exactly
//  * N pulses (needs an advanced/semi-advanced timer, see RunSteps() docs) and
//  * tolerates SetStepFrequency() ramps mid-move. Deciding *what* frequency to
//  * ramp through at each moment (accel/decel profile) is not this class's job —
//  * that belongs to Motor/orchestration, which knows the motion limits.
//  */
// class TIM_StepGenerator : public TIM
// {
// public:
// 	/**
// 	 * @brief Constructs a STEP-pulse generator on one channel.
// 	 * @param step  STEP pulse pin + channel. DIR is a plain GPIO, not owned here.
// 	 */
// 	TIM_StepGenerator(TIM_TypeDef* timx, Line step) : TIM(timx), _step(step)
// 	{
// 		if (step.pin.tim_base && step.pin.tim_base != reinterpret_cast<uint32_t>(timx))
// 			System::DebugTrap("TIM_StepGenerator: pin belongs to wrong timer");
// 	}
//
// 	/**
// 	 * @brief Configures the STEP channel for a free-running square wave.
// 	 * Does NOT start the timer — call Start()/Stop() (inherited) explicitly.
// 	 *
// 	 * @param freq          Initial step frequency in Hz (pulses/sec); 0 is invalid.
// 	 * @param count_in_isr  true: enable the Update IRQ and maintain GetStepCount()
// 	 *                      in software. false: no IRQ is enabled at all — use a
// 	 *                      TIM_HWCounter chained to this timer's TRGO instead
// 	 *                      (GetStepCount() then stays at 0, it isn't updated).
// 	 */
// 	SysInitStatus SetUp(bool count_in_isr = false);// SysInitStatus SetUp(uint32_t freq, bool count_in_isr = true);
//
// 	/**
// 	 * @brief Changes the step frequency, at rest or while running.
// 	 * Recomputes PSC/ARR from scratch — call at ramp-update rate, not per-pulse.
// 	 * Also cancels any pending RunSteps() one-shot, returning to free-running mode.
// 	 */
// 	SysInitStatus SetStepFrequency(uint32_t freq);
//
// 	/**
// 	 * @brief Generates exactly @p steps pulses, then stops itself in hardware
// 	 * (Repetition Counter + One Pulse Mode) — no ISR needed to detect
// 	 * completion, and correct even if SetStepFrequency() changes the speed
// 	 * mid-run (RCR counts elapsed periods, not elapsed time, so a ramp between
// 	 * calls doesn't throw off the count).
// 	 *
// 	 * Requires an advanced/semi-advanced timer with a Repetition Counter
// 	 * (TIM1/TIM8, TIM16/TIM17 — wherever PeriphInfo::has_bdtr is true);
// 	 * returns InitError otherwise, or if @p steps doesn't fit this timer's
// 	 * RCR width.
// 	 *
// 	 * @note The RCR arithmetic below (steps-1 Update events) has not been
// 	 * confirmed on real hardware — verify the actual pulse count on a scope
// 	 * on first bring-up before relying on it for position accuracy.
// 	 *
// 	 * @param steps  Number of STEP pulses to generate; 0 is invalid.
// 	 *
// 	 * @note No RCR on this timer (e.g. STEP generated on TIM3)? Use
// 	 * TIM_HWCounter::StopAfter() instead — pair a second timer chained via ITR
// 	 * and let its own down-count-to-zero stop this one. One IRQ for the whole move
// 	 * (on completion), not zero, but still nowhere near an IRQ per step.
// 	 */
// 	SysInitStatus RunSteps(uint32_t steps);
//
// 	/** @brief True while the timer is generating pulses (continuous or a RunSteps() burst). */
// 	inline bool IsRunning() const { return (TIMx->CR1 & TIM_CR1_CEN) != 0; }
//
// 	/** @brief Steps (rising edges) generated since SetUp() or the last ResetStepCount(). Only updates when SetUp() was called with count_in_isr = true; else stays 0 — see TIM_HWCounter. */
// 	inline uint32_t GetStepCount() const { return _step_count; }
// 	/** @brief Resets GetStepCount() back to 0. */
// 	inline void     ResetStepCount()     { _step_count = 0; }
//
// private:
// 	Line              _step;
// 	volatile uint32_t _step_count = 0;  ///< One Update event = one step. Unused when count_in_isr = false.
//
// 	void HandleIRQ() override;
// };
//
// // ===========================================================================
// // Measurers — read/decode an input signal
// // ===========================================================================
//
// // ---------------------------------------------------------------------------
// // TIM_InputCapture — single-channel input capture with callback
// // ---------------------------------------------------------------------------
//
// /**
//  * @brief Captures CCRx on a single channel's input edge, optionally via callback.
//  *
//  * @code
//  *   // Poll-based: read the last capture whenever convenient.
//  *   TIM_InputCapture cap(TIM3, TIM::_3::CH1::PA6);
//  *   cap.SetUp(1000000);                 // max expected input frequency, no callback
//  *   uint32_t ticks = cap.GetCapture();
//  *
//  *   // Callback-based: get notified from the CCx ISR on every edge instead.
//  *   TIM_InputCapture cap(TIM3, TIM::_3::CH1::PA6);
//  *   cap.SetUp(1000000, &OnEdgeCaptured);
//  * @endcode
//  */
// class TIM_InputCapture : public TIM
// {
// public:
// 	/**
// 	 * @brief Constructs an input-capture driver on one channel.
// 	 * @param input  Pin + channel to capture on.
// 	 */
// 	TIM_InputCapture(TIM_TypeDef* timx, Line input) : TIM(timx), _input(input)
// 	{
// 		if (input.pin.tim_base && input.pin.tim_base != reinterpret_cast<uint32_t>(timx))
// 			System::DebugTrap("TIM_InputCapture: pin belongs to wrong timer");
// 	}
//
// 	/**
// 	 * @brief Configures the capture channel and enables its CCx interrupt.
// 	 *
// 	 * @param max_freq  Maximum expected input frequency (sets PSC).
// 	 * @param cb        Called after each capture (nullptr = poll via GetCapture).
// 	 */
// 	SysInitStatus SetUp(uint32_t max_freq, void (*cb)(void) = nullptr);
//
// 	/** @brief Returns the last captured CCR value. */
// 	inline uint32_t GetCapture() const { return _capture; }
//
// private:
// 	Line     _input;
// 	uint32_t _capture = 0;
//
// 	void HandleIRQ() override;
// };
//
// // ---------------------------------------------------------------------------
// // TIM_PulseMeasure — PWM input (period + width) via reset mode
// // ---------------------------------------------------------------------------
//
// /** @brief Measures an input PWM signal's period and pulse width via hardware reset mode. */
// class TIM_PulseMeasure : public TIM
// {
// public:
// 	/**
// 	 * @brief Constructs a PWM-input (period + width) measurement driver.
// 	 * @param input  Pin + channel; channel must be CH1 or CH2.
// 	 */
// 	TIM_PulseMeasure(TIM_TypeDef* timx, Line input) :
// 		TIM(timx), _input(input)
// 	{
// 		if (input.pin.tim_base && input.pin.tim_base != reinterpret_cast<uint32_t>(timx))
// 			System::DebugTrap("TIM_PulseMeasure: pin belongs to wrong timer");
// 	}
//
// 	/** @brief Configures PWM-input mode and starts the timer. */
// 	SysInitStatus SetUp(uint32_t max_freq);
//
// 	/** @brief Returns the last measured pulse width (ticks). */
// 	inline uint32_t GetWidth()  const { return TIMx->CCR1; }
// 	/** @brief Returns the last measured signal period (ticks). */
// 	inline uint32_t GetPeriod() const { return TIMx->CCR2; }
// 	/** @brief Clears the last captured width/period. */
// 	inline void     Clear()           { TIMx->CCR1 = 0; TIMx->CCR2 = 0; }
//
// private:
// 	Line _input;   ///< Capture input pin + channel.
// };
//
// // ---------------------------------------------------------------------------
// // TIM_EncoderReader — hardware quadrature decoder for reading an encoder
// // ---------------------------------------------------------------------------
//
// /**
//  * @brief Reads an incremental (quadrature) encoder entirely in hardware —
//  * counterpart to TIM_EncoderGenerator, which fakes one for testing.
//  *
//  * Puts the timer in Encoder Interface mode (SMCR.SMS = 001/010/011): CNT
//  * increments/decrements on CH1/CH2 edges according to the real quadrature
//  * relationship between the two phases, with direction (CR1.DIR) set by
//  * hardware from the actual signal — no ISR, no CPU cycles, at any speed the
//  * encoder can produce.
//  *
//  * Requires CH1 and CH2 specifically (phase A/B) — Encoder Interface mode is
//  * hardwired to TI1/TI2 in every STM32 timer that has it, the same way
//  * External Clock Mode 1 (see TIM_HWCounter) is hardwired to ITRx; CH3/CH4
//  * can't be used for this regardless of which pins are passed in, so the
//  * constructor traps rather than silently mapping the wrong channels.
//  *
//  * @code
//  *   TIM_EncoderReader enc(TIM3, TIM::_3::CH1::PA6, TIM::_3::CH2::PA7);
//  *   enc.SetUp();                          // Mode::X4 by default: count every edge
//  *   enc.Start();
//  *
//  *   uint32_t pos = enc.Count();
//  *   bool     rev = enc.IsCountingDown();  // hardware-determined, read-only
//  * @endcode
//  */
// class TIM_EncoderReader : public TIM
// {
// public:
// 	/** @brief Selects how many quadrature edges count per detent — matches SMCR.SMS's encoder submodes. */
// 	enum class Mode : uint32_t {
// 		X2_CH1 = 0b01,  ///< Counts edges on CH1 only; direction read from CH2's level.
// 		X2_CH2 = 0b10,  ///< Counts edges on CH2 only; direction read from CH1's level.
// 		X4     = 0b11,  ///< Counts edges on both channels — 4x resolution, the common default.
// 	};
//
// 	/**
// 	 * @brief Constructs an encoder reader on the CH1/CH2 pair.
// 	 * @param a  Phase A pin — must be on CH1 (Encoder Interface mode is
// 	 *           hardwired to TI1/TI2, i.e. CH1/CH2 — traps otherwise).
// 	 * @param b  Phase B pin — must be on CH2.
// 	 */
// 	TIM_EncoderReader(TIM_TypeDef* timx, Line a, Line b) :
// 		TIM(timx), _a(a), _b(b)
// 	{
// 		if ((a.pin.tim_base && a.pin.tim_base != reinterpret_cast<uint32_t>(timx)) ||
// 		    (b.pin.tim_base && b.pin.tim_base != reinterpret_cast<uint32_t>(timx)))
// 			System::DebugTrap("TIM_EncoderReader: pin belongs to wrong timer");
// 		if (a.GetChannel() != Channel::CH1 || b.GetChannel() != Channel::CH2)
// 			System::DebugTrap("TIM_EncoderReader: Encoder Interface mode requires CH1 (phase A) and CH2 (phase B) specifically");
// 	}
//
// 	/**
// 	 * @brief Configures Encoder Interface mode and starts counting.
// 	 * @param mode  Which edges to count; Mode::X4 (both channels) by default.
// 	 */
// 	SysInitStatus SetUp(Mode mode = Mode::X4);
//
// 	/**
// 	 * @brief True if hardware is currently counting down — the real
// 	 * quadrature direction as decoded from the two phases. Read-only: unlike
// 	 * TIM_HWCounter::SetDirection(), nothing here is a software command,
// 	 * hardware alone decides it in this mode.
// 	 */
// 	inline bool IsCountingDown() const { return (TIMx->CR1 & TIM_CR1_DIR) != 0; }
//
// 	/** @brief Raw position — increments/decrements per Mode, direction set by hardware. */
// 	inline uint32_t Count() const { return TIMx->CNT; }
// 	/** @brief Resets the position (CNT) back to 0. */
// 	inline void     Reset()       { TIMx->CNT = 0; }
//
// private:
// 	Line _a, _b;   ///< Phase A (CH1) / Phase B (CH2) pins.
// };
//
// // ---------------------------------------------------------------------------
// // TIM_HWCounter — free-running hardware pulse counter, no interrupts/CPU
// // ---------------------------------------------------------------------------
//
// /**
//  * @brief Counts another timer's pulses entirely in hardware.
//  *
//  * Chains this timer's counter to a "master" timer's TRGO (Update event) via
//  * the internal trigger (ITR) bus, in External Clock Mode 1: CNT ticks once
//  * per master Update event with no ISR and no CPU cycles spent, at any pulse
//  * rate the master can generate. Use this instead of TIM_StepGenerator's
//  * built-in software counter when the step rate is high enough that an
//  * interrupt per toggle would be a real CPU cost.
//  *
//  * SetUp() chains once, binds the completion callback for this counter's
//  * whole lifetime, and enables the CC1 auto-stop unconditionally (works even
//  * with @p on_complete = nullptr — same "nullptr = no callback, still
//  * functions" convention as TIM_PeriodicIRQ::SetCompareIRQ()). Everything
//  * after that is retargetable on the fly, without redoing the ITR chain or
//  * touching the callback:
//  *  - SetDirection() is a plain, independent command (e.g. reflecting which
//  *    way a motion command is asking to move) — never touches any target.
//  *  - StopAfter(ticks) arms a CC1 compare-match at "current CNT + ticks" *in
//  *    whatever direction is currently set*; StopAT(ticks) arms one at the
//  *    absolute value @p ticks instead, regardless of current CNT/direction.
//  *    Both can be called at any time (running or not) to retarget an
//  *    already-moving counter, e.g. when a new move command arrives.
//  *  - CancelStopAfter() disarms the interrupt; a later StopAfter()/StopAT()
//  *    call re-arms it, no need to call SetUp() again.
//  *
//  * Two ways to know when a move is done:
//  *  - Free-running: just SetUp() + Start(), poll Count() yourself — the CC1
//  *    auto-stop only fires once a StopAfter()/StopAT() target is armed; pair
//  *    with TIM_StepGenerator::RunSteps() on the master if it has an RCR, or
//  *    just Stop() the master yourself once Count() reaches your target.
//  *  - StopAfter()/StopAT(): one IRQ for the whole move (on completion, via
//  *    CH1's compare match — CH1 drives no pin, see TIM_PeriodicIRQ::
//  *    SetCompareIRQ() for the same pin-free technique), not one per step.
//  *    Use this for a master timer without a Repetition Counter, where
//  *    TIM_StepGenerator::RunSteps() isn't available.
//  *
//  * @p ticks/Count() are in raw master-Update-event units — for a
//  * TIM_StepGenerator master (PWM mode, one Update event per step), that's
//  * already 1:1 with steps, no conversion needed at the call site. A
//  * toggle-mode master would need one (2 Update events per pulse), but
//  * TIM_HWCounter itself makes no assumption either way about what the
//  * master's pulses represent.
//  *
//  * Requires a verified (master, slave) route in TIM::itr_table — currently
//  * only STM32F4/F7 (TIM1/2/3/4/5/8) are populated; on STM32G0 both methods
//  * return InitError until the table entries are confirmed against RM0444
//  * (see the TODO in tim_defs.hpp).
//  *
//  * @code
//  *   // Free-running, poll-based:
//  *   TIM_StepGenerator step(TIM1, TIM::_1::CH1::PA8);
//  *   step.SetUp(200000, false);   // no IRQ — see TIM_StepGenerator docs above
//  *
//  *   TIM_HWCounter counter(TIM3); // TIM3 free-run, unrelated to STEP's own pins
//  *   if (counter.SetUp(step) != SysInitStatus::InitOK) return; // no ITR route for this pair
//  *
//  *   step.Start();
//  *   uint32_t steps_done = counter.Count();  // 1:1 with steps (PWM-mode master)
//  *
//  *   // Auto-stop on a master without RCR (e.g. STEP on TIM3):
//  *   TIM_StepGenerator step(TIM3, TIM::_3::CH1::PA6);
//  *   step.SetUp(5000, false);         // no IRQ on the STEP timer itself
//  *
//  *   TIM_HWCounter counter(TIM1);     // any timer with a free ITR route to TIM3
//  *   counter.SetUp(step, &OnMoveDone);   // chain once, bind the callback once
//  *   counter.SetDirection(false);        // forward move — a separate, explicit command
//  *   counter.StopAfter(800);             // 800 steps (PWM-mode master: 1:1 with ticks)
//  *   step.Start();
//  *
//  *   // A new move command arrives mid-motion — just retarget, no re-chaining
//  *   // and no need to pass the callback again:
//  *   counter.StopAfter(300);             // 300 more steps from wherever CNT is now
//  *   counter.StopAT(5000);               // ...or go to absolute position 5000 instead
//  * @endcode
//  */
// class TIM_HWCounter : public TIM
// {
// public:
// 	explicit TIM_HWCounter(TIM_TypeDef* timx) : TIM(timx) {}
//
// 	/**
// 	 * @brief Chains this timer's counter to another timer's Update event via
// 	 * the internal trigger (ITR) bus, in External Clock Mode 1 — CNT then
// 	 * ticks once per master Update event with zero CPU involvement. Starts
// 	 * free-running immediately (poll Count()); SetDirection()/StopAfter()/
// 	 * StopAT() can be called at any point afterwards without redoing this
// 	 * chain or re-passing @p on_complete.
// 	 *
// 	 * Also enables the CC1 auto-stop unconditionally (regardless of whether
// 	 * @p on_complete is null), so StopAfter()/StopAT() work right away —
// 	 * CancelStopAfter() is the only thing that turns it back off.
// 	 *
// 	 * @param master        Timer whose TRGO drives this counter. Its
// 	 *                      EnableTriggerOutput() is called here.
// 	 * @param on_complete   Called from this counter's IRQ whenever a
// 	 *                      StopAfter()/StopAT() target is reached (nullptr =
// 	 *                      no callback, just the auto-stop of this counter
// 	 *                      and @p master). Bound once here for this object's
// 	 *                      whole lifetime — every future StopAfter()/StopAT()
// 	 *                      call reuses it, it can't be changed per-call.
// 	 * @return InitError if this (master, slave) pair has no verified ITR route
// 	 *         for the current MCU family (see TIM::itr_table in tim_defs.hpp).
// 	 */
// 	SysInitStatus SetUp(TIM& master, void (*on_complete)(void) = nullptr);
//
// 	/**
// 	 * @brief Sets which way CNT counts — up (each master Update event adds
// 	 * 1) or down (subtracts 1). A plain, explicit command: independent of
// 	 * StopAfter(), which just uses whatever direction is set at the time
// 	 * it's called to compute its target.
// 	 */
// 	inline void SetDirection(bool down) { TIMx->CR1 = (TIMx->CR1 & ~TIM_CR1_DIR) | (down << TIM_CR1_DIR_Pos); }
// 	/** @brief True if currently set to count down (see SetDirection()). */
// 	inline bool IsCountingDown() const { return (TIMx->CR1 & TIM_CR1_DIR) != 0; }
//
// 	/**
// 	 * @brief Arms a CC1 compare-match interrupt that stops both this counter
// 	 * and the master, and calls the @p on_complete bound in SetUp(), once
// 	 * CNT has moved exactly @p ticks further in whatever direction
// 	 * SetDirection() currently has set. CH1 is used purely as a pin-free
// 	 * comparator (same technique as TIM_PeriodicIRQ::SetCompareIRQ()) — no
// 	 * GPIO is touched.
// 	 *
// 	 * Requires SetUp() to have already chained this counter to its master —
// 	 * this only arms the target, it never redoes the ITR chain and never
// 	 * touches the callback, so it can be called again at any time (running
// 	 * or not) to retarget an already-moving counter, e.g. when a new move
// 	 * command arrives. Also re-enables CC1 if CancelStopAfter() had turned
// 	 * it off.
// 	 *
// 	 * @param ticks  Master Update events to wait for from the current CNT,
// 	 *               in whatever direction is currently set; 0 is invalid.
// 	 *               Raw ticks, same units as Count() — see the class doc for
// 	 *               converting from "steps".
// 	 * @return InitError if SetUp() hasn't been called yet, or if @p ticks
// 	 *         doesn't fit this counter's width (see PeriphInfo::arr_max).
// 	 */
// 	SysInitStatus StopAfter(uint32_t ticks);
//
// 	/**
// 	 * @brief Like StopAfter(), but arms the target at the absolute value
// 	 * @p ticks instead of "current CNT + ticks" — ignores both the current
// 	 * CNT and the current SetDirection(). Useful when tracking an absolute
// 	 * position (e.g. "go to 5000") rather than a relative move.
// 	 *
// 	 * @param ticks  Absolute CNT value to stop at; 0 is invalid. Raw ticks,
// 	 *               same units as Count().
// 	 * @return InitError if SetUp() hasn't been called yet, or if @p ticks
// 	 *         doesn't fit this counter's width (see PeriphInfo::arr_max).
// 	 */
// 	SysInitStatus StopAT(uint32_t ticks);
//
// 	/**
// 	 * @brief Disarms the CC1 auto-stop armed by StopAfter()/StopAT() (or by
// 	 * SetUp() itself). Direction (SetDirection()) and the bound callback are
// 	 * untouched — a later StopAfter()/StopAT() call re-enables CC1, no need
// 	 * to call SetUp() again.
// 	 */
// 	inline void CancelStopAfter() { IRQ_en(IRQ::CC1, DISABLE); }
//
// 	/** @brief Raw counter value — ticks once per master Update event, up or down per SetDirection(). */
// 	inline uint32_t Count() const { return TIMx->CNT; }
// 	/** @brief Resets the counter (CNT) back to 0. */
// 	inline void     Reset()       { TIMx->CNT = 0; }
//
// private:
// 	TIM*  _master      = nullptr;             ///< Master timer set by SetUp(); auto-stopped from HandleIRQ() once a StopAfter()/StopAT() target is reached.
// 	void (*_on_complete)(void) = nullptr;      ///< Bound once in SetUp(); called from HandleIRQ() once a StopAfter()/StopAT() target is reached.
//
// 	void HandleIRQ() override;
// };
//

#endif // TIM_HPP_
