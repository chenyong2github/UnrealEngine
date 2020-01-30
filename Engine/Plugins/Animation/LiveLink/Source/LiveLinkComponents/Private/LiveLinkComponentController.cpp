// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkComponentController.h"

#include "Features/IModularFeatures.h"
#include "LiveLinkComponentSettings.h"
#include "LiveLinkComponentPrivate.h"
#include "LiveLinkControllerBase.h"
#include "UObject/EnterpriseObjectVersion.h"
#include "UObject/UObjectIterator.h"

#if WITH_EDITOR
#include "Framework/Notifications/NotificationManager.h"
#include "Kismet2/ComponentEditorUtils.h"
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


void ULiveLinkComponentController::OnSubjectRoleChanged()
{
	if (SubjectRepresentation.Role == nullptr)
	{
		ControllerMap.Empty();
	}
	else
	{
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
			}
		}
	}
}

void ULiveLinkComponentController::SetControllerClassForRole(TSubclassOf<ULiveLinkRole> RoleClass, TSubclassOf<ULiveLinkControllerBase> DesiredControllerClass)
{
	if (ControllerMap.Contains(RoleClass))
	{
		ULiveLinkControllerBase*& CurrentController = ControllerMap.FindOrAdd(RoleClass);
		if (CurrentController == nullptr || CurrentController->GetClass() != DesiredControllerClass)
		{
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

void ULiveLinkComponentController::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	ILiveLinkClient& LiveLinkClient = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);

	//Evaluate subject frame once and pass the data to our controllers
	FLiveLinkSubjectFrameData SubjectData;
	const bool bHasValidData = LiveLinkClient.EvaluateFrame_AnyThread(SubjectRepresentation.Subject, SubjectRepresentation.Role, SubjectData);

	//Go through each controllers and initialize them if we're dirty and tick them if there's valid data to process
	for (TTuple<TSubclassOf<ULiveLinkRole>, ULiveLinkControllerBase*>& ControllerEntry : ControllerMap)
	{
		ULiveLinkControllerBase* Controller = ControllerEntry.Value;
		if (Controller)
		{
			if (bIsDirty)
			{
				Controller->SetAttachedComponent(ComponentToControl.GetComponent(GetOwner()));
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
	bool bNeedUpdate = ControllerMap.Num() != SelectedRoleHierarchy.Num();
	
	if (!bNeedUpdate)
	{
		//Check if all map matches class hierarchy
		for (const TSubclassOf<ULiveLinkRole>& RoleClass : SelectedRoleHierarchy)
		{
			const ULiveLinkControllerBase* const* FoundController = ControllerMap.Find(RoleClass);
			bNeedUpdate = FoundController == nullptr;
			if (!bNeedUpdate)
			{
				const ULiveLinkControllerBase* FoundControllerPtr = *FoundController;
				TSubclassOf<ULiveLinkControllerBase> DesiredControllerClass = GetControllerClassForRoleClass(RoleClass);
				if (FoundControllerPtr == nullptr)
				{
					bNeedUpdate = DesiredControllerClass != nullptr;
				}
				else
				{
					bNeedUpdate = FoundControllerPtr->GetClass() != DesiredControllerClass;
				}
			}

			if (bNeedUpdate)
			{
				break;
			}
		}
	}

	return bNeedUpdate;
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



#undef LOCTEXT_NAMESPACE