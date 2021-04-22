// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/Types.h"

namespace CADKernel
{
	template<typename ValueType> const void Sort(const ValueType& ValueA, const ValueType& ValueB, ValueType& OutMin, ValueType& OutMax)
	{
		if (ValueA < ValueB)
		{
			OutMin = ValueA;
			OutMax = ValueB;
		}
		else
		{
			OutMin = ValueB;
			OutMax = ValueA;
		}
	}

	template<typename ValueType> const void Sort(ValueType& Min, ValueType& Max)
	{
		if (Max < Min)
		{
			Swap(Min, Max);
		}
	}

	inline int32 RealCompare(const double Value1, const double Value2, const double Tolerance = SMALL_NUMBER)
	{
		double Difference = Value1 - Value2;
		if (Difference < -Tolerance) 
		{
			return -1;
		}
		if (Difference > Tolerance) 
		{
			return 1;
		}
		return 0;
	}

	template< class T >
	static T Cubic(const T A)
	{
		return A * A * A;
	}

	template< class T >
	uint8 ToUInt8(T Value)
	{
		return FMath::Clamp((uint8)(Value / 255.), (uint8) 0, (uint8) 255);
	}


} // namespace CADKernel	
