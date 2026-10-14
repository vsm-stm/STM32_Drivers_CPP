#ifndef PID_H_
#define PID_H_

#include <stdint.h>
#include <math.h>

typedef struct
{
	uint32_t init; // Reserved; not used by the controller.

	float kp;
	float ki;
	float kd;
	float max_ki_I; // Maximum absolute integral contribution |ki * I|.
	float e_lim;    // Dead zone |e| <= e_lim: hold I, set D to zero.
	float dT;

	float e;
	float I;
	float e_old;
	float D;

}pid;

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
float PID(pid* p);

// Reset I, D and e_old; coefficients, dT and the current error e are preserved.
void zero_PID_I_D(pid* p);

// Initialize qual to zero or reset it with get == 0 before collecting samples.
// Keep u and get constant during a measurement; reset when changing them.
// Inputs must be finite and dt nonnegative. Supported modes:
// get == 0: reset every quality field; u, y and dt are not used.
// get == 1: e = 100 * (y - u) / u, or y - u for u == 0;
//           t_time accumulates only outside u +/- |u| * 0.05.
// get == 2: e = y - u; t_time accumulates on every call.
// t_time is an accumulated duration, not settling time.
void PID_get_quality(con_quality_t* qual, float u, float y, float dt, uint32_t get);

#ifdef __cplusplus
}
#endif

#endif /* PID_H_ */
