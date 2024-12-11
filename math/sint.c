/******************************************************************************
 * File: cosint.c Created on 31 мар. 2022 г.  
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

#pragma GCC push_options
#pragma GCC optimize ("O3")

#include "sint.h"

#include <stdint.h>

//**********************************************************
//---------------- module implementation -------------------

typedef enum
{
	coname = -1, siname
} EFName;

inline float cosint( float x, EFName fn );

//---------------- exported module functions ---------------

float sint( float x )
{
	return cosint( x, siname );
}

float cost( float x )
{
	return cosint( x, coname );
}

//**********************************************************
//------------- helper data and function -------------------

#ifndef M_PI
#	define M_PI	3.14159265358979323846	/* pi */
#endif

#ifdef	COSINT_RAD
#	define R_UNIT	M_PI
#else
#	define R_UNIT	180.
#endif

#define R_TU	( R_UNIT / 2. )		// The real measure unit in use

#define M_PRC	0x0A				// The precision of calculations
#define M_MAX	0x1F				// The bit range of calculations

#define	M_DAT	( M_MAX >> 1 )		// The bit range of a data

#define	M_SGN	0x02				// The sign mask for a result of calculations
#define	M_QTR	0x01				// The mask of a circle quarter
#define M_ASQ	( M_SGN | M_QTR )	// The mask of common characteristics for the angular data

#define	M_ADV	( 1 << M_DAT )		// The allowed data value
#define M_ADM	( M_ADV - 1 )		// The mask of allowed data value

#define M_MTR	(int32_t)( ( M_ADV << M_PRC ) / R_TU )

// Slow and precise, maximum error modulus is |0.0003|
#define DO_SLOW( t, pw, A, B ) ( ( ( t * ( B - A ) + ( ( 1 << pw ) - 1 ) ) >> pw ) + A )

// Fast and rough, about ~1.6% speedup, maximum error modulus is |0.0004|
#define DO_FAST( t, pw, A, B ) ( ( ( t * ( B - A ) ) >> pw ) + A )

#ifdef COSINT_FAST
#	define	LN_	DO_FAST
#else
#	define	LN_	DO_SLOW
#endif

// Control data for the spline

#define Ax	0x0000
#define Bx	0x320B
#define Cx	0x655D
#define Dx	0x8000

// helper function

inline float cosint( float x, EFName fn )
{
	int32_t sq, tm;
	int32_t A, B, C, D;

	// Converting from real to integer
	tm = M_MTR;
	tm *= x;
	tm = tm >> M_PRC;

	// Choosing a function via a phase shift by it name
	tm += fn & ( M_ADM );

	// Extracting a quarter of the angle and a sign of the result
	sq = ( tm >> M_DAT ) & M_ASQ;

	// Removing periods and reverse from the angle value
	// Transforming the angle value to the first quarter

	if ( sq & M_QTR )
		tm = ( tm & M_ADM ) ^ M_ADM;
	else
		tm &= M_ADM;

	// Calculating a result thru a line proportion

	A = Dx;
	B = LN_( tm, M_DAT, Cx, Dx );
	C = LN_( tm, M_DAT, Bx, Cx );
	D = LN_( tm, M_DAT, Ax, Bx );

	A = LN_( tm, M_DAT, B, A );
	B = LN_( tm, M_DAT, C, B );
	C = LN_( tm, M_DAT, D, C );

	A = LN_( tm, M_DAT, B, A );
	B = LN_( tm, M_DAT, C, B );

	A = LN_( tm, M_DAT, B, A );

	// Correcting a numeric sign of the result

	x = ( sq & M_SGN ) ? -A : A;

	// Converting from integer to real and return

	x /= M_ADV;

	return x;
}

#pragma GCC pop_options