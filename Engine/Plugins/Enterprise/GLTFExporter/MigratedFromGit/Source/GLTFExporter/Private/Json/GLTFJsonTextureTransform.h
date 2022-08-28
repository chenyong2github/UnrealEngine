// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonObject.h"
#include "Json/GLTFJsonVector.h"

struct FGLTFJsonTextureTransform : IGLTFJsonObject
{
	FGLTFJsonVector2 Offset;
	FGLTFJsonVector2 Scale;
	float Rotation;

	FGLTFJsonTextureTransform()
		: Offset(FGLTFJsonVector2::Zero)
		, Scale(FGLTFJsonVector2::One)
		, Rotation(0)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		if (!Offset.IsNearlyEqual(FGLTFJsonVector2::Zero))
		{
			Writer.Write(TEXT("offset"), Offset);
		}

		if (!Scale.IsNearlyEqual(FGLTFJsonVector2::One))
		{
			Writer.Write(TEXT("scale"), Scale);
		}

		if (!FMath::IsNearlyEqual(Rotation, 0))
		{
			Writer.Write(TEXT("rotation"), Rotation);
		}
	}

	bool IsNearlyEqual(const FGLTFJsonTextureTransform& Other, float Tolerance = KINDA_SMALL_NUMBER) const
	{
		return Offset.IsNearlyEqual(Other.Offset, Tolerance)
			&& Scale.IsNearlyEqual(Other.Scale, Tolerance)
			&& FMath::IsNearlyEqual(Rotation, Other.Rotation, Tolerance);
	}

	bool IsExactlyEqual(const FGLTFJsonTextureTransform& Other) const
	{
		return Offset.X == Other.Offset.X && Offset.Y == Other.Offset.Y
			&& Scale.X == Other.Scale.X && Scale.Y == Other.Scale.Y
			&& Rotation == Other.Rotation;
	}

	bool IsNearlyDefault(float Tolerance = KINDA_SMALL_NUMBER) const
	{
		return IsNearlyEqual(FGLTFJsonTextureTransform(), Tolerance);
	}

	bool IsExactlyDefault() const
	{
		return IsExactlyEqual(FGLTFJsonTextureTransform());
	}
};
