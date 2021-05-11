// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkComponentController.h"

#include "LiveLinkComponentPrivate.h"
#include "LiveLinkComponentSettings.h"
#include "LiveLinkControllerBase.h"

#include "Features/IModularFeatures.h"
#include "GameFramework/Actor.h"
#include "Logging/LogMacros.h"
#include "UObject/EnterpriseObjectVersion.h"
#include "UObject/UObjectIterator.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Kismet2/ComponentEditorUtils.h"
#include "Widgets/Notifications/SNotificationList.h"
#endif // WITH_EDITOR

#define LOCTEXT_NAMESPACE "LiveLinkController"


ULiveLinkComponentController::ULiveLinkComponentController()
	: bUpdateInEditor(true)
	, bIsDirty(false)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = ETickingGroup::TG_PrePhysics;
	bTickInEditor = true;

#if WITH_EDITOR
	FEditorDelegates::EndPIE.AddUObject(this, &ULiveLinkComponentController::OnEndPIE);
#endif //WITH_EDITOR
}

ULiveLinkComponentController::~ULiveLinkComponentController()
{
#if WITH_EDITOR
	FEditorDelegates::EndPIE.RemoveAll(this);
#endif //WITH_EDITOR
}

void ULiveLinkComponentController::OnSubjectRoleChanged()
{
	//Whenever the subject role is changed, we start from clean controller map. Cleanup the ones currently active
	CleanupControllersInMap();

	if (SubjectRepresentation.Role == nullptr)
	{
		ControllerMap.Empty();
	}
	else
	{
		UActorComponent* DesiredActorComponent = nullptr;
		TArray<TSubclassOf<ULiveLinkRole>> SelectedRoleHierarchy = GetSelectedRoleHierarchyClasses(SubjectRepresentation.Role);
		ControllerMap.Empty(SelectedRoleHierarchy.Num());
		for (const TSubclassOf<ULiveLinkRole>& RoleClass : SelectedRoleHierarchy)
		{
			if (RoleClass)
			{
				//Add each role class of the hierarchy in the map and assign a controller, if any, to each of them
				ControllerMap.FindOrAdd(RoleClass);

				TSubclassOf<ULiveLinkControllerBase> SelectedControllerClass = GetControllerClassForRoleClass(RoleClass);
				SetControllerClassForRole(RoleClass, SelectedControllerClass);

				//Keep track of the most specific available component in the hierarchy
				if (SelectedControllerClass)
				{
					if (AActor* Actor = GetOwner())
					{
						TSubclassOf<UActorComponent> DesiredClass = SelectedControllerClass.GetDefaultObject()->GetDesiredComponentClass();
						if (UActorComponent* ActorComponent = Actor->GetComponentByClass(DesiredClass))
						{
							DesiredActorComponent = ActorComponent;
						}
					}
				}
			}
		}

		//After creating the controller hierarchy, update component to control to the highest in the hierarchy.
#if WITH_EDITOR
		if (ComponentToControl.ComponentProperty == NAME_None && DesiredActorComponent != nullptr)
		{
			AActor* Actor = GetOwner();
			check(Actor);
			ComponentToControl = FComponentEditorUtils::MakeComponentReference(Actor, DesiredActorComponent);
		}
#endif
	}
}

