/**
 * @file PID.hpp
 * @brief PID with adjustable parameters, constructor-bound inputs and private state.
 *
 * Each u() call reads target/data once, computes target-data and advances one
 * sample. The application calls u() once per configured dt seconds; no clock is read here.
 * Only the state needed for I/D and current-term diagnostics is stored.
 * No heap allocation, sample log or response-quality evaluation is used.
 *
 * @code
 *   float target = 100.0f;
 *   float measured = 0.0f;
 *   constinit PID controller(
 *       1.0f, 0.1f, 0.0f,       // kp, ki, kd
 *       2.0f, 0.01f, 0.001f,    // ki_saturation, e_deadband, dt (seconds)
 *       &target, &measured);
 *   // Once per 1 ms, after refreshing measured:
 *   float command = controller.u();
 * @endcode
 *
 * Inputs and gains must be finite; dt is positive, ki_saturation/e_deadband nonnegative.
 * Input variables must outlive the controller. The application supplies valid
 * pointers and serializes signal updates, parameter changes, u(), Reset() and reads.
 * Output saturation, derivative filtering and automatic timing are not provided.
 */
#ifndef PID_HPP_
#define PID_HPP_

/** @brief PID with adjustable gains/limits/interval and fixed input bindings. */
class PID
{
public:
	/** @brief All tunable values; the input pointers are not part of the settings. */
	struct Parameters {
		float kp = 1.0f;
		float ki = 0.0f;
		float kd = 0.0f;
		float ki_saturation = 0.0f; ///< Maximum absolute contribution |ki*I|.
		float e_deadband = 0.0f;    ///< Holds I and zeros D when |error| <= this value.
		float dt = 1.0f;            ///< Configured sample interval in seconds.
	};

	/**
	 * @brief Bind inputs and use defaults: kp=dt=1, ki=kd=e_deadband=ki_saturation=0.
	 * No input reads, allocation or calculation. Apply saved settings later with
	 * set_parameters(); input variables must outlive this object.
	 */
	constexpr explicit PID(const float* ptr_to_target, const float* ptr_to_data)
		: _target(ptr_to_target), _data(ptr_to_data) {}

	/**
	 * @brief Set initial parameters, bind input pointers and zero calculation state.
	 * @param kp Proportional gain.
	 * @param ki Integral gain; zero holds the integral state.
	 * @param kd Derivative gain.
	 * @param ki_saturation Maximum absolute integral contribution |ki*I|, not total output.
	 * @param e_deadband Dead zone: |error| <= e_deadband holds I, sets D=0; P remains active.
	 * @param dt Initial sample period in seconds; the caller supplies this cadence.
	 * @param ptr_to_target Read-only pointer to the live setpoint.
	 * @param ptr_to_data Read-only pointer to the live measured value.
	 *
	 * No input reads, hardware access or allocation. Parameters can later be changed
	 * with set_parameters(); input bindings stay fixed and pointed-to values may change.
	 * constexpr supports constinit when the inputs have static storage duration.
	 * Input validity is the caller's responsibility; no constructor traps/checks.
	 */
	constexpr explicit PID(float kp, float ki, float kd, float ki_saturation, float e_deadband,
						   float dt, const float* ptr_to_target, const float* ptr_to_data)
		: _parameters{kp, ki, kd, ki_saturation, e_deadband, dt},
		  _target(ptr_to_target), _data(ptr_to_data) {}

	/**
	 * @brief Read inputs, advance one sample and return P + I + D.
	 * Error is target-data. Integral uses trapezoids and is limited so that
	 * |ki*I| <= ki_saturation. ki==0 holds I. In the dead zone I is held, D is zero
	 * and the previous error is refreshed. No output saturation is applied.
	 * dt==0 returns zero without reading inputs or changing the state.
	 */
	float u();

	/**
	 * @brief Clear integral, derivative and previous error; preserve parameters.
	 * All term getters return zero afterwards. The next sample uses zero as
	 * its previous error, so a nonzero error can cause a derivative transient.
	 * Does not read or modify target/data and does not compute an output.
	 */
	void Reset();

	/**
	 * @brief Return a copy of all settings, for inspection or application storage.
	 * Editing the copy does not affect the controller; apply it with set_parameters().
	 * No input reads or calculation state changes. Caller serializes all access.
	 */
	inline Parameters get_parameters() const { return _parameters; }

	/**
	 * @brief Apply every setting, then clamp the retained integral once with new ki/limit.
	 * No previous-error/derivative reset or control step. With ki==0 raw I is held;
	 * a later nonzero ki applies the limit. Parameter changes can change the output.
	 * New dt/deadband apply on the next u(); caller also updates the actual cadence.
	 * Values must be finite, ki_saturation/e_deadband nonnegative, dt positive;
	 * zero dt retains the documented no-op behavior of u(). Validation and
	 * serialization are the caller's responsibility; input bindings stay fixed.
	 */
	inline void set_parameters(const Parameters& parameters) {
		_parameters = parameters;
		ClampIntegral();
	}

	/// Contributions from stored calculation state and the current gains.
	/// Parameter updates affect these immediately; no signal reads or control step.
	/// Zero before the first sample and after Reset(). These are not raw I or slope.
	inline float p_term() const { return _parameters.kp * _previous_error; }
	inline float i_term() const { return _parameters.ki * _integral; }
	inline float d_term() const { return _parameters.kd * _derivative; }

private:
	Parameters _parameters{};
	const float* const _target;
	const float* const _data;
	float _integral = 0.0f;
	float _previous_error = 0.0f;
	float _derivative = 0.0f;

	// Apply the final ki and limit together, without intermediate clamping.
	inline void ClampIntegral() {
		if (_parameters.ki == 0.0f) return;
		const float contribution = _parameters.ki * _integral;
		if (contribution > _parameters.ki_saturation)
			_integral = _parameters.ki_saturation / _parameters.ki;
		else if (contribution < -_parameters.ki_saturation)
			_integral = -_parameters.ki_saturation / _parameters.ki;
	}
};

#endif // PID_HPP_
