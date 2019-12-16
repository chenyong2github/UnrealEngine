// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Controllers/LiveLinkCameraController.h"

#include "ILiveLinkClient.h"
#include "Roles/LiveLinkCameraRole.h"
#include "Roles/LiveLinkCameraTypes.h"

#include "Camera/CameraComponent.h"
#include "CineCameraComponent.h"
#include "Features/IModularFeatures.h"
#include "GameFramework/Actor.h"

#if WITH_EDITOR
#include "Kismet2/ComponentEditorUtils.h"
#endif


void ULiveLinkCameraController::OnEvaluateRegistered()
{
	AActor* OuterActor = GetOuterActor();
	TransformData.CheckForError(OuterActor ? OuterActor->GetFName() : NAME_None, Cast<USceneComponent>(ComponentToControl.GetComponent(OuterActor)));
}


void ULiveLinkCameraController::Tick(float DeltaTime, const FLiveLinkSubjectRepresentation& SubjectRepresentation)
{
	if (UCameraComponent* CameraComponent = Cast<UCameraComponent>(ComponentToControl.GetComponent(GetOuterActor())))
	{
		ILiveLinkClient& LiveLinkClient = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);

		FLiveLinkSubjectFrameData SubjectData;
		if (LiveLinkClient.EvaluateFrame_AnyThread(SubjectRepresentation.Subject, SubjectRepresentation.Role, SubjectData))
		{
			FLiveLinkCameraStaticData* StaticData = SubjectData.StaticData.Cast<FLiveLinkCameraStaticData>();
			FLiveLinkCameraFrameData* FrameData = SubjectData.FrameData.Cast<FLiveLinkCameraFrameData>();

			if (StaticData && FrameData)
			{
				TransformData.ApplyTransform(CameraComponent, FrameData->Transform);

				if (StaticData->bIsFieldOfViewSupported) { CameraComponent->SetFieldOfView(FrameData->FieldOfView); } 
				if (StaticData->bIsAspectRatioSupported) { CameraComponent->SetAspectRatio(FrameData->AspectRatio); }
				if (StaticData->bIsProjectionModeSupported) { CameraComponent->SetProjectionMode(FrameData->ProjectionMode == ELiveLinkCameraProjectionMode::Perspective ? ECameraProjectionMode::Perspective : ECameraProjectionMode::Orthographic); }

				if (UCineCameraComponent* CineCameraComponent = Cast<UCineCameraComponent>(CameraComponent))
				{
					if (StaticData->bIsFocalLengthSupported) { CineCameraComponent->CurrentFocalLength = FrameData->FocalLength; }
					if (StaticData->bIsApertureSupported) { CineCameraComponent->CurrentAperture = FrameData->Aperture; }
					if (StaticData->FilmBackWidth > 0.0f) { CineCameraComponent->Filmback.SensorWidth = StaticData->FilmBackWidth; }
					if (StaticData->FilmBackHeight > 0.0f) { CineCameraComponent->Filmback.SensorHeight = StaticData->FilmBackHeight; }
					if (StaticData->bIsFocusDistanceSupported) { CineCameraComponent->FocusSettings.ManualFocusDistance = FrameData->FocusDistance; }
				}
			}
		}
	}
}


bool ULiveLinkCameraController::IsRoleSupported(const TSubclassOf<ULiveLinkRole>& RoleToSupport)
{
	return RoleToSupport->IsChildOf(ULiveLinkCameraRole::StaticClass());
}


#if WITH_EDITOR
void ULiveLinkCameraController::InitializeInEditor()
{
	if (AActor* Actor = GetOuterActor())
	{
		if (UCameraComponent* CameraComponent = Actor->FindComponentByClass<UCameraComponent>())
		{
			ComponentToControl = FComponentEditorUtils::MakeComponentReference(Actor, CameraComponent);
		}
	}
}
#endif
