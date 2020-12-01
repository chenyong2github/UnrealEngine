// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Actor/ActorElementLevelEditorSelectionCustomization.h"
#include "Elements/Actor/ActorElementData.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"

#include "Elements/Framework/TypedElementList.h"
#include "Elements/Framework/EngineElementsLibrary.h"

#include "Elements/Actor/ActorElementSelectionInterface.h"
#include "Elements/Component/ComponentElementSelectionInterface.h"

#include "Editor.h"
#include "LevelUtils.h"
#include "UnrealEdGlobals.h"
#include "EditorModeManager.h"
#include "Editor/GroupActor.h"
#include "ActorGroupingUtils.h"
#include "Editor/UnrealEdEngine.h"
#include "Toolkits/IToolkitHost.h"
#include "Kismet2/ComponentEditorUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogActorLevelEditorSelection, Log, All);

bool FActorElementLevelEditorSelectionCustomization::CanSelectElement(const TTypedElement<UTypedElementSelectionInterface>& InElementSelectionHandle, const FTypedElementSelectionOptions& InSelectionOptions)
{
	return CanSelectActorElement(InElementSelectionHandle, InSelectionOptions);
}

bool FActorElementLevelEditorSelectionCustomization::CanDeselectElement(const TTypedElement<UTypedElementSelectionInterface>& InElementSelectionHandle, const FTypedElementSelectionOptions& InSelectionOptions)
{
	return CanDeselectActorElement(InElementSelectionHandle, InSelectionOptions);
}

bool FActorElementLevelEditorSelectionCustomization::SelectElement(const TTypedElement<UTypedElementSelectionInterface>& InElementSelectionHandle, UTypedElementList* InSelectionSet, const FTypedElementSelectionOptions& InSelectionOptions)
{
	return SelectActorElement(InElementSelectionHandle, InSelectionSet, InSelectionOptions);
}

bool FActorElementLevelEditorSelectionCustomization::DeselectElement(const TTypedElement<UTypedElementSelectionInterface>& InElementSelectionHandle, UTypedElementList* InSelectionSet, const FTypedElementSelectionOptions& InSelectionOptions)
{
	return DeselectActorElement(InElementSelectionHandle, InSelectionSet, InSelectionOptions);
}

bool FActorElementLevelEditorSelectionCustomization::AllowSelectionModifiers(const TTypedElement<UTypedElementSelectionInterface>& InElementSelectionHandle, const UTypedElementList* InSelectionSet)
{
	// Ctrl or Shift clicking an actor is the same as regular clicking when components are selected
	return !UComponentElementSelectionInterface::HasSelectedComponents(InSelectionSet);
}

FTypedElementHandle FActorElementLevelEditorSelectionCustomization::GetSelectionElement(const TTypedElement<UTypedElementSelectionInterface>& InElementSelectionHandle, const UTypedElementList* InCurrentSelection, const ETypedElementSelectionMethod InSelectionMethod)
{
	if (AActor* ConsideredActor = ActorElementDataUtil::GetActorFromHandle(InElementSelectionHandle))
	{
		while (ConsideredActor->IsChildActor())
		{
			ConsideredActor = ConsideredActor->GetParentActor();
		}
		return UEngineElementsLibrary::AcquireEditorActorElementHandle(ConsideredActor);
	}
	return InElementSelectionHandle;
}

