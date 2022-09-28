// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFCameraConverters.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Utilities/GLTFCoreUtilities.h"
#include "Converters/GLTFNameUtility.h"
#include "Camera/CameraComponent.h"

FGLTFJsonCamera* FGLTFCameraConverter::Convert(const UCameraComponent* CameraComponent)
{
	FGLTFJsonCamera* JsonCamera = Builder.AddCamera();
	JsonCamera->Name = FGLTFNameUtility::GetName(CameraComponent);
	JsonCamera->Type = FGLTFCoreUtilities::ConvertCameraType(CameraComponent->ProjectionMode);

	FMinimalViewInfo DesiredView;
	const_cast<UCameraComponent*>(CameraComponent)->GetCameraView(0, DesiredView);
	const float ExportScale = Builder.ExportOptions->ExportUniformScale;

	switch (JsonCamera->Type)
	{
		case EGLTFJsonCameraType::Orthographic:
			if (!DesiredView.bConstrainAspectRatio)
			{
				Builder.LogWarning(FString::Printf(TEXT("Aspect ratio for orthographic camera component %s (in actor %s) will be constrainted in glTF"), *CameraComponent->GetName(), *CameraComponent->GetOwner()->GetName()));
			}
			JsonCamera->Orthographic.XMag = FGLTFCoreUtilities::ConvertLength(DesiredView.OrthoWidth, ExportScale);
			JsonCamera->Orthographic.YMag = FGLTFCoreUtilities::ConvertLength(DesiredView.OrthoWidth / DesiredView.AspectRatio, ExportScale); // TODO: is this correct?
			JsonCamera->Orthographic.ZFar = FGLTFCoreUtilities::ConvertLength(DesiredView.OrthoFarClipPlane, ExportScale);
			JsonCamera->Orthographic.ZNear = FGLTFCoreUtilities::ConvertLength(DesiredView.OrthoNearClipPlane, ExportScale);
			break;

		case EGLTFJsonCameraType::Perspective:
			if (DesiredView.bConstrainAspectRatio)
			{
				JsonCamera->Perspective.AspectRatio = DesiredView.AspectRatio;
			}
			JsonCamera->Perspective.YFov = FGLTFCoreUtilities::ConvertFieldOfView(DesiredView.FOV, DesiredView.AspectRatio);
			// NOTE: even thought ZFar is optional, if we don't set it, then most gltf viewers won't handle it well.
			JsonCamera->Perspective.ZFar = FGLTFCoreUtilities::ConvertLength(WORLD_MAX, ExportScale); // TODO: Unreal doesn't have max draw distance per view?
			JsonCamera->Perspective.ZNear = FGLTFCoreUtilities::ConvertLength(GNearClippingPlane, ExportScale);
			break;

		case EGLTFJsonCameraType::None:
			// TODO: report error (unsupported camera type)
			return nullptr;

		default:
			checkNoEntry();
			break;
	}

	return JsonCamera;
}
