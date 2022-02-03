// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmartObjectComponent.h"
#include "GameFramework/Actor.h"
#include "SmartObjectSubsystem.h"
#include "VisualLogger/VisualLogger.h"

#if WITH_EDITOR
#include "Engine/World.h"
#endif

USmartObjectComponent::USmartObjectComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

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

	/** Do not register components that don't have a valid definition */
	if (!IsValid(DefinitionAsset))
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

	// Note: we don't report error or ensure on missing subsystem since it might happen
	// in various scenarios (e.g. inactive world, AI system is cleaned up before the components gets unregistered, etc.)
	if (USmartObjectSubsystem* Subsystem = USmartObjectSubsystem::GetCurrent(World))
	{
		Subsystem->UnregisterSmartObject(*this);
	}
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

TStructOnScope<FActorComponentInstanceData> USmartObjectComponent::GetComponentInstanceData() const
{
	return MakeStructOnScope<FActorComponentInstanceData, FSmartObjectComponentInstanceData>(this, DefinitionAsset);
}

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
		if (SmartObjectComponent->DefinitionAsset != DefinitionAsset)
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