bool FActorElementLevelEditorSelectionCustomization::CanSelectActorElement(const TTypedElement<UTypedElementSelectionInterface>& InActorSelectionHandle, const FTypedElementSelectionOptions& InSelectionOptions) const
{
	AActor* Actor = ActorElementDataUtil::GetActorFromHandleChecked(InActorSelectionHandle);

	// Bail if global selection is locked, or this actor cannot be edited or selected
	if (GEdSelectionLock || !Actor->IsEditable() || !Actor->IsSelectable())
	{
		return false;
	}

	// Bail if the actor is hidden, and we're not allowed to select hidden elements
	if (!InSelectionOptions.AllowHidden() && (Actor->IsHiddenEd() || !FLevelUtils::IsLevelVisible(Actor->GetLevel())))
	{
		return false;
	}

	// Ensure that neither the level nor the actor is being destroyed or is unreachable
	const EObjectFlags InvalidSelectableFlags = RF_BeginDestroyed;
	if (Actor->GetLevel()->HasAnyFlags(InvalidSelectableFlags) || (!GIsTransacting && Actor->GetLevel()->IsPendingKillOrUnreachable()))
	{
		UE_LOG(LogActorLevelEditorSelection, Warning, TEXT("SelectActor: %s (%s)"), TEXT("The requested operation could not be completed because the level has invalid flags."), *Actor->GetActorLabel());
		return false;
	}
	if (Actor->HasAnyFlags(InvalidSelectableFlags) || (!GIsTransacting && Actor->IsPendingKillOrUnreachable()))
	{
		UE_LOG(LogActorLevelEditorSelection, Warning, TEXT("SelectActor: %s (%s)"), TEXT("The requested operation could not be completed because the actor has invalid flags."), *Actor->GetActorLabel());
		return false;
	}

	if (!Actor->IsTemplate() && FLevelUtils::IsLevelLocked(Actor->GetLevel()))
	{
		UE_CLOG(InSelectionOptions.WarnIfLocked(), LogActorLevelEditorSelection, Warning, TEXT("SelectActor: %s (%s)"), TEXT("The requested operation could not be completed because the level is locked."), *Actor->GetActorLabel());
		return false;
	}

	// If grouping operations are not currently allowed, don't select groups
	AGroupActor* SelectedGroupActor = Cast<AGroupActor>(Actor);
	if (SelectedGroupActor && (!UActorGroupingUtils::IsGroupingActive() || !InSelectionOptions.AllowGroups()))
	{
		return false;
	}

	if (const IToolkitHost* ToolkitHostPtr = GetToolkitHost())
	{
		// Allow active modes to determine whether the selection is allowed
		return ToolkitHostPtr->GetEditorModeManager().IsSelectionAllowed(Actor, /*bInSelected*/true);
	}

	return true;
}

bool FActorElementLevelEditorSelectionCustomization::CanDeselectActorElement(const TTypedElement<UTypedElementSelectionInterface>& InActorSelectionHandle, const FTypedElementSelectionOptions& InSelectionOptions) const
{
	AActor* Actor = ActorElementDataUtil::GetActorFromHandleChecked(InActorSelectionHandle);

	// Bail if global selection is locked
	if (GEdSelectionLock)
	{
		return false;
	}

	if (const IToolkitHost* ToolkitHostPtr = GetToolkitHost())
	{
		// Allow active modes to determine whether the deselection is allowed
		return ToolkitHostPtr->GetEditorModeManager().IsSelectionAllowed(Actor, /*bInSelected*/false);
	}

	return true;
}

