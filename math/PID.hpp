/**
 * @file PID.hpp
 * @brief Fixed-step PID with constructor-bound inputs and private state.
 *
 * Each u() call reads target/data once, computes target-data and advances one
 * sample. The application calls u() once per dt seconds; no clock is read here.
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
 * pointers and serializes signal updates, u(), Reset() and diagnostic reads.
 * Output saturation, derivative filtering and automatic timing are not provided.
 */
#ifndef PID_HPP_
#define PID_HPP_

/** @brief PID with fixed parameters and sample interval. */
class PID
{
public:
	/**
	 * @brief Fix parameters and input pointers; zero calculation state.
	 * @param kp Proportional gain.
	 * @param ki Integral gain; zero holds the integral state.
	 * @param kd Derivative gain.
	 * @param ki_saturation Maximum absolute integral contribution |ki*I|, not total output.
	 * @param e_deadband Dead zone: |error| <= e_deadband holds I, sets D=0; P remains active.
	 * @param dt Fixed sample period in seconds; the caller supplies this cadence.
	 * @param ptr_to_target Read-only pointer to the live setpoint.
	 * @param ptr_to_data Read-only pointer to the live measured value.
	 *
	 * No input reads, hardware access or allocation. Parameters/pointer bindings
	 * cannot be changed after construction; the pointed-to values may change.
	 * constexpr supports constinit when the inputs have static storage duration.
	 * Input validity is the caller's responsibility; no constructor traps/checks.
	 */
	constexpr explicit PID(float kp, float ki, float kd, float ki_saturation, float e_deadband,
						   float dt, const float* ptr_to_target, const float* ptr_to_data)
		: _kp(kp), _ki(ki), _kd(kd), _i_ki_saturation(ki_saturation), _e_deadband(e_deadband), _dt(dt),
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

	/// Current gain-scaled contributions; no signal reads or state updates.
	/// Zero before the first sample and after Reset(). These are not raw I or slope.
	inline float p_term() const { return _kp * _previous_error; }
	inline float i_term() const { return _ki * _integral; }
	inline float d_term() const { return _kd * _derivative; }

private:
	const float _kp;
	const float _ki;
	const float _kd;
	const float _i_ki_saturation;
	const float _e_deadband;
	const float _dt;
	const float* const _target;
	const float* const _data;
	float _integral = 0.0f;
	float _previous_error = 0.0f;
	float _derivative = 0.0f;
};

#endif // PID_HPP_
