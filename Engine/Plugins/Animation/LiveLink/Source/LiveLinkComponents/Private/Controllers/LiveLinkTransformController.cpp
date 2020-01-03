// Copyright Epic Games, Inc. All Rights Reserved.

#include "Controllers/LiveLinkTransformController.h"
#include "LiveLinkComponentPrivate.h"

#include "ILiveLinkClient.h"
#include "Roles/LiveLinkTransformRole.h"
#include "Roles/LiveLinkTransformTypes.h"

#include "Components/SceneComponent.h"
#include "Features/IModularFeatures.h"
#include "GameFramework/Actor.h"

#if WITH_EDITOR
#include "Framework/Notifications/NotificationManager.h"
#include "Kismet2/ComponentEditorUtils.h"
#include "Widgets/Notifications/SNotificationList.h"
#endif

#define LOCTEXT_NAMESPACE "LiveLinkController"

void FLiveLinkTransformControllerData::ApplyTransform(USceneComponent* SceneComponent, const FTransform& Transform) const
{
	if (SceneComponent)
	{
		FTransform ComponentTransform = Transform;
		if (!bUseScale)
		{
			ComponentTransform.SetScale3D(FVector::OneVector);
		}
		if (bWorldTransform)
		{
			SceneComponent->SetWorldTransform(ComponentTransform, bSweep, nullptr, bTeleport ? ETeleportType::TeleportPhysics : ETeleportType::ResetPhysics);
		}
		else
		{
			SceneComponent->SetRelativeTransform(ComponentTransform, bSweep, nullptr, bTeleport ? ETeleportType::TeleportPhysics : ETeleportType::ResetPhysics);
		}
	}
}


void FLiveLinkTransformControllerData::CheckForError(FName OwnerName, USceneComponent* SceneComponent) const
{
	if (SceneComponent == nullptr)
	{
		UE_LOG(LogLiveLinkComponents, Warning, TEXT("The component to control is invalid for '%s'."), *OwnerName.ToString());
#if WITH_EDITOR
		FNotificationInfo NotificationInfo(LOCTEXT("InvalidComponent", "The component to control is invalid."));
		NotificationInfo.ExpireDuration = 2.0f;
		FSlateNotificationManager::Get().AddNotification(NotificationInfo);
#endif
	}
	else if (SceneComponent->Mobility != EComponentMobility::Movable)
	{
		UE_LOG(LogLiveLinkComponents, Warning, TEXT("The component '%s' has an invalid mobility."), *OwnerName.ToString());
#if WITH_EDITOR
		FNotificationInfo NotificationInfo(FText::Format(LOCTEXT("InvalidMobility", "'{0}' has an invalid mobility"), FText::FromName(OwnerName)));
		NotificationInfo.ExpireDuration = 2.0f;
		FSlateNotificationManager::Get().AddNotification(NotificationInfo);
#endif
	}
}


void ULiveLinkTransformController::OnEvaluateRegistered()
{
	AActor* OuterActor = GetOuterActor();
	TransformData.CheckForError(OuterActor ? OuterActor->GetFName() : NAME_None, Cast<USceneComponent>(ComponentToControl.GetComponent(OuterActor)));
}


void ULiveLinkTransformController::Tick(float DeltaTime, const FLiveLinkSubjectRepresentation& SubjectRepresentation)
{
	if (USceneComponent* SceneComponent = Cast<USceneComponent>(ComponentToControl.GetComponent(GetOuterActor())))
	{
		ILiveLinkClient& LiveLinkClient = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);

		FLiveLinkSubjectFrameData SubjectData;
		if (LiveLinkClient.EvaluateFrame_AnyThread(SubjectRepresentation.Subject, SubjectRepresentation.Role, SubjectData))
		{
			FLiveLinkTransformStaticData* StaticData = SubjectData.StaticData.Cast<FLiveLinkTransformStaticData>();
			FLiveLinkTransformFrameData* FrameData = SubjectData.FrameData.Cast<FLiveLinkTransformFrameData>();

			if (StaticData && FrameData)
			{
				TransformData.ApplyTransform(SceneComponent, FrameData->Transform);
			}
		}
	}
}


bool ULiveLinkTransformController::IsRoleSupported(const TSubclassOf<ULiveLinkRole>& RoleToSupport)
{
	return RoleToSupport->IsChildOf(ULiveLinkTransformRole::StaticClass());
}


#if WITH_EDITOR
void ULiveLinkTransformController::InitializeInEditor()
{
	if (AActor* Actor = GetOuterActor())
	{
		if (USceneComponent* SceneComponent = Actor->FindComponentByClass<USceneComponent>())
		{
			ComponentToControl = FComponentEditorUtils::MakeComponentReference(Actor, SceneComponent);
		}
	}
}
#endif

#undef LOCTEXT_NAMESPACE
