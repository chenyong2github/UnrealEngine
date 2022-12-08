// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmartObjectComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "GameFramework/Actor.h"
#include "SmartObjectSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SmartObjectComponent)

#if WITH_EDITOR
#include "Engine/World.h"
#endif

#if WITH_EDITORONLY_DATA
USmartObjectComponent::FOnSmartObjectChanged USmartObjectComponent::OnSmartObjectChanged;
#endif // WITH_EDITORONLY_DATA

USmartObjectComponent::USmartObjectComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

}

void USmartObjectComponent::PostInitProperties()
{
	Super::PostInitProperties();
#if WITH_EDITORONLY_DATA
	if (HasAnyFlags(RF_ClassDefaultObject) == false)
	{
		if (AActor* Actor = GetOwner())
		{
			// tagging owner actors since the tags get included in FWorldPartitionActorDesc 
			// and that's the only way we can tell a given actor has a SmartObjectComponent 
			// until it's fully loaded
			if (Actor->Tags.Contains(UE::SmartObjects::WithSmartObjectTag) == false)
			{
				Actor->Tags.AddUnique(UE::SmartObjects::WithSmartObjectTag);
				Actor->MarkPackageDirty();
			}
		}
	}
#endif // WITH_EDITORONLY_DATA
}

void USmartObjectComponent::OnRegister()
{
	Super::OnRegister();

	RegisterToSubsystem();
}

void USmartObjectComponent::RegisterToSubsystem()
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

#if WITH_EDITOR
	// Do not process any component registered to preview world
	if (World->WorldType == EWorldType::EditorPreview)
	{
		return;
	}
#endif

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}

	// Note: we don't report error or ensure on missing subsystem since it might happen
	// in various scenarios (e.g. inactive world)
	if (USmartObjectSubsystem* Subsystem = USmartObjectSubsystem::GetCurrent(World))
	{
		Subsystem->RegisterSmartObject(*this);
	}
}

void USmartObjectComponent::OnUnregister()
{
	Super::OnUnregister();

	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

#if WITH_EDITOR
	// Do not process any component registered to preview world
	if (World->WorldType == EWorldType::EditorPreview)
	{
		return;
	}
#endif

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}

	/** Do not register components that don't have a valid definition */
	if (!IsValid(DefinitionAsset))
	{
		return;
	}

	if (GetRegisteredHandle().IsValid())
	{	
		if (USmartObjectSubsystem* Subsystem = USmartObjectSubsystem::GetCurrent(World))
		{
			if (!IsBeingDestroyed())
			{
				Subsystem->UnregisterSmartObject(*this);
			}
			else
			{
				// note that this case is really only expected in the editor when the component is being unregistered 
				// as part of DestroyComponent. In default game flow EndPlay will get called first and once we make 
				// it here the RegisteredHandle should already be Invalid
				Subsystem->RemoveSmartObject(*this);
			}
		}
	}

	ensureMsgf(!OnComponentTagsModifiedHandle.IsValid(), TEXT("AbilitySystemComponent delegate is expected to be unbound after unregistration"));
	ensureMsgf(!bInstanceTagsDelegateBound, TEXT("SmartObject runtime instance delegate is expected to be unbound after unregistration"));
}

void USmartObjectComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bPendingRuntimeInstanceBinding && RegisteredHandle.IsValid())
	{
		if (USmartObjectSubsystem* Subsystem = USmartObjectSubsystem::GetCurrent(GetWorld()))
		{
			Subsystem->BindComponentToSimulation(*this);
		}
	}
	bPendingRuntimeInstanceBinding = false;
}

void USmartObjectComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{	
	if (EndPlayReason == EEndPlayReason::Destroyed && GetRegisteredHandle().IsValid())
	{
		if (USmartObjectSubsystem* Subsystem = USmartObjectSubsystem::GetCurrent(GetWorld()))
		{
			Subsystem->RemoveSmartObject(*this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

FBox USmartObjectComponent::GetSmartObjectBounds() const
{
	FBox BoundingBox(ForceInitToZero);

	const AActor* Owner = GetOwner();
	if (Owner != nullptr && DefinitionAsset != nullptr)
	{
		BoundingBox = DefinitionAsset->GetBounds().TransformBy(Owner->GetTransform());
	}

	return BoundingBox;
}

void USmartObjectComponent::SetRegisteredHandle(const FSmartObjectHandle Value, const ESmartObjectRegistrationType InRegistrationType)
{
	ensure(Value.IsValid());
	ensure(RegisteredHandle.IsValid() == false || RegisteredHandle == Value);
	RegisteredHandle = Value;
	ensure(RegistrationType == ESmartObjectRegistrationType::None && InRegistrationType != ESmartObjectRegistrationType::None);
	RegistrationType = InRegistrationType;
}

void USmartObjectComponent::InvalidateRegisteredHandle()
{
	RegisteredHandle = FSmartObjectHandle::Invalid;
	RegistrationType = ESmartObjectRegistrationType::None;
}

void USmartObjectComponent::OnRuntimeInstanceBound(FSmartObjectRuntime& RuntimeInstance)
{
	if (HasBegunPlay() == false)
	{
		// not processing this call since the Owner might still get some components registered. We'll retry in BeginPlay
		bPendingRuntimeInstanceBinding = true;
		return;
	}
	
	if (UAbilitySystemComponent* AbilityComponent = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner()))
	{
		BindTagsDelegates(RuntimeInstance, *AbilityComponent);

		if (RegistrationType == ESmartObjectRegistrationType::WithCollection)
		{
			// Update component using the tags from the instance since the component might get reloaded while the instance
			// was still part of the simulation (i.e. persistent). In this case we need to apply the most up to date
			// tag counts to the component. Unfortunately there is no way at the moment to replace all tags in one go
			// so update each tag count individually.
			const FGameplayTagContainer& InstanceTags = RuntimeInstance.GetTags();
			FGameplayTagContainer AbilityComponentTags;
			AbilityComponent->GetOwnedGameplayTags(AbilityComponentTags);

			// Adjust count of any existing and add the missing ones
			for (auto It(InstanceTags.CreateConstIterator()); It; ++It)
			{
				AbilityComponentTags.RemoveTag(*It);
				AbilityComponent->SetTagMapCount(*It, 1);
			}

			// Remove all remaining tags that are no longer valid
			for (auto It(AbilityComponentTags.CreateConstIterator()); It; ++It)
			{
				AbilityComponent->SetTagMapCount(*It, 0);
			}
		}
	}

	bPendingRuntimeInstanceBinding = false;
}

void USmartObjectComponent::OnRuntimeInstanceUnbound(FSmartObjectRuntime& RuntimeInstance)
{
	UnbindComponentTagsDelegate();
	UnbindRuntimeInstanceTagsDelegate(RuntimeInstance);
}

void USmartObjectComponent::OnRuntimeInstanceDestroyed()
{
	UnbindComponentTagsDelegate();

	// No need to try to unbind the Runtime instance delegate since it was destroyed.
	// Simply invalidate our handle.
	bInstanceTagsDelegateBound = false;
}

void USmartObjectComponent::BindTagsDelegates(FSmartObjectRuntime& RuntimeInstance, UAbilitySystemComponent& AbilitySystemComponent)
{
	USmartObjectSubsystem* Subsystem = USmartObjectSubsystem::GetCurrent(GetWorld());

	if (Subsystem != nullptr)
	{
		// Register callback when Tags in the component are modified to mirror the change in the Runtime instance
		OnComponentTagsModifiedHandle = AbilitySystemComponent.RegisterGenericGameplayTagEvent().AddLambda
		([Subsystem, Handle = RuntimeInstance.GetRegisteredHandle()](const FGameplayTag Tag, const int32 NewCount)
		{
			// This specific delegate is only invoked whenever a tag is added or removed (but not if just count is increased)
			// so we can add or remove the tag on the instance (no reference counting)
			if (NewCount)
			{
				Subsystem->AddTagToInstance(Handle, Tag);
			}
			else
			{
				Subsystem->RemoveTagFromInstance(Handle, Tag);
			}
		});

		// Register callback when Tags in the Runtime instance are modified to mirror the change in the component
		// The lambda capture assumes that the AbilitySystemComponent has the same lifetime as the current SmartObjectComponent
		RuntimeInstance.GetTagChangedDelegate().BindLambda
		([&AbilitySystemComponent](const FGameplayTag Tag, const int32 NewCount)
		{
			AbilitySystemComponent.SetTagMapCount(Tag, NewCount);
		});
		bInstanceTagsDelegateBound = true;
	}
}

void USmartObjectComponent::UnbindComponentTagsDelegate()
{
	if (OnComponentTagsModifiedHandle.IsValid())
	{
		if (UAbilitySystemComponent* AbilityComponent = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner()))
		{
			AbilityComponent->RegisterGenericGameplayTagEvent().Remove(OnComponentTagsModifiedHandle);
		}
		OnComponentTagsModifiedHandle.Reset();
	}
}

void USmartObjectComponent::UnbindRuntimeInstanceTagsDelegate(FSmartObjectRuntime& RuntimeInstance)
{
	if (bInstanceTagsDelegateBound)
	{
		RuntimeInstance.GetTagChangedDelegate().Unbind();
		bInstanceTagsDelegateBound = false;
	}
}

TStructOnScope<FActorComponentInstanceData> USmartObjectComponent::GetComponentInstanceData() const
{
	return MakeStructOnScope<FActorComponentInstanceData, FSmartObjectComponentInstanceData>(this, DefinitionAsset);
}

#if WITH_EDITOR
void USmartObjectComponent::PostEditUndo()
{
	Super::PostEditUndo();

	OnSmartObjectChanged.Broadcast(*this);
}

void USmartObjectComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	OnSmartObjectChanged.Broadcast(*this);
}
#endif // WITH_EDITOR

//-----------------------------------------------------------------------------
// FSmartObjectComponentInstanceData
//-----------------------------------------------------------------------------
bool FSmartObjectComponentInstanceData::ContainsData() const
{
	return true;
}

void FSmartObjectComponentInstanceData::ApplyToComponent(UActorComponent* Component, const ECacheApplyPhase CacheApplyPhase)
{
	// Apply data first since we might need to register to the subsystem
	// before the component gets re-registered by the base class.
	if (CacheApplyPhase == ECacheApplyPhase::PostUserConstructionScript)
	{
		USmartObjectComponent* SmartObjectComponent = CastChecked<USmartObjectComponent>(Component);
		// We only need to force a register if DefinitionAsset is currently null and a valid one was backed up.
		// Reason is that our registration to the Subsystem depends on a valid definition so it can be skipped.
		if (SmartObjectComponent->DefinitionAsset != DefinitionAsset && SmartObjectComponent->DefinitionAsset == nullptr)
		{
			SmartObjectComponent->DefinitionAsset = DefinitionAsset;
			// Registering to the subsystem should only be attempted on registered component
			// otherwise the OnRegister callback will take care of it.
			if (SmartObjectComponent->IsRegistered())
			{
				SmartObjectComponent->RegisterToSubsystem();
			}
		}
	}

	Super::ApplyToComponent(Component, CacheApplyPhase);
}

