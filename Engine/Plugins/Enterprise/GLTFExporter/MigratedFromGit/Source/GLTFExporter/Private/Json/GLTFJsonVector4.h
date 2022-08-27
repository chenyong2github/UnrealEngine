// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonArray.h"
#include "Converters/GLTFRawTypes.h"

struct FGLTFJsonVector4 : IGLTFJsonArray
{
	float X, Y, Z, W;

	static const FGLTFJsonVector4 Zero;
	static const FGLTFJsonVector4 One;

	FGLTFJsonVector4(float X, float Y, float Z, float W)
		: X(X), Y(Y), Z(Z), W(W)
	{
	}

	FGLTFJsonVector4(const FGLTFRawVector4& Raw)
		: X(Raw.X), Y(Raw.Y), Z(Raw.Z), W(Raw.W)
	{
	}

	virtual void WriteArray(IGLTFJsonWriter& Writer) const override
	{
		Writer.Write(X);
		Writer.Write(Y);
		Writer.Write(Z);
		Writer.Write(W);
	}

	bool operator==(const FGLTFJsonVector4& Other) const
	{
		return X == Other.X
			&& Y == Other.Y
			&& Z == Other.Z
			&& W == Other.W;
	}

	bool operator!=(const FGLTFJsonVector4& Other) const
	{
		return X != Other.X
			|| Y != Other.Y
			|| Z != Other.Z
			|| W != Other.W;
	}
};
