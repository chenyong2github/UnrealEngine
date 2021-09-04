// Copyright Epic Games, Inc. All Rights Reserved.

#include "Restorability/SnapshotRestorability.h"

#include "LevelSnapshotsLog.h"
#include "LevelSnapshotsModule.h"

#include "Components/ActorComponent.h"
#include "Components/BillboardComponent.h"
#include "Engine/LevelScriptActor.h"
#include "GameFramework/Actor.h"
#include "GameFramework/DefaultPhysicsVolume.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/UnrealType.h"
#if WITH_EDITOR
#include "ActorEditorUtils.h"
#endif

namespace
{
	bool DoesActorHaveSupportedClass(const AActor* Actor)
	{
		const TSet<UClass*> UnsupportedClasses = 
		{
			ALevelScriptActor::StaticClass(),		// The level blueprint. Filtered out to avoid external map errors when saving a snapshot.
            ADefaultPhysicsVolume::StaticClass()	// Does not show up in world outliner; always spawned with world.
        };

		for (UClass* Class : UnsupportedClasses)
		{
			if (Actor->IsA(Class))
			{
				return false;
			}
		}
	
		return true;
	}

	bool DoesActorHaveSupportedClassForRemoving(const AActor* Actor)
	{
		const TSet<UClass*> UnsupportedClasses = 
		{
			AWorldSettings::StaticClass()			// Every sublevel creates a world settings. We do not want to be respawning them
		};

		for (UClass* Class : UnsupportedClasses)
		{
			if (Actor->IsA(Class))
			{
				return false;
			}
		}
	
		return true;
	}

	bool DoesComponentHaveSupportedClassForCapture(const UActorComponent* Component)
	{
		const TSet<UClass*> UnsupportedClasses = 
		{
			UBillboardComponent::StaticClass(),		// Attached to all editor actors > It always has a different name so we will never be able to match it.
        };

		for (UClass* Class : UnsupportedClasses)
		{
			if (Component->IsA(Class))
			{
				return false;
			}
		}
	
		return true;
	}
}

bool FSnapshotRestorability::IsActorDesirableForCapture(const AActor* Actor)
{
	SCOPED_SNAPSHOT_CORE_TRACE(IsActorDesirableForCapture);
	
	bool bSomebodyAllowed = false;
	const TArray<TSharedRef<ISnapshotRestorabilityOverrider>>& Overrides = FLevelSnapshotsModule::GetInternalModuleInstance().GetOverrides(); 
	for (const TSharedRef<ISnapshotRestorabilityOverrider>& Override : Overrides)
	{
		const ISnapshotRestorabilityOverrider::ERestorabilityOverride Result = Override->IsActorDesirableForCapture(Actor);
		bSomebodyAllowed = Result == ISnapshotRestorabilityOverrider::ERestorabilityOverride::Allow;
		if (Result == ISnapshotRestorabilityOverrider::ERestorabilityOverride::Disallow)
		{
			return false;
		}
	}

	if (bSomebodyAllowed)
	{
		return true;
	}



	
	return IsValid(Actor)
            && DoesActorHaveSupportedClass(Actor)
            && !Actor->IsTemplate()								// Should never happen, but we never want CDOs
            && !Actor->HasAnyFlags(RF_Transient)				// Don't add transient actors in non-play worlds		
#if WITH_EDITOR
            && Actor->IsEditable()
            && Actor->IsListedInSceneOutliner() 				// Only add actors that are allowed to be selected and drawn in editor
            && !FActorEditorUtils::IsABuilderBrush(Actor)		// Don't add the builder brush
#endif
        ;	
}

bool FSnapshotRestorability::IsComponentDesirableForCapture(const UActorComponent* Component)
{
	SCOPED_SNAPSHOT_CORE_TRACE(IsComponentDesirableForCapture);
	
	bool bSomebodyAllowed = false;
	const TArray<TSharedRef<ISnapshotRestorabilityOverrider>>& Overrides = FLevelSnapshotsModule::GetInternalModuleInstance().GetOverrides(); 
	for (const TSharedRef<ISnapshotRestorabilityOverrider>& Override : Overrides)
	{
		const ISnapshotRestorabilityOverrider::ERestorabilityOverride Result = Override->IsComponentDesirableForCapture(Component);
		bSomebodyAllowed = Result == ISnapshotRestorabilityOverrider::ERestorabilityOverride::Allow;
		if (Result == ISnapshotRestorabilityOverrider::ERestorabilityOverride::Disallow)
		{
			return false;
		}
	}

	if (bSomebodyAllowed)
	{
		return true;
	}
	

	
	// We only support native components or the ones added through the component list in Blueprints for now
	return IsValid(Component) && DoesComponentHaveSupportedClassForCapture(Component)
		&& (Component->CreationMethod == EComponentCreationMethod::Native || Component->CreationMethod == EComponentCreationMethod::SimpleConstructionScript);
}

bool FSnapshotRestorability::IsPropertyBlacklistedForCapture(const FProperty* Property)
{
	SCOPED_SNAPSHOT_CORE_TRACE(IsPropertyBlacklisted);
	return FLevelSnapshotsModule::GetInternalModuleInstance().IsPropertyBlacklisted(Property);
}

bool FSnapshotRestorability::IsPropertyWhitelistedForCapture(const FProperty* Property)
{
	SCOPED_SNAPSHOT_CORE_TRACE(IsPropertyWhitelisted);
	return FLevelSnapshotsModule::GetInternalModuleInstance().IsPropertyWhitelisted(Property);
}

bool FSnapshotRestorability::ShouldConsiderNewActorForRemoval(const AActor* Actor)
{
	return DoesActorHaveSupportedClassForRemoving(Actor) && IsActorDesirableForCapture(Actor);
}

bool FSnapshotRestorability::IsRestorableProperty(const FProperty* LeafProperty)
{
	SCOPED_SNAPSHOT_CORE_TRACE(IsRestorableProperty);
	
	// Subobjects are currently not supported
	const uint64 InstancedFlags = CPF_InstancedReference | CPF_ContainsInstancedReference | CPF_PersistentInstance;
	
	// Deprecated and transient properties should not cause us to consider the property different because we do not save these properties.
	const uint64 UnsavedProperties = CPF_Deprecated | CPF_Transient;
	// Property is not editable in details panels
	const int64 UneditableFlags = CPF_DisableEditOnInstance;
	
	
	// Only consider editable properties
	const uint64 RequiredFlags = CPF_Edit;

	FLevelSnapshotsModule& Module = FLevelSnapshotsModule::GetInternalModuleInstance();
	const bool bIsWhitelisted = Module.IsPropertyWhitelisted(LeafProperty);
	const bool bIsBlacklisted = Module.IsPropertyBlacklisted(LeafProperty);
	const bool bPassesDefaultChecks =
		!LeafProperty->HasAnyPropertyFlags(UnsavedProperties | InstancedFlags | UneditableFlags)
        && LeafProperty->HasAllPropertyFlags(RequiredFlags);
	
	return bIsWhitelisted || (!bIsBlacklisted && bPassesDefaultChecks);
}
