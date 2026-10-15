/**
 * @file PID.h
 * @brief Variable-step PID and accumulated response-quality metrics, C/C++ API.
 *
 * Each controller owns its history; no heap allocation or internal locks.
 * Set e before each call using a consistent application sign (commonly target-y).
 * Inputs/gains must be finite, dT positive, max_ki_I/e_lim nonnegative.
 * Integral uses trapezoidal integration, derivative a finite error difference.
 * Outside the dead zone, |ki*I| is clamped to max_ki_I; ki=0 holds I.
 * Within the dead zone I is held, D=0, e_old is updated, and kp*e still contributes.
 * No output saturation, derivative filtering or automatic sample timing is provided.
 *
 * @code
 *   pid loop = {0};
 *   loop.kp = 1.0f; loop.ki = 0.1f; loop.kd = 0.0f;
 *   loop.max_ki_I = 2.0f; loop.e_lim = 0.01f; loop.dT = 0.001f;
 *   loop.e = target - measured; // Application signals; refresh for each sample.
 *   float command = PID(&loop);
 * @endcode
 * Changing gains/units is not automatically made bumpless; reset history explicitly.
 */
#ifndef PID_H_
#define PID_H_

#include <stdint.h>
#include <math.h>

/** @brief Gains, current error, sample interval and persistent controller history. */
typedef struct
{
	uint32_t init; // Reserved; not used by the controller.

	float kp;
	float ki;
	float kd;
	float max_ki_I; // Maximum absolute integral contribution |ki * I|.
	float e_lim;    // Dead zone |e| <= e_lim: hold I, set D to zero.
	float dT; // Positive interval in seconds; zero returns zero without state updates.

	float e;     // Current application error, set before PID().
	float I;     // Integrated error, not the already gain-scaled contribution.
	float e_old; // Previous nonzero-dT sample's error.
	float D;     // Error slope outside the dead zone; zero within it.

}pid;

/**
 * @brief Quality history, initialized to zero before collecting a response.
 * max_y tracks an extreme from a zero baseline. sigma is a relative fraction,
 * not a percentage and not clamped nonnegative. Keep target and mode constant.
 */
typedef struct
{
	float max_y;  // Peak y: maximum for u >= 0, minimum for u < 0; reset to zero.
	float sigma;  // Relative peak deviation (max_y - u) / u; zero for u == 0.
	float t_time; // Accumulated time, in the same units as dt; see get modes below.
	float e;      // Relative (%) or absolute error, depending on get and u.

}con_quality_t;

#ifdef __cplusplus
extern "C" {
#endif

// Zero-initialize the state before first use, or call zero_PID_I_D().
// Set e before each call. Inputs and coefficients must be finite; dT is in
// seconds and must be positive, e_lim and max_ki_I must be nonnegative.
// dT == 0 returns zero without changing the state.
// Outside the dead zone, clamp ki * I to +/-max_ki_I; ki == 0 holds I.
// Every nonzero-dT call updates e_old. The kp * e contribution is always kept.
/**
 * @brief Update a sample and return kp*e + ki*I + kd*D.
 * @param p Non-null state with current e and sample interval.
 * @return Output, or zero without state changes when dT==0.
 * Application handles actuator limits and serialization of this controller.
 */
float PID(pid* p);

// Reset I, D and e_old; coefficients, dT and the current error e are preserved.
/**
 * @brief Clear I, D and e_old, preserving gains, limits, dT and current e.
 * @param p Non-null state. Does not compute a new output.
 */
void zero_PID_I_D(pid* p);

// Initialize qual to zero or reset it with get == 0 before collecting samples.
// Keep u and get constant during a measurement; reset when changing them.
// Inputs must be finite and dt nonnegative. Supported modes:
// get == 0: reset every quality field; u, y and dt are not used.
// get == 1: e = 100 * (y - u) / u, or y - u for u == 0;
//           t_time accumulates only outside u +/- |u| * 0.05.
// get == 2: e = y - u; t_time accumulates on every call.
// t_time is an accumulated duration, not settling time.
/**
 * @brief Accumulate metrics for a constant target/mode.
 * @param qual Non-null history; initialize to zero or reset with get=0.
 * @param u Constant target; reset history when changing target or mode.
 * @param y Measured output, in the same units as u.
 * @param dt Nonnegative sample duration; t_time uses these same units.
 * @param get 0 resets; 1 relative error/outside-5%-band duration;
 *            2 absolute error/all-sample duration. Other modes are unsupported.
 *
 * For u<0 max_y tracks the minimum, otherwise the maximum. sigma=(max_y-u)/u,
 * or zero for u=0. Mode 1 e is percent for nonzero u, absolute y-u for u=0.
 * t_time is accumulated duration, not settling time.
 *
 * @code
 *   con_quality_t quality = {0};
 *   PID_get_quality(&quality, 0, 0, 0, 0);
 *   PID_get_quality(&quality, target, measured, 0.001f, 1);
 * @endcode
 */
void PID_get_quality(con_quality_t* qual, float u, float y, float dt, uint32_t get);

#ifdef __cplusplus
}
#endif

#endif /* PID_H_ */
