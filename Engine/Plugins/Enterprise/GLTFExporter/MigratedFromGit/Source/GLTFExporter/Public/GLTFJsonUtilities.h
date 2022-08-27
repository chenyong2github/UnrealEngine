// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFJsonEnums.h"

inline const TCHAR* AlphaModeToString(EGLTFJsonAlphaMode Mode)
{
	switch (Mode)
	{
		case EGLTFJsonAlphaMode::Opaque: return TEXT("OPAQUE");
		case EGLTFJsonAlphaMode::Blend:  return TEXT("BLEND");
		case EGLTFJsonAlphaMode::Mask:   return TEXT("MASK");
		default:                         return TEXT("UNKNOWN");
	}
}

inline int32 PrimitiveModeToNumber(EGLTFJsonPrimitiveMode Mode)
{
	return static_cast<int32>(Mode);
}

inline int32 FilterToNumber(EGLTFJsonSamplerFilter Filter)
{
	return static_cast<int32>(Filter);
}

inline int32 WrapModeToNumber(EGLTFJsonSamplerWrap Wrap)
{
	return static_cast<int32>(Wrap);
}

inline const TCHAR* AccessorTypeToString(EGLTFJsonAccessorType Type)
{
	switch (Type)
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

inline int32 ComponentTypeToNumber(EGLTFJsonComponentType Type)
{
	return static_cast<int32>(Type);
}

inline int32 BufferTargetToNumber(EGLTFJsonBufferTarget Target)
{
	return static_cast<int32>(Target);
}
