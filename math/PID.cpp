/**
 * @file PID.cpp
 * @brief Fixed-step trapezoidal PID.
 * Contracts/examples are in PID.hpp. No clock reads, output saturation,
 * finite-value validation or synchronization are performed here.
 */
#include "PID.hpp"
#include <math.h>

float PID::u()
{
	if (_dt == 0.0f) return 0.0f;

	const float target = *_target;
	const float data = *_data;
	const float error = target - data;
	if (error > _e_deadband || error < -_e_deadband) {
		if (_ki != 0.0f) {
			float integral = _integral + (error + _previous_error) * 0.5f * _dt;
			if (fabsf(_ki * integral) > _i_ki_saturation)
				integral = (integral > 0.0f ? _i_ki_saturation : -_i_ki_saturation)
					/ fabsf(_ki);
			_integral = integral;
		}
		_derivative = (error - _previous_error) / _dt;
	} else {
		_derivative = 0.0f;
	}
	_previous_error = error;
	return p_term() + i_term() + d_term();
}

void PID::Reset()
{
	_integral = 0.0f;
	_previous_error = 0.0f;
	_derivative = 0.0f;
}
