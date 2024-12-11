/******************************************************************************
 * File: cosint.h Created on 31 мар. 2022 г.  
 *
 * Copyright (c) 2022, "Nikolay E. Garbuz" <nik_garbuz@list.ru>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by "Nikolay E. Garbuz" <nik_garbuz@list.ru>
 * Modified by
 *
 * TAB Size .EQ 4
 ********************************************************************************/

#ifndef _COSINT_H_
#define _COSINT_H_

#ifdef __cplusplus
extern "C"
{
#endif

	//**********************************************************
	//---------------- module interface ------------------------

	/**
	 * @brief  Fast sine function for approximate calculations
	 * @param  Geometric angle between -5400.00 and 5400.00 degrees
	 * 		with two correct numbers after the decimal separator
	 * @retval Sine with three correct numbers after the decimal separator
	 */
	float sint( float x );

	/**
	 * @brief  Fast cosine function for approximate calculations
	 * @param  Geometric angle between -5400.00 and 5400.00 degrees
	 * 		with two correct numbers after the decimal separator
	 * @retval Cosine with three correct numbers after the decimal separator
	 */
	float cost( float x );

	//**********************************************************
	//---------------- module options --__----------------------
	/*
	 * The [COSINT_RAD] directive sets the angle units.
	 * By default, module functions take a parameter measured in degrees.
	 *
	 * Remove or comment out the [#undef COSINT_RAD] line
	 * to switch the interface [cosint] from degrees to radiant.
	 */
#define COSINT_RAD
// #undef COSINT_RAD

	/*
	 * The [COSINT_FAST] directive defines the linear proportion method.
	 * The default is the slow method with a maximum error modulus is |0.0003|.
	 *
	 * Remove or comment out the [#undef COSINT_FAST] line to switch module [cosint] to fast mode.
	 * About ~1.6% speedup, maximum error modulus is 0.0004
	 */
#define COSINT_FAST
// #undef COSINT_FAST

#ifdef __cplusplus
}
#endif

#endif /* _COSINT_H_ */