bool FActorElementLevelEditorSelectionCustomization::SelectActorElement(const TTypedElement<UTypedElementSelectionInterface>& InActorSelectionHandle, UTypedElementList* InSelectionSet, const FTypedElementSelectionOptions& InSelectionOptions)
{
	AActor* Actor = ActorElementDataUtil::GetActorFromHandleChecked(InActorSelectionHandle);

	if (const IToolkitHost* ToolkitHostPtr = GetToolkitHost())
	{
		// Allow active modes to potentially handle the selection
		// TODO: Should this pass through the selection set?
		if (ToolkitHostPtr->GetEditorModeManager().IsSelectionHandled(Actor, /*bInSelected*/true))
		{
			return true;
		}
	}

	// If trying to select an actor, use this actors root selection actor instead (if it has one)
	if (AActor* RootSelection = Actor->GetRootSelectionParent())
	{
		Actor = RootSelection;
	}

	bool bSelectionChanged = false;

	if (UActorGroupingUtils::IsGroupingActive() && InSelectionOptions.AllowGroups())
	{
		// If this actor is a group, do a group select
		if (AGroupActor* SelectedGroupActor = Cast<AGroupActor>(Actor))
		{
			bSelectionChanged |= SelectActorGroup(SelectedGroupActor, InSelectionSet, InSelectionOptions, /*bForce*/true);
		}
		// Select/Deselect this actor's entire group, starting from the top locked group
		else if (AGroupActor* ActorLockedRootGroup = AGroupActor::GetRootForActor(Actor, true))
		{
			bSelectionChanged |= SelectActorGroup(ActorLockedRootGroup, InSelectionSet, InSelectionOptions, /*bForce*/false);
		}
	}

	// Select the desired actor
	{
		TTypedElement<UTypedElementSelectionInterface> ActorSelectionHandle = InSelectionSet->GetElement<UTypedElementSelectionInterface>(UEngineElementsLibrary::AcquireEditorActorElementHandle(Actor));
		if (!ActorSelectionHandle.SelectElement(InSelectionSet, InSelectionOptions))
		{
			return bSelectionChanged;
		}
	}

	UE_LOG(LogActorLevelEditorSelection, Verbose, TEXT("Selected Actor: %s"), *Actor->GetClass()->GetName());

	// Update the annotation state
	GSelectedActorAnnotation.Set(Actor);
	
	// Bind the override delegates for the components on the selected actor
	for (UActorComponent* Component : Actor->GetComponents())
	{
		if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
		{
			FComponentEditorUtils::BindComponentSelectionOverride(SceneComponent, /*bBind*/true);
		}
	}
		
	// Flush some cached data
	GUnrealEd->PostActorSelectionChanged();

	// A fast path to mark selection rather than reconnecting ALL components for ALL actors that have changed state
	Actor->PushSelectionToProxies();

	return true;
}

bool FActorElementLevelEditorSelectionCustomization::DeselectActorElement(const TTypedElement<UTypedElementSelectionInterface>& InActorSelectionHandle, UTypedElementList* InSelectionSet, const FTypedElementSelectionOptions& InSelectionOptions)
{
	AActor* Actor = ActorElementDataUtil::GetActorFromHandleChecked(InActorSelectionHandle);

	if (const IToolkitHost* ToolkitHostPtr = GetToolkitHost())
	{
		// Allow active modes to potentially handle the deselection
		// TODO: Should this pass through the selection set?
		if (ToolkitHostPtr->GetEditorModeManager().IsSelectionHandled(Actor, /*bInSelected*/false))
		{
			return true;
		}
	}

	bool bSelectionChanged = false;

	if (UActorGroupingUtils::IsGroupingActive() && InSelectionOptions.AllowGroups())
	{
		// If this actor is a group, do a group select
		if (AGroupActor* SelectedGroupActor = Cast<AGroupActor>(Actor))
		{
			bSelectionChanged |= DeselectActorGroup(SelectedGroupActor, InSelectionSet, InSelectionOptions, /*bForce*/true);
		}
		// Select/Deselect this actor's entire group, starting from the top locked group
		else if (AGroupActor* ActorLockedRootGroup = AGroupActor::GetRootForActor(Actor, true))
		{
			bSelectionChanged |= DeselectActorGroup(ActorLockedRootGroup, InSelectionSet, InSelectionOptions, /*bForce*/false);
		}
	}

	// Deselect the desired actor
	{
		TTypedElement<UTypedElementSelectionInterface> ActorSelectionHandle = InSelectionSet->GetElement<UTypedElementSelectionInterface>(UEngineElementsLibrary::AcquireEditorActorElementHandle(Actor));
		if (!ActorSelectionHandle.DeselectElement(InSelectionSet, InSelectionOptions))
		{
			return bSelectionChanged;
		}
	}
	
	UE_LOG(LogActorLevelEditorSelection, Verbose, TEXT("Deselected Actor: %s"), *Actor->GetClass()->GetName());
	
	// Update the annotation state
	GSelectedActorAnnotation.Clear(Actor);
	
	// Deselect and unbind the override delegates for the components on the selected actor
	{
		FTypedElementListLegacySyncScopedBatch LegacySyncBatch(InSelectionSet, InSelectionOptions.AllowLegacyNotifications());

		for (UActorComponent* Component : Actor->GetComponents())
		{
			TTypedElement<UTypedElementSelectionInterface> ComponentSelectionHandle = InSelectionSet->GetElement<UTypedElementSelectionInterface>(UEngineElementsLibrary::AcquireEditorComponentElementHandle(Component));
			ComponentSelectionHandle.DeselectElement(InSelectionSet, InSelectionOptions);

			if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
			{
				FComponentEditorUtils::BindComponentSelectionOverride(SceneComponent, /*bBind*/false);
			}
		}
	}

	// Flush some cached data
	GUnrealEd->PostActorSelectionChanged();

	// A fast path to mark selection rather than reconnecting ALL components for ALL actors that have changed state
	Actor->PushSelectionToProxies();

	return true;
}

