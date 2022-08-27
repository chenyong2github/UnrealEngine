// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFCameraUtility.h"
#include "Converters/GLTFConverterUtility.h"
#include "Camera/CameraTypes.h"

FGLTFJsonOrthographic FGLTFCameraUtility::ConvertOrthographic(const FMinimalViewInfo& View, const float ConversionScale)
{
	FGLTFJsonOrthographic Orthographic;

	// NOTE: it goes against the glTF standard to not define a XMag, but the gltf viewer can handle it
	if (View.bConstrainAspectRatio)
	{
		Orthographic.XMag = FGLTFConverterUtility::ConvertLength(View.OrthoWidth, ConversionScale);
	}

	Orthographic.YMag = FGLTFConverterUtility::ConvertLength(View.OrthoWidth / View.AspectRatio, ConversionScale); // TODO: is this correct?
	Orthographic.ZFar = FGLTFConverterUtility::ConvertLength(View.OrthoFarClipPlane, ConversionScale);
	Orthographic.ZNear = FGLTFConverterUtility::ConvertLength(View.OrthoNearClipPlane, ConversionScale);
	return Orthographic;
}

FGLTFJsonPerspective FGLTFCameraUtility::ConvertPerspective(const FMinimalViewInfo& View, const float ConversionScale)
{
	FGLTFJsonPerspective Perspective;

	if (View.bConstrainAspectRatio)
	{
		Perspective.AspectRatio = View.AspectRatio;
	}

	Perspective.YFov = ConvertFieldOfView(View);

	// NOTE: even thought ZFar is optional, if we don't set it, then most gltf viewers won't handle it well.
	Perspective.ZFar = FGLTFConverterUtility::ConvertLength(WORLD_MAX, ConversionScale); // TODO: Unreal doesn't have max draw distance per view?
	Perspective.ZNear = FGLTFConverterUtility::ConvertLength(GNearClippingPlane, ConversionScale);
	return Perspective;
}

float FGLTFCameraUtility::ConvertFieldOfView(const FMinimalViewInfo& View)
{
	const float HorizontalFOV = FMath::DegreesToRadians(View.FOV);
	const float VerticalFOV = 2 * FMath::Atan(FMath::Tan(HorizontalFOV / 2) / View.AspectRatio);
	return VerticalFOV;
}
