// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/EngineTypes.h"

struct GLTFEXPORTER_API FGLTFConverterUtility
{
	static inline FVector ConvertVector(const FVector& Vector)
	{
		// UE4 uses a left-handed coordinate system, with Z up.
		// glTF uses a right-handed coordinate system, with Y up.
		return { Vector.X, Vector.Z, Vector.Y };
	}

	static inline FVector ConvertPosition(const FVector& Position)
	{
		return ConvertVector(Position);
	}

	static inline FVector ConvertScale(const FVector& Scale)
	{
		return ConvertVector(Scale);
	}

	static inline FVector ConvertNormal(const FVector& Normal)
	{
		return ConvertVector(Normal);
	}

	static inline FVector4 ConvertTangent(const FVector& Tangent)
	{
		// glTF stores tangent as Vec4, with W component indicating handedness of tangent basis.
		return FVector4(ConvertVector(Tangent), 1.0f);
	}

	static inline FColor ConvertColor(const FColor& Color)
	{
		// UE4 uses ABGR while glTF uses RGBA.
		return { Color.R, Color.G, Color.B, Color.A };
	}

	static inline FQuat ConvertRotation(const FQuat& Rotation)
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

	static inline FMatrix ConvertMatrix(const FMatrix& Matrix)
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

	static inline EGLTFJsonAlphaMode ConvertAlphaMode(const EBlendMode Mode)
	{
		switch (Mode)
		{
			case EBlendMode::BLEND_Opaque:      return EGLTFJsonAlphaMode::Opaque;
			case EBlendMode::BLEND_Translucent: return EGLTFJsonAlphaMode::Blend;
			case EBlendMode::BLEND_Masked:      return EGLTFJsonAlphaMode::Mask;
			default:                            return EGLTFJsonAlphaMode::Opaque; // fallback
		}
	}

	static bool IsSkySphereBlueprint(const UBlueprint* Blueprint);

	static bool IsHDRIBackdropBlueprint(const UBlueprint* Blueprint);
};