bool FActorElementLevelEditorSelectionCustomization::SelectActorGroup(AGroupActor* InGroupActor, UTypedElementList* InSelectionSet, const FTypedElementSelectionOptions& InSelectionOptions, const bool bForce)
{
	bool bSelectionChanged = false;

	// Select all actors within the group (if locked or forced)
	if (bForce || InGroupActor->IsLocked())
	{
		FTypedElementListLegacySyncScopedBatch LegacySyncBatch(InSelectionSet, InSelectionOptions.AllowLegacyNotifications());

		const FTypedElementSelectionOptions GroupSelectionOptions = FTypedElementSelectionOptions(InSelectionOptions)
			.SetAllowGroups(false);

		TArray<AActor*> GroupActors;
		InGroupActor->GetGroupActors(GroupActors);
		for (AActor* Actor : GroupActors)
		{
			TTypedElement<UTypedElementSelectionInterface> ActorSelectionHandle = InSelectionSet->GetElement<UTypedElementSelectionInterface>(UEngineElementsLibrary::AcquireEditorActorElementHandle(Actor));
			if (CanSelectActorElement(ActorSelectionHandle, GroupSelectionOptions))
			{
				bSelectionChanged |= SelectActorElement(ActorSelectionHandle, InSelectionSet, GroupSelectionOptions);
			}
		}
	}

	return bSelectionChanged;
}

bool FActorElementLevelEditorSelectionCustomization::DeselectActorGroup(AGroupActor* InGroupActor, UTypedElementList* InSelectionSet, const FTypedElementSelectionOptions& InSelectionOptions, const bool bForce)
{
	bool bSelectionChanged = false;

	// Deselect all actors within the group (if locked or forced)
	if (bForce || InGroupActor->IsLocked())
	{
		FTypedElementListLegacySyncScopedBatch LegacySyncBatch(InSelectionSet, InSelectionOptions.AllowLegacyNotifications());

		const FTypedElementSelectionOptions GroupSelectionOptions = FTypedElementSelectionOptions(InSelectionOptions)
			.SetAllowGroups(false);

		TArray<AActor*> GroupActors;
		InGroupActor->GetGroupActors(GroupActors);
		for (AActor* Actor : GroupActors)
		{
			TTypedElement<UTypedElementSelectionInterface> ActorSelectionHandle = InSelectionSet->GetElement<UTypedElementSelectionInterface>(UEngineElementsLibrary::AcquireEditorActorElementHandle(Actor));
			if (CanDeselectActorElement(ActorSelectionHandle, GroupSelectionOptions))
			{
				bSelectionChanged |= DeselectActorElement(ActorSelectionHandle, InSelectionSet, GroupSelectionOptions);
			}
		}
	}

	return bSelectionChanged;
}
