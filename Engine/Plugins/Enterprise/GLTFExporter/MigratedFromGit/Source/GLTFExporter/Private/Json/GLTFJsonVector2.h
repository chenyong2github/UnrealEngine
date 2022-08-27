// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonArray.h"
#include "Converters/GLTFRawTypes.h"

struct FGLTFJsonVector2 : IGLTFJsonArray
{
	float X, Y;

	static const FGLTFJsonVector2 Zero;
	static const FGLTFJsonVector2 One;

	FGLTFJsonVector2(float X, float Y)
		: X(X), Y(Y)
	{
	}

	FGLTFJsonVector2(const FGLTFRawVector2& Raw)
		: X(Raw.X), Y(Raw.Y)
	{
	}

	virtual void WriteArray(IGLTFJsonWriter& Writer) const override
	{
		Writer.Write(X);
		Writer.Write(Y);
	}

	bool operator==(const FGLTFJsonVector2& Other) const
	{
		return X == Other.X
			&& Y == Other.Y;
	}

	bool operator!=(const FGLTFJsonVector2& Other) const
	{
		return X != Other.X
			|| Y != Other.Y;
	}
};
