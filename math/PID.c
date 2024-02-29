#include <PID.h>

float PID(pid* p)
{
	if(0 == p->dT) return 0;

	float tmp_I;

	if((p->e > p->e_lim) || (p->e < -p->e_lim))
	{
//		if(((p->ki*p->I) < p->max_ki_I)
//		&&((p->ki*p->I) > -(p->max_ki_I)))
		tmp_I = p->I + ((p->e+p->e_old)/2)*p->dT;
//		p->I += ((p->e+p->e_old)/2)*p->dT;
		if(fabs(p->ki) * tmp_I < p->max_ki_I)
			p->I = tmp_I;

		p->D = (p->e - p->e_old)/p->dT;

		p->e_old = p->e;
	}

	return (p->kp*p->e + p->ki*p->I + p->kd*p->D);
}

void zero_PID_I_D(pid* p)
{
	p->I = 0;
	p->D = 0;
	p->e_old = 0;
}

void PID_get_quality(con_quality_t* qual, float u, float y, float dt, uint32_t get) // get: 0 - zero, 1 - get rpm, 2 - get pos;
{
	if(get)
	{
		if(y > qual->max_y)
			qual->max_y = y;

		if(0 != u)
			qual->sigma = (qual->max_y - u)/u;

		if((y > u*1.05)
		|| (y < u*0.95)
		|| (get == 2))
			qual->t_time+= dt;

		if(get == 1)
			qual->e = ((y - u)/u)*100;
		else
		if(get == 2)
			qual->e = (y - u);

	}
	else
	{
		qual->max_y = 0;
		qual->sigma = 0;
		qual->t_time = 0;
	}
}
