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
