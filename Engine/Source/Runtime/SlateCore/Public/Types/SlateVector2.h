// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Math/UnrealMathUtility.h"

namespace UE::Slate
{

FORCEINLINE static FVector2f CastToVector2f(FVector2d InValue)
{
	const float X = UE_REAL_TO_FLOAT(InValue.X);
	const float Y = UE_REAL_TO_FLOAT(InValue.Y);
#if 0
	ensureAlways(FMath::IsNearlyEqual((double)X, InValue.X));
	ensureAlways(FMath::IsNearlyEqual((double)Y, InValue.Y));
#endif
	return FVector2f(X, Y);
}

/**
 * Structure for deprecating FVector2D to FVector2f
 */
struct FDeprecateVector2D
{
	FDeprecateVector2D() = default;
	FDeprecateVector2D(FVector2f InValue)
		: Data(InValue)
	{
	}

public:
	operator FVector2d() const
	{
		return FVector2d(Data);
	}

	operator FVector2f() const
	{
		return Data;
	}

public:
	FORCEINLINE FVector2f operator+(const FVector2d V) const
	{
		return Data + CastToVector2f(V);
	}

	FORCEINLINE FVector2f operator+(const FVector2f V) const
	{
		return Data + V;
	}

	FORCEINLINE FVector2f operator-(const FVector2d V) const
	{
		return Data - CastToVector2f(V);
	}

	FORCEINLINE FVector2f operator-(const FVector2f V) const
	{
		return Data - V;
	}

	FORCEINLINE FVector2f operator+(float Scale) const
	{
		return Data + Scale;
	}

	FORCEINLINE FVector2f operator-(float Scale) const
	{
		return Data - Scale;
	}

	FORCEINLINE FVector2f operator*(float Scale) const
	{
		return Data * Scale;
	}

	FORCEINLINE FVector2f operator/(float Scale) const
	{
		return Data / Scale;
	}

public:
	bool operator==(FVector2d V) const
	{
		return CastToVector2f(V) == Data;
	}

	bool operator==(FVector2f V) const
	{
		return V == Data;
	}

	bool operator!=(FVector2d V) const
	{
		return CastToVector2f(V) != Data;
	}

	bool operator!=(FVector2f V) const
	{
		return V != Data;
	}

private:
	FVector2f Data;
};

} // namespace