void ULiveLinkComponentController::SetControllerClassForRole(TSubclassOf<ULiveLinkRole> RoleClass, TSubclassOf<ULiveLinkControllerBase> DesiredControllerClass)
{
	if (ControllerMap.Contains(RoleClass))
	{
		ULiveLinkControllerBase*& CurrentController = ControllerMap.FindOrAdd(RoleClass);
		if (CurrentController == nullptr || CurrentController->GetClass() != DesiredControllerClass)
		{
			//Controller is about to change, cleanup current one before 
			if (CurrentController)
			{
				CurrentController->Cleanup();
			}

			if (DesiredControllerClass != nullptr)
			{
				const EObjectFlags ControllerObjectFlags = GetMaskedFlags(RF_Public | RF_Transactional | RF_ArchetypeObject);
				CurrentController = NewObject<ULiveLinkControllerBase>(this, DesiredControllerClass, NAME_None, ControllerObjectFlags);

#if WITH_EDITOR
				//For the controller directly associated with the subject role, set the component to control to the desired component this controller wants
				if (RoleClass == SubjectRepresentation.Role)
				{
					TSubclassOf<UActorComponent> DesiredComponent = CurrentController->GetDesiredComponentClass();
					if (AActor* Actor = GetOwner())
					{
						if (UActorComponent* ActorComponent = Actor->GetComponentByClass(DesiredComponent))
						{
							ComponentToControl = FComponentEditorUtils::MakeComponentReference(Actor, ActorComponent);
						}
					}
				}
				
				CurrentController->InitializeInEditor();
#endif
			}
			else
			{
				CurrentController = nullptr;
			}
		}
	}

	//Mark ourselves as dirty to update each controller's on next tick
	bIsDirty = true;
}

void ULiveLinkComponentController::OnRegister()
{
	Super::OnRegister();

	bIsDirty = true;
}

#if WITH_EDITOR
void ULiveLinkComponentController::OnEndPIE(bool bIsSimulating)
{
	// Cleanup each controller when PIE session is ending
	CleanupControllersInMap();
}
#endif //WITH_EDITOR

void ULiveLinkComponentController::DestroyComponent(bool bPromoteChildren /*= false*/)
{
	// Cleanup each controller before this component is destroyed
	CleanupControllersInMap();

	Super::DestroyComponent(bPromoteChildren);
}

void ULiveLinkComponentController::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	// Check for spawnable
	if (bIsDirty || !bIsSpawnableCache.IsSet())
	{
		static const FName SequencerActorTag(TEXT("SequencerActor"));
		AActor* OwningActor = GetOwner();

		bIsSpawnableCache = OwningActor && OwningActor->ActorHasTag(SequencerActorTag);

		if (*bIsSpawnableCache && bDisableEvaluateLiveLinkWhenSpawnable)
		{
			bEvaluateLiveLink = false;
		}
	}

	ILiveLinkClient& LiveLinkClient = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);

	//Evaluate subject frame once and pass the data to our controllers
	FLiveLinkSubjectFrameData SubjectData;

	const bool bHasValidData = bEvaluateLiveLink ? LiveLinkClient.EvaluateFrame_AnyThread(SubjectRepresentation.Subject, SubjectRepresentation.Role, SubjectData) : false;

	//Go through each controllers and initialize them if we're dirty and tick them if there's valid data to process
	for (TTuple<TSubclassOf<ULiveLinkRole>, ULiveLinkControllerBase*>& ControllerEntry : ControllerMap)
	{
		ULiveLinkControllerBase* Controller = ControllerEntry.Value;
		if (Controller)
		{
			if (bIsDirty)
			{
				Controller->SetAttachedComponent(ComponentToControl.GetComponent(GetOwner()));
				Controller->SetSelectedSubject(SubjectRepresentation);
				Controller->OnEvaluateRegistered();
			}
			
			if (bHasValidData)
			{
				Controller->Tick(DeltaTime, SubjectData);
			}
		}
	}

	if (OnLiveLinkUpdated.IsBound())
	{
		FEditorScriptExecutionGuard ScriptGuard;
		OnLiveLinkUpdated.Broadcast(DeltaTime);
	}
	
	bIsDirty = false;

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}


void ULiveLinkComponentController::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

#if WITH_EDITOR
	Ar.UsingCustomVersion(FEnterpriseObjectVersion::GUID);

	if (Ar.IsLoading())
	{
		if (Ar.CustomVer(FEnterpriseObjectVersion::GUID) < FEnterpriseObjectVersion::LiveLinkControllerSplitPerRole)
		{
			ConvertOldControllerSystem();
		}
	}
#endif
}

#if WITH_EDITOR

