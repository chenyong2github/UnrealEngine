// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFCameraConverters.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Converters/GLTFConverterUtility.h"
#include "Converters/GLTFNameUtility.h"
#include "Converters/GLTFCameraUtility.h"
#include "Actors/GLTFOrbitCameraActor.h"

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

		default:
			// TODO: report error (unsupported camera type)
			return FGLTFJsonCameraIndex(INDEX_NONE);
	}

	const AActor* Owner = CameraComponent->GetOwner();
	const AGLTFOrbitCameraActor* OrbitCameraActor = Owner != nullptr ? Cast<AGLTFOrbitCameraActor>(Owner) : nullptr;

	if (OrbitCameraActor != nullptr)
	{
		if (Builder.ExportOptions->bExportOrbitalCameras)
		{
			if (OrbitCameraActor->Focus != nullptr)
			{
				FGLTFJsonOrbitCamera OrbitCamera;
				OrbitCamera.Focus = Builder.GetOrAddNode(OrbitCameraActor->Focus);
				OrbitCamera.MaxDistance = FGLTFConverterUtility::ConvertLength(OrbitCameraActor->DistanceMax, Builder.ExportOptions->ExportScale);
				OrbitCamera.MinDistance = FGLTFConverterUtility::ConvertLength(OrbitCameraActor->DistanceMin, Builder.ExportOptions->ExportScale);
				OrbitCamera.MaxAngle = OrbitCameraActor->PitchAngleMax;
				OrbitCamera.MinAngle = OrbitCameraActor->PitchAngleMin;
				OrbitCamera.DistanceSensitivity = OrbitCameraActor->DistanceSensitivity;
				OrbitCamera.OrbitSensitivity = OrbitCameraActor->OrbitSensitivity;
				OrbitCamera.OrbitInertia = OrbitCameraActor->OrbitInertia;
				OrbitCamera.DollyDuration = OrbitCameraActor->DollyDuration;

				Camera.OrbitCamera = OrbitCamera;
			}
			else
			{
				Builder.AddWarningMessage(FString::Printf(
					TEXT("OrbitalCamera %s has no focus set and will be skipped"),
					*Owner->GetName()));
			}
		}
		else
		{
			Builder.AddWarningMessage(FString::Printf(
				TEXT("OrbitalCamera %s disabled by export options"),
				*Owner->GetName()));
		}
	}

	return Builder.AddCamera(Camera);
}
