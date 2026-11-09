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
	if (_parameters.dt == 0.0f) return 0.0f;

	const float target = *_target;
	const float data = *_data;
	const float error = target - data;
	if (error > _parameters.e_deadband || error < -_parameters.e_deadband) {
		if (_parameters.ki != 0.0f) {
			float integral = _integral + (error + _previous_error) * 0.5f * _parameters.dt;
			if (fabsf(_parameters.ki * integral) > _parameters.ki_saturation)
				integral = (integral > 0.0f ? _parameters.ki_saturation : -_parameters.ki_saturation)
					/ fabsf(_parameters.ki);
			_integral = integral;
		}
		_derivative = (error - _previous_error) / _parameters.dt;
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
