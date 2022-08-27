// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFCameraConverters.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Converters/GLTFConverterUtility.h"
#include "Converters/GLTFNameUtility.h"
#include "Converters/GLTFCameraUtility.h"

FGLTFJsonCameraIndex FGLTFCameraComponentConverter::Convert(const UCameraComponent* CameraComponent)
{
	FGLTFJsonCamera Camera;
	Camera.Name = FGLTFNameUtility::GetName(CameraComponent);
	Camera.Type = FGLTFConverterUtility::ConvertCameraType(CameraComponent->ProjectionMode);

	FMinimalViewInfo DesiredView;
	const_cast<UCameraComponent*>(CameraComponent)->GetCameraView(0, DesiredView);

	switch (Camera.Type)
	{
		case EGLTFJsonCameraType::Orthographic:
			Camera.Orthographic = FGLTFCameraUtility::ConvertOrthographic(DesiredView, Builder.ExportOptions->ExportScale);
			break;

		case EGLTFJsonCameraType::Perspective:
			Camera.Perspective = FGLTFCameraUtility::ConvertPerspective(DesiredView, Builder.ExportOptions->ExportScale);
			break;

		default:
		    // TODO: report error (unsupported camera type)
		    return FGLTFJsonCameraIndex(INDEX_NONE);
	}

	return Builder.AddCamera(Camera);
}
