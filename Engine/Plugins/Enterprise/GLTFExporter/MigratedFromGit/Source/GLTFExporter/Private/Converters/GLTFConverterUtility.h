// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Converters/GLTFPackedColor.h"
#include "Json/GLTFJsonEnums.h"
#include "Json/GLTFJsonVector2.h"
#include "Json/GLTFJsonVector3.h"
#include "Json/GLTFJsonVector4.h"
#include "Json/GLTFJsonColor4.h"
#include "Json/GLTFJsonQuaternion.h"
#include "Engine/EngineTypes.h"

struct FGLTFConverterUtility
{
	static FGLTFJsonVector3 ConvertVector(const FVector& Vector)
	{
		// UE4 uses a left-handed coordinate system, with Z up.
		// glTF uses a right-handed coordinate system, with Y up.
		return { Vector.X, Vector.Z, Vector.Y };
	}

	static FGLTFJsonVector3 ConvertPosition(const FVector& Position)
	{
		return ConvertVector(Position * 0.01f); // TODO: use options export scale instead of hardcoded value
	}

	static FGLTFJsonVector3 ConvertScale(const FVector& Scale)
	{
		return ConvertVector(Scale);
	}

	static FGLTFJsonVector3 ConvertNormal(const FVector& Normal)
	{
		return ConvertVector(Normal);
	}

	static FGLTFJsonVector4 ConvertTangent(const FVector& Tangent)
	{
		// glTF stores tangent as Vec4, with W component indicating handedness of tangent basis.
		return { ConvertVector(Tangent), 1.0f };
	}

	static FGLTFJsonVector2 ConvertUV(const FVector2D& UV)
	{
		// No conversion actually needed, this is primarily for type-safety.
		return { UV.X, UV.Y };
	}

	static FGLTFJsonColor4 ConvertColor(const FLinearColor& Color)
	{
		// No conversion actually needed, this is primarily for type-safety.
		return { Color.R, Color.G, Color.B, Color.A };
	}

	static FGLTFPackedColor ConvertColor(const FColor& Color)
	{
		// UE4 uses ABGR or ARGB depending on endianness.
		// glTF always uses RGBA independent of endianness.
		return { Color.R, Color.G, Color.B, Color.A };
	}

	static FGLTFJsonQuaternion ConvertRotation(const FQuat& Rotation)
	{
		// UE4 uses a left-handed coordinate system, with Z up.
		// glTF uses a right-handed coordinate system, with Y up.
		// Rotation = (qX, qY, qZ, qW) = (sin(angle/2) * aX, sin(angle/2) * aY, sin(angle/2) * aZ, cons(angle/2))
		// where (aX, aY, aZ) - rotation axis, angle - rotation angle
		// Y swapped with Z between these coordinate systems
		// also, as handedness is changed rotation is inversed - hence negation
		// therefore glTFRotation = (-qX, -qZ, -qY, qw)

		// Not checking if quaternion is normalized
		// e.g. some sources use non-unit Quats for rotation tangents
		return { -Rotation.X, -Rotation.Z, -Rotation.Y, Rotation.W };
	}

	static FMatrix ConvertMatrix(const FMatrix& Matrix)
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

	static EGLTFJsonAlphaMode ConvertBlendMode(const EBlendMode Mode)
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

	static bool IsSelected(const UActorComponent* ActorComponent);
};
