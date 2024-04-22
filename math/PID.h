#ifndef PID_H_
#define PID_H_

#include <stdint.h>
#include <math.h>

typedef struct
{
	uint32_t init;

	float kp;
	float ki;
	float kd;
	float max_ki_I;
	float e_lim;
	float dT;

	float e;
	float I;
	float e_old;
	float D;

}pid;

typedef struct
{
	float max_y;
	float sigma;
	float t_time;
	float e;

}con_quality_t;

float PID(pid* p);

void zero_PID_I_D(pid* p);

void PID_get_quality(con_quality_t* qual, float u, float y, float dt, uint32_t get); // get: 0 - zero, 1 - get;

#endif /* PID_H_ */
