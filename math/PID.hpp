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
	 * by setters; input pointer bindings stay fixed and pointed-to values may change.
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

	/**
	 * @name Parameter access
	 * Inline getters/setters; input pointers remain constructor-bound. Values must
	 * be finite, ki_saturation/e_deadband nonnegative and dt positive (zero retains
	 * the documented no-op behavior of u()). Validation/serialization is the caller's
	 * responsibility. Setters do not read inputs or run a control step.
	 *
	 * set_k()/set_limits() retain integral state and clamp it once per call
	 * to the new contribution limit. ki==0 holds raw I; a later nonzero ki clamps
	 * it then. set_dt() preserves all calculation state. Parameter changes can
	 * change the output; Reset() is explicit. New dt/deadband apply on the next u().
	 */
	inline float get_kp() const { return _kp; }
	inline float get_ki() const { return _ki; }
	inline float get_kd() const { return _kd; }
	inline float get_ki_saturation() const { return _i_ki_saturation; }
	inline float get_e_deadband() const { return _e_deadband; }
	inline float get_dt() const { return _dt; }

	/// Set all gains, then limit the retained integral once with the new ki.
	inline void set_k(float kp, float ki, float kd) {
		_kp = kp;
		_ki = ki;
		_kd = kd;
		ClampIntegral();
	}
	/// Set the interval for the next u(); the caller also changes the actual cadence.
	inline void set_dt(float value) { _dt = value; }
	/// Set error deadband and integral contribution limit, then limit retained I once.
	inline void set_limits(float e_deadband, float ki_saturation) {
		_e_deadband = e_deadband;
		_i_ki_saturation = ki_saturation;
		ClampIntegral();
	}

	/// Contributions from stored calculation state and the current gains.
	/// Gain setters affect these immediately; no signal reads or control step.
	/// Zero before the first sample and after Reset(). These are not raw I or slope.
	inline float p_term() const { return _kp * _previous_error; }
	inline float i_term() const { return _ki * _integral; }
	inline float d_term() const { return _kd * _derivative; }

private:
	float _kp;
	float _ki;
	float _kd;
	float _i_ki_saturation;
	float _e_deadband;
	float _dt;
	const float* const _target;
	const float* const _data;
	float _integral = 0.0f;
	float _previous_error = 0.0f;
	float _derivative = 0.0f;

	// Used only when changing ki/its limit; the regular u() path stays unchanged.
	inline void ClampIntegral() {
		if (_ki == 0.0f) return;
		const float contribution = _ki * _integral;
		if (contribution > _i_ki_saturation)
			_integral = _i_ki_saturation / _ki;
		else if (contribution < -_i_ki_saturation)
			_integral = -_i_ki_saturation / _ki;
	}
};

#endif // PID_HPP_
