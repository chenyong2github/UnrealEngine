// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FBounds
{
	FVector	Min = {  MAX_flt,  MAX_flt,  MAX_flt };
	FVector	Max = { -MAX_flt, -MAX_flt, -MAX_flt };

	FORCEINLINE FBounds& operator=( const FVector& Other )
	{
		Min = Other;
		Max = Other;
		return *this;
	}

	FORCEINLINE FBounds& operator+=( const FVector& Other )
	{
		Min = FVector::Min( Min, Other );
		Max = FVector::Max( Max, Other );
		return *this;
	}

	FORCEINLINE FBounds& operator+=( const FBounds& Other )
	{
		Min = FVector::Min( Min, Other.Min );
		Max = FVector::Max( Max, Other.Max );
		return *this;
	}

	FORCEINLINE FBounds operator+( const FBounds& Other ) const
	{
		return FBounds(*this) += Other;
	}

	FORCEINLINE FVector GetCenter() const
	{
		return (Max + Min) * 0.5f;
	}

	FORCEINLINE FVector GetExtent() const
	{
		return (Max - Min) * 0.5f;
	}

	FORCEINLINE float GetSurfaceArea() const
	{
		FVector Size = Max - Min;
		return 0.5f * ( Size.X * Size.Y + Size.X * Size.Z + Size.Y * Size.Z );
	}

	FORCEINLINE friend FArchive& operator<<(FArchive& Ar, FBounds& Bounds)
	{
		Ar << Bounds.Min;
		Ar << Bounds.Max;
		return Ar;
	}
};