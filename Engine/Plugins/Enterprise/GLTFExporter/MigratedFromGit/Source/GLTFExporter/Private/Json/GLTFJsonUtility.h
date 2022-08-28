// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonEnums.h"

struct FGLTFJsonUtility
{
	template <typename EnumType>
	static int32 ToNumber(EnumType Value)
	{
		return static_cast<int32>(Value);
	}

	static const TCHAR* ToString(EGLTFJsonExtension Value)
	{
		switch (Value)
		{
			case EGLTFJsonExtension::KHR_LightsPunctual:     return TEXT("KHR_lights_punctual");
			case EGLTFJsonExtension::KHR_MaterialsUnlit:     return TEXT("KHR_materials_unlit");
			case EGLTFJsonExtension::KHR_MaterialsClearCoat: return TEXT("KHR_materials_clearcoat");
			case EGLTFJsonExtension::KHR_MeshQuantization:   return TEXT("KHR_mesh_quantization");
			default:                                         return TEXT("unknown");
		}
	}

	static const TCHAR* ToString(EGLTFJsonAlphaMode Value)
	{
		switch (Value)
		{
			case EGLTFJsonAlphaMode::Opaque: return TEXT("OPAQUE");
			case EGLTFJsonAlphaMode::Blend:  return TEXT("BLEND");
			case EGLTFJsonAlphaMode::Mask:   return TEXT("MASK");
			default:                         return TEXT("UNKNOWN");
		}
	}

	static const TCHAR* ToString(EGLTFJsonAccessorType Value)
	{
		switch (Value)
		{
			case EGLTFJsonAccessorType::Scalar: return TEXT("SCALAR");
			case EGLTFJsonAccessorType::Vec2:   return TEXT("VEC2");
			case EGLTFJsonAccessorType::Vec3:   return TEXT("VEC3");
			case EGLTFJsonAccessorType::Vec4:   return TEXT("VEC4");
			case EGLTFJsonAccessorType::Mat2:   return TEXT("MAT2");
			case EGLTFJsonAccessorType::Mat3:   return TEXT("MAT3");
			case EGLTFJsonAccessorType::Mat4:   return TEXT("MAT4");
			default:                            return TEXT("UNKNOWN");
		}
	}
};