void ULiveLinkComponentController::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(ULiveLinkComponentController, bUpdateInEditor))
	{
		bTickInEditor = bUpdateInEditor;
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void ULiveLinkComponentController::ConvertOldControllerSystem()
{
	if (Controller_DEPRECATED)
	{
		TArray<TSubclassOf<ULiveLinkRole>> SelectedRoleHierarchy = GetSelectedRoleHierarchyClasses(SubjectRepresentation.Role);
		ControllerMap.Empty(SelectedRoleHierarchy.Num());
		for (const TSubclassOf<ULiveLinkRole>& RoleClass : SelectedRoleHierarchy)
		{
			if (RoleClass)
			{
				ControllerMap.FindOrAdd(RoleClass);

				//Set the previous controller on the Subject Role entry and create new controllers for parent role classes
				if (RoleClass == SubjectRepresentation.Role)
				{
					ControllerMap[RoleClass] = Controller_DEPRECATED;
				}
				else
				{
					//Verify in project settings if there is a controller associated with this component type. If not, pick the first one we find
					TSubclassOf<ULiveLinkControllerBase> SelectedControllerClass = GetControllerClassForRoleClass(RoleClass);
					SetControllerClassForRole(RoleClass, SelectedControllerClass);
				}
			}
		}
	}

	Controller_DEPRECATED = nullptr;
}

bool ULiveLinkComponentController::IsControllerMapOutdated() const
{
	TArray<TSubclassOf<ULiveLinkRole>> SelectedRoleHierarchy = GetSelectedRoleHierarchyClasses(SubjectRepresentation.Role);
	
	//If the role class hierarchy doesn't have the same number of controllers, early exit, we need to update
	if (ControllerMap.Num() != SelectedRoleHierarchy.Num())
	{
		return true;
	}
	
	//Check if all map matches class hierarchy
	for (const TSubclassOf<ULiveLinkRole>& RoleClass : SelectedRoleHierarchy)
	{
		const ULiveLinkControllerBase* const* FoundController = ControllerMap.Find(RoleClass);

		//If ControllerMap doesn't have an entry for one of the role class hierarchy, we need to update
		if (FoundController == nullptr)
		{
			return true;
		}
	}

	return false;
}

#endif //WITH_EDITOR

TArray<TSubclassOf<ULiveLinkRole>> ULiveLinkComponentController::GetSelectedRoleHierarchyClasses(const TSubclassOf<ULiveLinkRole> InCurrentRoleClass) const
{
	TArray<TSubclassOf<ULiveLinkRole>> ClassHierarchy;

	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (!It->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
		{
			if (InCurrentRoleClass->IsChildOf(*It))
			{
				ClassHierarchy.AddUnique(*It);
			}
		}
	}

	return MoveTemp(ClassHierarchy);
}

TSubclassOf<ULiveLinkControllerBase> ULiveLinkComponentController::GetControllerClassForRoleClass(const TSubclassOf<ULiveLinkRole> RoleClass) const
{
	//Verify in project settings if there is a controller associated with this component type. If not, pick the first one we find that supports that role
	TSubclassOf<ULiveLinkControllerBase> SelectedControllerClass = nullptr;
	const TSubclassOf<ULiveLinkControllerBase>* ControllerClass = GetDefault<ULiveLinkComponentSettings>()->DefaultControllerForRole.Find(RoleClass);
	if (ControllerClass == nullptr || ControllerClass->Get() == nullptr)
	{
		TArray<TSubclassOf<ULiveLinkControllerBase>> NewControllerClasses = ULiveLinkControllerBase::GetControllersForRole(RoleClass);
		if (NewControllerClasses.Num() > 0)
		{
			SelectedControllerClass = NewControllerClasses[0];
		}
	}
	else
	{
		SelectedControllerClass = *ControllerClass;
	}

	return SelectedControllerClass;
}

void ULiveLinkComponentController::CleanupControllersInMap()
{
	//Cleanup the currently active controllers in the map
	for (TPair<TSubclassOf<ULiveLinkRole>, ULiveLinkControllerBase*>& ControllerPair : ControllerMap)
	{
		if (ControllerPair.Value)
		{
			ControllerPair.Value->Cleanup();
		}
	}
}

#undef LOCTEXT_NAMESPACE