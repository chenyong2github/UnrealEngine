// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFConverterUtility.h"
#include "Converters/GLTFTextureUtility.h"

EGLTFJsonAlphaMode FGLTFConverterUtility::ConvertBlendMode(EBlendMode Mode)
{
	switch (Mode)
	{
		case BLEND_Opaque:      return EGLTFJsonAlphaMode::Opaque;
		case BLEND_Translucent: return EGLTFJsonAlphaMode::Blend;
		case BLEND_Masked:      return EGLTFJsonAlphaMode::Mask;
		default:                return EGLTFJsonAlphaMode::Opaque; // fallback
	}
}

EGLTFJsonTextureWrap FGLTFConverterUtility::ConvertWrap(TextureAddress Address)
{
	switch (Address)
	{
		case TA_Wrap:   return EGLTFJsonTextureWrap::Repeat;
		case TA_Mirror: return EGLTFJsonTextureWrap::MirroredRepeat;
		case TA_Clamp:  return EGLTFJsonTextureWrap::ClampToEdge;
		default:        return EGLTFJsonTextureWrap::Repeat; // fallback
	}
}

EGLTFJsonTextureFilter FGLTFConverterUtility::ConvertMinFilter(TextureFilter Filter)
{
	switch (Filter)
	{
		case TF_Nearest:   return EGLTFJsonTextureFilter::NearestMipmapNearest;
		case TF_Bilinear:  return EGLTFJsonTextureFilter::LinearMipmapNearest;
		case TF_Trilinear: return EGLTFJsonTextureFilter::LinearMipmapLinear;
		default:           return EGLTFJsonTextureFilter::None;
	}
}

EGLTFJsonTextureFilter FGLTFConverterUtility::ConvertMagFilter(TextureFilter Filter)
{
	switch (Filter)
	{
		case TF_Nearest:   return EGLTFJsonTextureFilter::Nearest;
		case TF_Bilinear:  return EGLTFJsonTextureFilter::Linear;
		case TF_Trilinear: return EGLTFJsonTextureFilter::Linear;
		default:           return EGLTFJsonTextureFilter::None;
	}
}

EGLTFJsonTextureFilter FGLTFConverterUtility::ConvertMinFilter(TextureFilter Filter, TextureGroup LODGroup)
{
	return ConvertMinFilter(Filter == TF_Default ? FGLTFTextureUtility::GetDefaultFilter(LODGroup) : Filter);
}

EGLTFJsonTextureFilter FGLTFConverterUtility::ConvertMagFilter(TextureFilter Filter, TextureGroup LODGroup)
{
	return ConvertMagFilter(Filter == TF_Default ? FGLTFTextureUtility::GetDefaultFilter(LODGroup) : Filter);
}
