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

	static EGLTFJsonShadingModel ConvertShadingModel(const EMaterialShadingModel ShadingModel)
	{
		switch (ShadingModel)
		{
			case MSM_Unlit:      return EGLTFJsonShadingModel::Unlit;
			case MSM_DefaultLit: return EGLTFJsonShadingModel::Default;
			case MSM_ClearCoat:  return EGLTFJsonShadingModel::ClearCoat;
			default:             return EGLTFJsonShadingModel::None;
		}
	}

	static EGLTFJsonAlphaMode ConvertBlendMode(const EBlendMode Mode)
	{
		switch (Mode)
		{
			case BLEND_Opaque:      return EGLTFJsonAlphaMode::Opaque;
			case BLEND_Translucent: return EGLTFJsonAlphaMode::Blend;
			case BLEND_Masked:      return EGLTFJsonAlphaMode::Mask;
			default:                return EGLTFJsonAlphaMode::Opaque; // fallback
		}
	}

	static EGLTFJsonTextureWrap ConvertWrap(TextureAddress Address)
	{
		switch (Address)
		{
			case TA_Wrap:   return EGLTFJsonTextureWrap::Repeat;
			case TA_Mirror: return EGLTFJsonTextureWrap::MirroredRepeat;
			case TA_Clamp:  return EGLTFJsonTextureWrap::ClampToEdge;
			default:        return EGLTFJsonTextureWrap::Repeat; // fallback
		}
	}

	static EGLTFJsonTextureFilter ConvertMinFilter(TextureFilter Filter)
	{
		switch (Filter)
		{
			case TF_Nearest:   return EGLTFJsonTextureFilter::NearestMipmapNearest;
			case TF_Bilinear:  return EGLTFJsonTextureFilter::LinearMipmapNearest;
			case TF_Trilinear: return EGLTFJsonTextureFilter::LinearMipmapLinear;
			default:           return EGLTFJsonTextureFilter::None;
		}
	}

	static EGLTFJsonTextureFilter ConvertMagFilter(TextureFilter Filter)
	{
		switch (Filter)
		{
			case TF_Nearest:   return EGLTFJsonTextureFilter::Nearest;
			case TF_Bilinear:  return EGLTFJsonTextureFilter::Linear;
			case TF_Trilinear: return EGLTFJsonTextureFilter::Linear;
			default:           return EGLTFJsonTextureFilter::None;
		}
	}

	static EGLTFJsonTextureFilter ConvertMinFilter(TextureFilter Filter, TextureGroup LODGroup)
	{
		return ConvertMinFilter(Filter == TextureFilter::TF_Default ? GetDefaultFilter(LODGroup) : Filter);
	}

	static EGLTFJsonTextureFilter ConvertMagFilter(TextureFilter Filter, TextureGroup LODGroup)
	{
		return ConvertMagFilter(Filter == TextureFilter::TF_Default ? GetDefaultFilter(LODGroup) : Filter);
	}

	static TextureFilter GetDefaultFilter(TextureGroup Group);

	template <typename EnumType>
	static FString GetEnumDisplayName(EnumType Value)
	{
		static_assert(TIsEnum<EnumType>::Value, "Should only call this with enum types");
		const UEnum* Enum = StaticEnum<EnumType>();
		check(Enum != nullptr);
		return Enum->GetDisplayNameTextByValue(Value).ToString();
	}
};
