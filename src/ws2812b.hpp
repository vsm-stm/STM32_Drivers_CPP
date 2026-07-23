#ifndef WS2812B_HPP
#define WS2812B_HPP

#include <cstdint>
#include <cstddef>
#include <algorithm>

// ---------------------------------------------------------------------------
// RGB color
// ---------------------------------------------------------------------------

struct RGB
{
	uint8_t r = 0, g = 0, b = 0;

	constexpr RGB() = default;
	constexpr RGB(uint8_t r, uint8_t g, uint8_t b) : r(r), g(g), b(b) {}

	/** Scale all channels by 0-255 (255 = full brightness). */
	constexpr RGB dim(uint8_t brightness) const {
		return {
			static_cast<uint8_t>(static_cast<uint16_t>(r) * brightness / 255u),
			static_cast<uint8_t>(static_cast<uint16_t>(g) * brightness / 255u),
			static_cast<uint8_t>(static_cast<uint16_t>(b) * brightness / 255u)
		};
	}

	constexpr bool operator==(const RGB& o) const { return r==o.r && g==o.g && b==o.b; }
	constexpr bool operator!=(const RGB& o) const { return !(*this == o); }

	// Common presets
	static constexpr RGB Black()   { return {  0,   0,   0}; }
	static constexpr RGB White()   { return {255, 255, 255}; }
	static constexpr RGB Red()     { return {255,   0,   0}; }
	static constexpr RGB Green()   { return {  0, 255,   0}; }
	static constexpr RGB Blue()    { return {  0,   0, 255}; }
	static constexpr RGB Yellow()  { return {255, 255,   0}; }
	static constexpr RGB Cyan()    { return {  0, 255, 255}; }
	static constexpr RGB Magenta() { return {255,   0, 255}; }
	static constexpr RGB Orange()  { return {255, 128,   0}; }
	static constexpr RGB Purple()  { return {128,   0, 255}; }
};

// ---------------------------------------------------------------------------
// HSV → RGB  (h: 0-255, s: 0-255, v: 0-255)
//   h=0   → Red
//   h=85  → Green
//   h=170 → Blue
// ---------------------------------------------------------------------------

inline RGB FromHSV(uint8_t h, uint8_t s, uint8_t v)
{
	if (!s) return {v, v, v};
	uint8_t region = h / 43u;
	uint8_t rem    = static_cast<uint8_t>((h - region * 43u) * 6u);
	uint8_t p = static_cast<uint8_t>(static_cast<uint16_t>(v) * (255u - s) / 255u);
	uint8_t q = static_cast<uint8_t>(static_cast<uint16_t>(v) * (255u - static_cast<uint16_t>(s) * rem / 255u) / 255u);
	uint8_t t = static_cast<uint8_t>(static_cast<uint16_t>(v) * (255u - static_cast<uint16_t>(s) * (255u - rem) / 255u) / 255u);
	switch (region) {
		case 0:  return {v, t, p};
		case 1:  return {q, v, p};
		case 2:  return {p, v, t};
		case 3:  return {p, q, v};
		case 4:  return {t, p, v};
		default: return {v, p, q};
	}
}

// ---------------------------------------------------------------------------
// WS2812B_Strip<N_LEDS>
//
// Manages a uint16_t CCR buffer for DMA-driven PWM output.
//
// Buffer layout:
//   [0 .. N_LEDS*24 - 1]  — pixel data: N_LEDS × 24 CCR values (GRB, MSB first)
//   [N_LEDS*24 .. end  ]  — reset trailer: RESET_SLOTS zeros (≥50 µs LOW)
//
// Usage:
//   WS2812B_Strip<6> strip(CCR_ONE, CCR_ZERO);
//   strip.SetAll(RGB::Red());
//   ws_tim.SendDMA(TIM::Channel::CH1, strip.Data(), strip.Len());
// ---------------------------------------------------------------------------

template<size_t N_LEDS, size_t RESET_SLOTS = 40>
class WS2812B_Strip
{
public:
	static constexpr size_t BUF_SIZE = N_LEDS * 24u + RESET_SLOTS;

	/**
	 * @param ccr_one   CCR value for a "1" bit (≈ 2/3 of ARR+1).
	 * @param ccr_zero  CCR value for a "0" bit (≈ 1/3 of ARR+1).
	 */
	WS2812B_Strip(uint16_t ccr_one, uint16_t ccr_zero)
		: _one(ccr_one), _zero(ccr_zero)
	{
		// _buf is zero-initialised (static or value-init) — reset trailer is
		// already all zeros.  Pixel area needs an explicit first pass.
		Clear();
	}

	// -----------------------------------------------------------------------
	// Pixel setters
	// -----------------------------------------------------------------------

	/** Set one LED by index (0-based). */
	void SetPixel(size_t idx, RGB c) {
		if (idx >= N_LEDS) return;
		uint16_t* p = _buf + idx * 24u;
		EncodeByte(p,       c.g);   // WS2812B wire order: G R B
		EncodeByte(p +  8u, c.r);
		EncodeByte(p + 16u, c.b);
	}
	void SetPixel(size_t idx, uint8_t r, uint8_t g, uint8_t b) {
		SetPixel(idx, {r, g, b});
	}

	/** Fill all LEDs with one color. */
	void SetAll(RGB c) {
		for (size_t i = 0; i < N_LEDS; ++i) SetPixel(i, c);
	}
	void SetAll(uint8_t r, uint8_t g, uint8_t b) { SetAll({r, g, b}); }

	/**
	 * Fill a range [start, end) with one color.
	 * Indices are clamped to [0, N_LEDS).
	 */
	void Fill(size_t start, size_t end, RGB c) {
		if (end > N_LEDS) end = N_LEDS;
		for (size_t i = start; i < end; ++i) SetPixel(i, c);
	}

	/** Turn all LEDs off (send black). */
	void Clear() { SetAll(RGB::Black()); }

	// -----------------------------------------------------------------------
	// Rainbow helpers
	// -----------------------------------------------------------------------

	/**
	 * Fill the strip with a rainbow spread across all LEDs.
	 * @param offset  Starting hue (0-255); increment each call to animate.
	 * @param v       Brightness (0-255).
	 */
	void Rainbow(uint8_t offset = 0, uint8_t v = 255) {
		for (size_t i = 0; i < N_LEDS; ++i) {
			uint8_t h = static_cast<uint8_t>(offset + i * 255u / N_LEDS);
			SetPixel(i, FromHSV(h, 255, v));
		}
	}

	// -----------------------------------------------------------------------
	// DMA interface
	// -----------------------------------------------------------------------

	/** Raw CCR buffer — pass to TIM_PWM::SendDMA(). */
	const uint16_t* Data() const { return _buf; }

	/** Number of uint16_t elements in the buffer (DMA transfer count). */
	static constexpr size_t Len() { return BUF_SIZE; }

	/** Number of LEDs managed by this strip. */
	static constexpr size_t Size() { return N_LEDS; }

	/** Update CCR values for "1" and "0" bits (call after re-configuring the timer). */
	void SetCCR(uint16_t ccr_one, uint16_t ccr_zero) {
		_one = ccr_one; _zero = ccr_zero;
	}

private:
	void EncodeByte(uint16_t* dst, uint8_t byte) {
		for (int bit = 7; bit >= 0; --bit)
			*dst++ = (byte >> bit) & 1u ? _one : _zero;
	}

	uint16_t _one, _zero;
	uint16_t _buf[BUF_SIZE] = {};
};

#endif // WS2812B_HPP
