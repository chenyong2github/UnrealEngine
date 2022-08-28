// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonObject.h"
#include "Json/GLTFJsonVector2.h"

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
		if (Offset != FGLTFJsonVector2::Zero)
		{
			Writer.Write(TEXT("offset"), Offset);
		}

		if (Scale != FGLTFJsonVector2::One)
		{
			Writer.Write(TEXT("scale"), Scale);
		}

		if (Rotation != 0)
		{
			Writer.Write(TEXT("rotation"), Rotation);
		}
	}

	bool operator==(const FGLTFJsonTextureTransform& Other) const
	{
		return Offset == Other.Offset
			&& Scale == Other.Scale
			&& Rotation == Other.Rotation;
	}

	bool operator!=(const FGLTFJsonTextureTransform& Other) const
	{
		return !(*this == Other);
	}
};
