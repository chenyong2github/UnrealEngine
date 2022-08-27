// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFCameraConverters.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Converters/GLTFConverterUtility.h"
#include "Converters/GLTFNameUtility.h"
#include "Actors/GLTFCameraActor.h"
#include "Camera/CameraComponent.h"

FGLTFJsonCamera* FGLTFCameraConverter::Convert(const UCameraComponent* CameraComponent)
{
	FGLTFJsonCamera* JsonCamera = Builder.AddCamera();
	JsonCamera->Name = FGLTFNameUtility::GetName(CameraComponent);
	JsonCamera->Type = FGLTFConverterUtility::ConvertCameraType(CameraComponent->ProjectionMode);

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
			JsonCamera->Orthographic.XMag = FGLTFConverterUtility::ConvertLength(DesiredView.OrthoWidth, ExportScale);
			JsonCamera->Orthographic.YMag = FGLTFConverterUtility::ConvertLength(DesiredView.OrthoWidth / DesiredView.AspectRatio, ExportScale); // TODO: is this correct?
			JsonCamera->Orthographic.ZFar = FGLTFConverterUtility::ConvertLength(DesiredView.OrthoFarClipPlane, ExportScale);
			JsonCamera->Orthographic.ZNear = FGLTFConverterUtility::ConvertLength(DesiredView.OrthoNearClipPlane, ExportScale);
			break;

		case EGLTFJsonCameraType::Perspective:
			if (DesiredView.bConstrainAspectRatio)
			{
				JsonCamera->Perspective.AspectRatio = DesiredView.AspectRatio;
			}
			JsonCamera->Perspective.YFov = FGLTFConverterUtility::ConvertFieldOfView(DesiredView.FOV, DesiredView.AspectRatio);
			// NOTE: even thought ZFar is optional, if we don't set it, then most gltf viewers won't handle it well.
			JsonCamera->Perspective.ZFar = FGLTFConverterUtility::ConvertLength(WORLD_MAX, ExportScale); // TODO: Unreal doesn't have max draw distance per view?
			JsonCamera->Perspective.ZNear = FGLTFConverterUtility::ConvertLength(GNearClippingPlane, ExportScale);
			break;

		case EGLTFJsonCameraType::None:
			// TODO: report error (unsupported camera type)
			return nullptr;

		default:
			checkNoEntry();
			break;
	}

	const AActor* Owner = CameraComponent->GetOwner();
	const AGLTFCameraActor* CameraActor = Owner != nullptr ? Cast<AGLTFCameraActor>(Owner) : nullptr;

	if (CameraActor != nullptr)
	{
		if (Builder.ExportOptions->bExportCameraControls)
		{
			FGLTFJsonCameraControl CameraControl;
			CameraControl.Mode = FGLTFConverterUtility::ConvertCameraControlMode(CameraActor->Mode);
			CameraControl.Target = Builder.GetOrAddNode(CameraActor->Target);
			CameraControl.MaxDistance = FGLTFConverterUtility::ConvertLength(CameraActor->DistanceMax, ExportScale);
			CameraControl.MinDistance = FGLTFConverterUtility::ConvertLength(CameraActor->DistanceMin, ExportScale);
			CameraControl.MaxPitch = CameraActor->PitchAngleMax;
			CameraControl.MinPitch = CameraActor->PitchAngleMin;

			if (CameraActor->UsesYawLimits())
			{
				// Transform yaw limits to match right-handed system and glTF specification for cameras, i.e
				// positive rotation is CCW, and camera looks down Z- (instead of X+).
				const float MaxYaw = FMath::Max(-CameraActor->YawAngleMin, -CameraActor->YawAngleMax) - 90.0f;
				const float MinYaw = FMath::Min(-CameraActor->YawAngleMin, -CameraActor->YawAngleMax) - 90.0f;

				// We prefer the limits to be in the 0..360 range, but we only use MaxYaw to calculate
				// the needed offset since we need to keep both limits a fixed distance apart from each other.
				const float PositiveRangeOffset = FRotator::ClampAxis(MaxYaw) - MaxYaw;

				CameraControl.MaxYaw = MaxYaw + PositiveRangeOffset;
				CameraControl.MinYaw = MinYaw + PositiveRangeOffset;
			}

			CameraControl.RotationSensitivity = CameraActor->RotationSensitivity;
			CameraControl.RotationInertia = CameraActor->RotationInertia;
			CameraControl.DollySensitivity = CameraActor->DollySensitivity;
			CameraControl.DollyDuration = CameraActor->DollyDuration;

			JsonCamera->CameraControl = CameraControl;
		}
	}

	return JsonCamera;
}
