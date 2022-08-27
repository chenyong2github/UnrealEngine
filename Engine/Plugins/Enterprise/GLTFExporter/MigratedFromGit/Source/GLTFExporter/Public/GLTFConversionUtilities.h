// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFJsonEnums.h"
#include "Engine/EngineTypes.h"

inline FVector ConvertVector(const FVector& Vector)
{
	// UE4 uses a left-handed coordinate system, with Z up.
	// glTF uses a right-handed coordinate system, with Y up.
	return { Vector.X, Vector.Z, Vector.Y };
}

inline FVector ConvertPosition(const FVector& Position)
{
	return ConvertVector(Position);
}

inline FVector ConvertSize(const FVector& Size)
{
	return ConvertVector(Size);
}

inline FVector ConvertScale(const FVector& Scale)
{
	return ConvertVector(Scale);
}

inline FVector4 ConvertTangent(const FVector4& Tangent)
{
	// glTF stores tangent as Vec4, with W component indicating handedness of tangent basis.
	return FVector4(ConvertVector(Tangent), Tangent.W);
}

inline FColor ConvertColor(const FColor& Color)
{
	// UE4 uses ABGR while glTF uses RGBA.
	return { Color.R, Color.G, Color.B, Color.A };
}

inline FQuat ConvertRotation(const FQuat& Rotation)
{
	// UE4 uses a left-handed coordinate system, with Z up.
	// glTF uses a right-handed coordinate system, with Y up.
	// Rotation = (qX, qY, qZ, qW) = (sin(angle/2) * aX, sin(angle/2) * aY, sin(angle/2) * aZ, cons(angle/2))
	// where (aX, aY, aZ) - rotation axis, angle - rotation angle
	// Y swapped with Z between these coordinate systems
	// also, as handedness is changed rotation is inversed - hence negation
	// therefore glTFRotation = (-qX, -qZ, -qY, qw)

	const FQuat Result(-Rotation.X, -Rotation.Z, -Rotation.Y, Rotation.W);
	// Not checking if quaternion is normalized
	// e.g. some sources use non-unit Quats for rotation tangents
	return Result;
}

inline FMatrix ConvertMatrix(const FMatrix& Matrix)
{
	// Unreal stores matrix elements in row major order.
	// glTF stores matrix elements in column major order.

	FMatrix Result;
	for (int32 Row = 0; Row < 4; ++Row)
	{
		for (int32 Col = 0; Col < 4; ++Col)
		{
			Result.M[Col][Row] = Matrix.M[Row][Col];
		}
	}
	return Result;
}

inline EGLTFJsonAlphaMode ConvertAlphaMode(const EBlendMode Mode)
{
	switch (Mode)
	{
		case EBlendMode::BLEND_Opaque:      return EGLTFJsonAlphaMode::Opaque;
		case EBlendMode::BLEND_Translucent: return EGLTFJsonAlphaMode::Blend;
		case EBlendMode::BLEND_Masked:      return EGLTFJsonAlphaMode::Mask;
		default:                            return EGLTFJsonAlphaMode::Opaque; // fallback
	}
}

bool IsSkySphereBlueprint(const UBlueprint* Blueprint);

bool IsHDRIBackdropBlueprint(const UBlueprint* Blueprint);
