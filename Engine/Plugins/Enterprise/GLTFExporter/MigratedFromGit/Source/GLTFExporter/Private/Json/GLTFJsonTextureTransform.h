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

		if (Rotation != 0)
		{
			Writer.Write(TEXT("rotation"), Rotation);
		}
	}

	bool IsNearlyEqual(const FGLTFJsonTextureTransform& Other) const
	{
		return Offset.IsNearlyEqual(Other.Offset)
			&& Scale.IsNearlyEqual(Other.Scale)
			&& FMath::IsNearlyEqual(Rotation, Other.Rotation);
	}

	bool IsExactEqual(const FGLTFJsonTextureTransform& Other) const
	{
		return Offset.X == Other.Offset.X && Offset.Y == Other.Offset.Y
			&& Scale.X == Other.Scale.X && Scale.Y == Other.Scale.Y
			&& Rotation == Other.Rotation;
	}

	bool IsDefault() const
	{
		return IsExactEqual(FGLTFJsonTextureTransform());
	}
};
