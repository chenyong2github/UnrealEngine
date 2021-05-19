// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FBounds
{
	FVector3f	Min = {  MAX_flt,  MAX_flt,  MAX_flt };
	FVector3f	Max = { -MAX_flt, -MAX_flt, -MAX_flt };

	FORCEINLINE FBounds& operator=( const FVector3f& Other )
	{
		Min = Other;
		Max = Other;
		return *this;
	}

	FORCEINLINE FBounds& operator+=( const FVector3f& Other )
	{
		Min = FVector3f::Min( Min, Other );
		Max = FVector3f::Max( Max, Other );
		return *this;
	}

	FORCEINLINE FBounds& operator+=( const FBounds& Other )
	{
		Min = FVector3f::Min( Min, Other.Min );
		Max = FVector3f::Max( Max, Other.Max );
		return *this;
	}

	FORCEINLINE FBounds operator+( const FBounds& Other ) const
	{
		return FBounds(*this) += Other;
	}

	FORCEINLINE FVector3f GetCenter() const
	{
		return (Max + Min) * 0.5f;
	}

	FORCEINLINE FVector3f GetExtent() const
	{
		return (Max - Min) * 0.5f;
	}

	FORCEINLINE float GetSurfaceArea() const
	{
		FVector3f Size = Max - Min;
		return 0.5f * ( Size.X * Size.Y + Size.X * Size.Z + Size.Y * Size.Z );
	}

	FORCEINLINE friend FArchive& operator<<(FArchive& Ar, FBounds& Bounds)
	{
		Ar << Bounds.Min;
		Ar << Bounds.Max;
		return Ar;
	}
};