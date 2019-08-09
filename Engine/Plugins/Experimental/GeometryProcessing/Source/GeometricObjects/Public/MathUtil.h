// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"   // required for GEOMETRICOBJECTS_API macro
#include <cmath>

/**
 * Math constants and utility functions, templated on float/double type
 */
template<typename RealType>
class TMathUtil
{
public:
	/** Machine Epsilon - float approx 1e-7, double approx 2e-16 */
	GEOMETRICOBJECTS_API static const RealType Epsilon;
	/** Zero tolerance for math operations (eg like parallel tests) - float 1e-6, double 1e-8 */
	GEOMETRICOBJECTS_API static const RealType ZeroTolerance;

	/** largest possible number for type */
	GEOMETRICOBJECTS_API static const RealType MaxReal;

	/** 3.14159... */
	GEOMETRICOBJECTS_API static const RealType Pi;
	GEOMETRICOBJECTS_API static const RealType FourPi;
	GEOMETRICOBJECTS_API static const RealType TwoPi;
	GEOMETRICOBJECTS_API static const RealType HalfPi;

	/** 1.0 / Pi */
	GEOMETRICOBJECTS_API static const RealType InvPi;
	/** 1.0 / (2*Pi) */
	GEOMETRICOBJECTS_API static const RealType InvTwoPi;

	/** pi / 180 */
	GEOMETRICOBJECTS_API static const RealType DegToRad;
	/** 180 / pi */
	GEOMETRICOBJECTS_API static const RealType RadToDeg;

	//static const RealType LN_2;
	//static const RealType LN_10;
	//static const RealType INV_LN_2;
	//static const RealType INV_LN_10;

	GEOMETRICOBJECTS_API static const RealType Sqrt2;
	GEOMETRICOBJECTS_API static const RealType InvSqrt2;
	GEOMETRICOBJECTS_API static const RealType Sqrt3;
	GEOMETRICOBJECTS_API static const RealType InvSqrt3;

	static inline bool IsNaN(const RealType Value);
	static inline bool IsFinite(const RealType Value);
	static inline RealType Abs(const RealType Value);
	static inline RealType Clamp(const RealType Value, const RealType ClampMin, const RealType ClampMax);
	static inline RealType Sign(const RealType Value);
	static inline RealType SignNonZero(const RealType Value);
	static inline RealType Max(const RealType A, const RealType B);
	static inline RealType Max3(const RealType A, const RealType B, const RealType C);
	static inline RealType Min(const RealType A, const RealType B);
	static inline RealType Min3(const RealType A, const RealType B, const RealType C);
	static inline RealType Sqrt(const RealType Value);
	static inline RealType Atan2(const RealType ValueY, const RealType ValueX);
	static inline RealType Sin(const RealType Value);
	static inline RealType Cos(const RealType Value);
	static inline RealType ACos(const RealType Value);
	static inline RealType Floor(const RealType Value);
	static inline RealType Ceil(const RealType Value);



	/**
	 * @return result of Atan2 shifted to [0,2pi]  (normal ATan2 returns in range [-pi,pi])
	 */
	static inline RealType Atan2Positive(const RealType Y, const RealType X);

private:
	TMathUtil() = delete;
};
typedef TMathUtil<float> FMathf;
typedef TMathUtil<double> FMathd;


template<typename RealType>
bool TMathUtil<RealType>::IsNaN(const RealType Value)
{
	return std::isnan(Value);
}


template<typename RealType>
bool TMathUtil<RealType>::IsFinite(const RealType Value)
{
	return std::isfinite(Value);
}


template<typename RealType>
RealType TMathUtil<RealType>::Abs(const RealType Value)
{
	return (Value >= (RealType)0) ? Value : -Value;
}


template<typename RealType>
RealType TMathUtil<RealType>::Clamp(const RealType Value, const RealType ClampMin, const RealType ClampMax)
{
	return (Value < ClampMin) ? ClampMin : ((Value > ClampMax) ? ClampMax : Value);
}


template<typename RealType>
RealType TMathUtil<RealType>::Sign(const RealType Value)
{
	return (Value > (RealType)0) ? (RealType)1 : ((Value < (RealType)0) ? (RealType)-1 : (RealType)0);
}

template<typename RealType>
RealType TMathUtil<RealType>::SignNonZero(const RealType Value)
{
	return (Value < (RealType)0) ? (RealType)-1 : (RealType)1;
}

template<typename RealType>
RealType TMathUtil<RealType>::Max(const RealType A, const RealType B)
{
	return (A >= B) ? A : B;
}

template<typename RealType>
RealType TMathUtil<RealType>::Max3(const RealType A, const RealType B, const RealType C)
{
	return Max(Max(A, B), C);
}

template<typename RealType>
RealType TMathUtil<RealType>::Min(const RealType A, const RealType B)
{
	return (A <= B) ? A : B;
}

template<typename RealType>
RealType TMathUtil<RealType>::Min3(const RealType A, const RealType B, const RealType C)
{
	return Min(Min(A, B), C);
}

template<typename RealType>
RealType TMathUtil<RealType>::Sqrt(const RealType Value)
{
	return sqrt(Value);
}

template<typename RealType>
RealType TMathUtil<RealType>::Atan2(const RealType ValueY, const RealType ValueX)
{
	return atan2(ValueY, ValueX);
}

template<typename RealType>
RealType TMathUtil<RealType>::Sin(const RealType Value)
{
	return sin(Value);
}

template<typename RealType>
RealType TMathUtil<RealType>::Cos(const RealType Value)
{
	return cos(Value);
}

template<typename RealType>
RealType TMathUtil<RealType>::ACos(const RealType Value)
{
	return acos(Value);
}

template<typename RealType>
RealType TMathUtil<RealType>::Floor(const RealType Value)
{
	return floor(Value);
}

template<typename RealType>
RealType TMathUtil<RealType>::Ceil(const RealType Value)
{
	return ceil(Value);
}


template<typename RealType>
RealType TMathUtil<RealType>::Atan2Positive(const RealType Y, const RealType X)
{
	// @todo this is a float atan2 !!
	RealType Theta = TMathUtil<RealType>::Atan2(Y, X);
	if (Theta < 0)
	{
		return ((RealType)2 * TMathUtil<RealType>::Pi) + Theta;
	}
	return Theta;
}


