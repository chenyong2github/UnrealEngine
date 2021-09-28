// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmartObjectComponent.h"
#include "GameFramework/Actor.h"
#include "SmartObjectSubsystem.h"
#include "VisualLogger/VisualLogger.h"

#if WITH_EDITOR
#include "Engine/World.h"
#endif

namespace FSmartObject
{
	const FVector DefaultSlotSize(40, 40, 90);
}

USmartObjectComponent::USmartObjectComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

}

void USmartObjectComponent::OnRegister()
{
	Super::OnRegister();

	UWorld* World = GetWorld();
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

	// We keep registering in non editor build to generate runtime data but this will
	// no longer be required after moving that data to the SmartObjectCollection actor.
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

	UWorld* World = GetWorld();
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
	if (Owner == nullptr)
	{
 		return BoundingBox;
	}

	const FTransform LocalToWorld = Owner->GetTransform();

	for (const FSmartObjectSlot& Slot : GetConfig().GetSlots())
	{
		const FVector SlotWorldLocation = LocalToWorld.TransformPositionNoScale(Slot.Offset);
		BoundingBox += SlotWorldLocation + FSmartObject::DefaultSlotSize;
		BoundingBox += SlotWorldLocation - FSmartObject::DefaultSlotSize;
	}

	return BoundingBox;
}
