// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonArray.h"
#include "Json/GLTFJsonVector2.h"
#include "Converters/GLTFRawTypes.h"

struct FGLTFJsonVector3 : IGLTFJsonArray
{
	float X, Y, Z;

	static const FGLTFJsonVector3 Zero;
	static const FGLTFJsonVector3 One;

	FGLTFJsonVector3(float X, float Y, float Z)
		: X(X), Y(Y), Z(Z)
	{
	}

	FGLTFJsonVector3(const FGLTFJsonVector2& Vector2, float Z)
		: X(Vector2.X), Y(Vector2.Y), Z(Z)
	{
	}

	FGLTFJsonVector3(const FGLTFRawVector3& Raw)
		: X(Raw.X), Y(Raw.Y), Z(Raw.Z)
	{
	}

	virtual void WriteArray(IGLTFJsonWriter& Writer) const override
	{
		Writer.Write(X);
		Writer.Write(Y);
		Writer.Write(Z);
	}

	bool operator==(const FGLTFJsonVector3& Other) const
	{
		return X == Other.X
			&& Y == Other.Y
			&& Z == Other.Z;
	}

	bool operator!=(const FGLTFJsonVector3& Other) const
	{
		return X != Other.X
			|| Y != Other.Y
			|| Z != Other.Z;
	}
};
