// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFCameraConverters.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Converters/GLTFConverterUtility.h"
#include "Converters/GLTFNameUtility.h"
#include "Converters/GLTFCameraUtility.h"
#include "Actors/GLTFCameraActor.h"

FGLTFJsonCameraIndex FGLTFCameraConverter::Convert(const UCameraComponent* CameraComponent)
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
			OrbitCamera.MaxDistance = FGLTFConverterUtility::ConvertLength(CameraActor->DistanceMax, Builder.ExportOptions->ExportScale);
			OrbitCamera.MinDistance = FGLTFConverterUtility::ConvertLength(CameraActor->DistanceMin, Builder.ExportOptions->ExportScale);
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
