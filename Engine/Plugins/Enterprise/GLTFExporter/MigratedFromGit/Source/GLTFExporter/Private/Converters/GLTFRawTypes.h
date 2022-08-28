// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

// Primarily for type-safety
struct FGLTFRawVector2
{
	float X, Y;

	FGLTFRawVector2(float X, float Y)
		: X(X), Y(Y)
	{
	}
};

// Primarily for type-safety
struct FGLTFRawVector3
{
	float X, Y, Z;

	FGLTFRawVector3(float X, float Y, float Z)
		: X(X), Y(Y), Z(Z)
	{
	}

	FGLTFRawVector3(const FGLTFRawVector2& Vector2, float Z)
		: X(Vector2.X), Y(Vector2.Y), Z(Z)
	{
	}

	operator FGLTFRawVector3() const
	{
		return { X, Y, Z };
	}
};

// Primarily for type-safety
struct FGLTFRawVector4
{
	float X, Y, Z, W;

	FGLTFRawVector4(float X, float Y, float Z, float W)
		: X(X), Y(Y), Z(Z), W(W)
	{
	}

	FGLTFRawVector4(const FGLTFRawVector3& Vector3, float W)
		: X(Vector3.X), Y(Vector3.Y), Z(Vector3.Z), W(W)
	{
	}

	operator FGLTFRawVector3() const
	{
		return { X, Y, Z };
	}
};

// Primarily for type-safety
struct FGLTFRawQuaternion
{
	float X, Y, Z, W;

	FGLTFRawQuaternion(float X, float Y, float Z, float W)
		: X(X), Y(Y), Z(Z), W(W)
	{
	}
};

// Primarily for type-safety
struct FGLTFRawMatrix4
{
	float Cells[16];

	float& operator()(int32 Row, int32 Col)
	{
		return Cells[Row * 4 + Col];
	}

	const float& operator()(int32 Row, int32 Col) const
	{
		return Cells[Row * 4 + Col];
	}
};
