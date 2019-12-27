// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkComponentController.h"
#include "LiveLinkComponentPrivate.h"

#include "LiveLinkControllerBase.h"

#if WITH_EDITOR
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#endif

#define LOCTEXT_NAMESPACE "LiveLinkController"

ULiveLinkComponentController::ULiveLinkComponentController()
	: bUpdateInEditor(true)
	, bIsDirty(false)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = ETickingGroup::TG_PrePhysics;
	bTickInEditor = true;
}


void ULiveLinkComponentController::OnRegister()
{
	bIsDirty = true;
	Super::OnRegister();
}


void ULiveLinkComponentController::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	if (Controller)
	{
		if (bIsDirty)
		{
			Controller->OnEvaluateRegistered();
			bIsDirty = false;
		}

		Controller->Tick(DeltaTime, SubjectRepresentation);
	}

	if (OnLiveLinkUpdated.IsBound())
	{
		FEditorScriptExecutionGuard ScriptGuard;
		OnLiveLinkUpdated.Broadcast(DeltaTime);
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}


#if WITH_EDITOR

void ULiveLinkComponentController::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	//Detect changes to the SubjectRepresentation blindly when one of our property has changed. In MultiUser, this will be called with an empty property event
	bool bCreateAnewController = false;
	if (SubjectRepresentation.Role.Get() == nullptr)
	{
		Controller = nullptr;
	}
	else if (Controller)
	{
		if (!Controller->IsRoleSupported(SubjectRepresentation.Role))
		{
			Controller = nullptr;
			bCreateAnewController = true;
		}
	}
	else
	{
		bCreateAnewController = true;
	}

	if (bCreateAnewController)
	{
		TSubclassOf<ULiveLinkControllerBase> NewControllerClass = ULiveLinkControllerBase::GetControllerForRole(SubjectRepresentation.Role);
		if (NewControllerClass.Get())
		{
			const EObjectFlags ControllerObjectFlags = GetMaskedFlags(RF_Public | RF_Transactional | RF_ArchetypeObject);
			Controller = NewObject<ULiveLinkControllerBase>(this, NewControllerClass.Get(), NAME_None, ControllerObjectFlags);
			Controller->InitializeInEditor();
		}
		else
		{
			UE_LOG(LogLiveLinkComponents, Warning, TEXT("No controller was found for role '%s'."), *SubjectRepresentation.Role->GetName());
			FNotificationInfo NotificationInfo(LOCTEXT("NoFoundController", "No controller was found for the role."));
			NotificationInfo.ExpireDuration = 2.0f;
			FSlateNotificationManager::Get().AddNotification(NotificationInfo);
		}
	}
	
	bTickInEditor = bUpdateInEditor;
	bIsDirty = true;

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif //WITH_EDITOR

#undef LOCTEXT_NAMESPACE