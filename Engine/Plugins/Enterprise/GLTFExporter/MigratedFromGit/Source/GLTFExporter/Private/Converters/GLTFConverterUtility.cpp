// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFConverterUtility.h"
#include "Converters/GLTFTextureUtility.h"

EGLTFJsonCameraType FGLTFConverterUtility::ConvertCameraType(ECameraProjectionMode::Type ProjectionMode)
{
	switch (ProjectionMode)
	{
		case ECameraProjectionMode::Perspective:  return EGLTFJsonCameraType::Perspective;
		case ECameraProjectionMode::Orthographic: return EGLTFJsonCameraType::Orthographic;
		default:                                  return EGLTFJsonCameraType::None;
	}
}

EGLTFJsonShadingModel FGLTFConverterUtility::ConvertShadingModel(EMaterialShadingModel ShadingModel)
{
	switch (ShadingModel)
	{
		case MSM_Unlit:      return EGLTFJsonShadingModel::Unlit;
		case MSM_DefaultLit: return EGLTFJsonShadingModel::Default;
		case MSM_ClearCoat:  return EGLTFJsonShadingModel::ClearCoat;
		default:             return EGLTFJsonShadingModel::None;
	}
}

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

EGLTFJsonCubeFace FGLTFConverterUtility::ConvertCubeFace(ECubeFace CubeFace)
{
	switch (CubeFace)
	{
		case CubeFace_PosX:	return EGLTFJsonCubeFace::NegX;
		case CubeFace_NegX:	return EGLTFJsonCubeFace::PosX;
		case CubeFace_PosY:	return EGLTFJsonCubeFace::PosZ;
		case CubeFace_NegY:	return EGLTFJsonCubeFace::NegZ;
		case CubeFace_PosZ:	return EGLTFJsonCubeFace::PosY;
		case CubeFace_NegZ:	return EGLTFJsonCubeFace::NegY;
		default:            return EGLTFJsonCubeFace::None;
	}
}

FGLTFJsonOrthographic FGLTFConverterUtility::ConvertOrthographic(const FMinimalViewInfo& View)
{
	// TODO: do we need to convert any property from world units?

	FGLTFJsonOrthographic Orthographic;
	Orthographic.XMag = View.OrthoWidth;
	Orthographic.YMag = View.OrthoWidth / View.AspectRatio; // TODO: is this correct?
	Orthographic.ZFar = View.OrthoFarClipPlane;
	Orthographic.ZNear = View.OrthoNearClipPlane;
	return Orthographic;
}

FGLTFJsonPerspective FGLTFConverterUtility::ConvertPerspective(const FMinimalViewInfo& View)
{
	FGLTFJsonPerspective Perspective;
	Perspective.AspectRatio = View.AspectRatio;
	Perspective.YFov = ConvertFieldOfView(View);
	Perspective.ZFar = 0; // infinite
	Perspective.ZNear = GNearClippingPlane; // TODO: need to convert units?
	return Perspective;
}

float FGLTFConverterUtility::ConvertFieldOfView(const FMinimalViewInfo& View)
{
	const float HorizontalFOV = FMath::DegreesToRadians(View.FOV);
	const float VerticalFOV = FMath::Atan(FMath::Tan(HorizontalFOV) / View.AspectRatio);
	return VerticalFOV;
}
