#include <PID.h>

float PID(pid* p)
{
	if(p->dT == 0.0f) return 0.0f;

	if((p->e > p->e_lim) || (p->e < -p->e_lim))
	{
		if(p->ki != 0.0f)
		{
			float tmp_I = p->I + (p->e + p->e_old) * 0.5f * p->dT;
			if(fabsf(p->ki * tmp_I) > p->max_ki_I)
				tmp_I = (tmp_I > 0.0f ? p->max_ki_I : -p->max_ki_I)
					/ fabsf(p->ki);
			p->I = tmp_I;
		}

		p->D = (p->e - p->e_old)/p->dT;
	}
	else
	{
		p->D = 0.0f;
	}
	p->e_old = p->e;

	return (p->kp*p->e + p->ki*p->I + p->kd*p->D);
}

void zero_PID_I_D(pid* p)
{
	p->I = 0;
	p->D = 0;
	p->e_old = 0;
}

void PID_get_quality(con_quality_t* qual, float u, float y, float dt, uint32_t get)
{
	if(get)
	{
		if((u < 0.0f && y < qual->max_y)
		|| (u >= 0.0f && y > qual->max_y))
			qual->max_y = y;

		qual->sigma = u != 0.0f ? (qual->max_y - u) / u : 0.0f;

		const float tolerance = fabsf(u) * 0.05f;
		if((y > u + tolerance)
		|| (y < u - tolerance)
		|| (get == 2))
			qual->t_time+= dt;

		if(get == 1)
			qual->e = u != 0.0f ? ((y - u) / u) * 100.0f : y - u;
		else
		if(get == 2)
			qual->e = (y - u);

	}
	else
	{
		qual->max_y = 0;
		qual->sigma = 0;
		qual->t_time = 0;
		qual->e = 0;
	}
}
