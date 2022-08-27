// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFCameraConverters.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Converters/GLTFConverterUtility.h"
#include "Converters/GLTFNameUtility.h"
#include "Actors/GLTFCameraActor.h"

FGLTFJsonCameraIndex FGLTFCameraConverter::Convert(const UCameraComponent* CameraComponent)
{
	FGLTFJsonCamera Camera;
	Camera.Name = FGLTFNameUtility::GetName(CameraComponent);
	Camera.Type = FGLTFConverterUtility::ConvertCameraType(CameraComponent->ProjectionMode);

	FMinimalViewInfo DesiredView;
	const_cast<UCameraComponent*>(CameraComponent)->GetCameraView(0, DesiredView);
	const float ExportScale = Builder.ExportOptions->ExportScale;

	switch (Camera.Type)
	{
		case EGLTFJsonCameraType::Orthographic:
			if (!DesiredView.bConstrainAspectRatio)
			{
				Builder.AddWarningMessage(FString::Printf(TEXT("Aspect ratio for orthographic camera component %s (in actor %s) will be constrainted in glTF"), *CameraComponent->GetName(), *CameraComponent->GetOwner()->GetName()));
			}
			Camera.Orthographic.XMag = FGLTFConverterUtility::ConvertLength(DesiredView.OrthoWidth, ExportScale);
			Camera.Orthographic.YMag = FGLTFConverterUtility::ConvertLength(DesiredView.OrthoWidth / DesiredView.AspectRatio, ExportScale); // TODO: is this correct?
			Camera.Orthographic.ZFar = FGLTFConverterUtility::ConvertLength(DesiredView.OrthoFarClipPlane, ExportScale);
			Camera.Orthographic.ZNear = FGLTFConverterUtility::ConvertLength(DesiredView.OrthoNearClipPlane, ExportScale);
			break;

		case EGLTFJsonCameraType::Perspective:
			if (DesiredView.bConstrainAspectRatio)
			{
				Camera.Perspective.AspectRatio = DesiredView.AspectRatio;
			}
			Camera.Perspective.YFov = FGLTFConverterUtility::ConvertFieldOfView(DesiredView.FOV, DesiredView.AspectRatio);
			// NOTE: even thought ZFar is optional, if we don't set it, then most gltf viewers won't handle it well.
			Camera.Perspective.ZFar = FGLTFConverterUtility::ConvertLength(WORLD_MAX, ExportScale); // TODO: Unreal doesn't have max draw distance per view?
			Camera.Perspective.ZNear = FGLTFConverterUtility::ConvertLength(GNearClippingPlane, ExportScale);
			break;

		case EGLTFJsonCameraType::None:
			// TODO: report error (unsupported camera type)
			return FGLTFJsonCameraIndex(INDEX_NONE);

		default:
			checkNoEntry();
			break;
	}

	const AActor* Owner = CameraComponent->GetOwner();
	const AGLTFCameraActor* CameraActor = Owner != nullptr ? Cast<AGLTFCameraActor>(Owner) : nullptr;

	if (CameraActor != nullptr)
	{
		if (Builder.ExportOptions->bExportOrbitalCameras)
		{
			FGLTFJsonOrbitCamera OrbitCamera;
			OrbitCamera.Focus = Builder.GetOrAddNode(CameraActor->Focus);
			OrbitCamera.MaxDistance = FGLTFConverterUtility::ConvertLength(CameraActor->DistanceMax, ExportScale);
			OrbitCamera.MinDistance = FGLTFConverterUtility::ConvertLength(CameraActor->DistanceMin, ExportScale);
			OrbitCamera.MaxAngle = CameraActor->PitchAngleMax;
			OrbitCamera.MinAngle = CameraActor->PitchAngleMin;
			OrbitCamera.DistanceSensitivity = CameraActor->DistanceSensitivity;
			OrbitCamera.OrbitSensitivity = CameraActor->OrbitSensitivity;
			OrbitCamera.OrbitInertia = CameraActor->OrbitInertia;
			OrbitCamera.DollyDuration = CameraActor->DollyDuration;

			Camera.OrbitCamera = OrbitCamera;
		}
	}

	return Builder.AddCamera(Camera);
}
