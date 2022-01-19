// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmartObjectDefinition.h"

#include "SmartObjectTypes.h"

namespace UE::SmartObject
{
	const FVector DefaultSlotSize(40, 40, 90);
}

bool USmartObjectDefinition::Validate() const
{
	bValid = false;
	if (Slots.Num() == 0)
	{
		UE_LOG(LogSmartObject, Error, TEXT("Need to provide at least one slot definition"));
		return false;
	}

	// Detect null entries in default definitions
	int32 NullEntryIndex;
	if (DefaultBehaviorDefinitions.Find(nullptr, NullEntryIndex))
	{
		UE_LOG(LogSmartObject, Error, TEXT("Null entry found at index %d in default behavior definition list"), NullEntryIndex);
		return false;
	}

	// Detect null entries in slot definitions
	for (int i = 0; i < Slots.Num(); ++i)
	{
		const FSmartObjectSlotDefinition& Slot = Slots[i];
		if (Slot.BehaviorDefinitions.Find(nullptr, NullEntryIndex))
		{
			UE_LOG(LogSmartObject, Error, TEXT("Null definition entry found at index %d in behavior list of slot %d"), i, NullEntryIndex);
			return false;
		}
	}

	// Detect missing definitions in slots if no default one are provided
	if (DefaultBehaviorDefinitions.Num() == 0)
	{
		for (int i = 0; i < Slots.Num(); ++i)
		{
			const FSmartObjectSlotDefinition& Slot = Slots[i];
			if (Slot.BehaviorDefinitions.Num() == 0)
			{
				UE_LOG(LogSmartObject, Error, TEXT("Slot at index %d needs to provide a behavior definition since there is no default one in the SmartObject definition"), i);
				return false;
			}
		}
	}

	bValid = true;
	return true;
}

const USmartObjectBehaviorDefinition* USmartObjectDefinition::GetBehaviorDefinition(const FSmartObjectSlotIndex& SlotIndex,
																			const TSubclassOf<USmartObjectBehaviorDefinition>& DefinitionClass) const
{
	const USmartObjectBehaviorDefinition* Definition = nullptr;
	if (Slots.IsValidIndex(SlotIndex))
	{
		Definition = GetBehaviorDefinitionByType(Slots[SlotIndex].BehaviorDefinitions, DefinitionClass);
	}

	if (Definition == nullptr)
	{
		Definition = GetBehaviorDefinitionByType(DefaultBehaviorDefinitions, DefinitionClass);
	}

	return Definition;
}

FBox USmartObjectDefinition::GetBounds() const
{
	FBox BoundingBox(ForceInitToZero);
	for (const FSmartObjectSlotDefinition& Slot : GetSlots())
	{
		BoundingBox += Slot.Offset + UE::SmartObject::DefaultSlotSize;
		BoundingBox += Slot.Offset - UE::SmartObject::DefaultSlotSize;
	}
	 return BoundingBox;
}

TOptional<FTransform> USmartObjectDefinition::GetSlotTransform(const FTransform& OwnerTransform, const FSmartObjectSlotIndex SlotIndex) const
{
	TOptional<FTransform> Transform;

	if (ensureMsgf(Slots.IsValidIndex(SlotIndex), TEXT("Requesting slot transform for an out of range index: %s"), *LexToString(SlotIndex)))
	{
		const FSmartObjectSlotDefinition& Slot = Slots[SlotIndex];
		Transform = FTransform(Slot.Rotation, Slot.Offset) * OwnerTransform;
	}

	return Transform;
}

const USmartObjectBehaviorDefinition* USmartObjectDefinition::GetBehaviorDefinitionByType(const TArray<USmartObjectBehaviorDefinition*>& BehaviorDefinitions,
																				 const TSubclassOf<USmartObjectBehaviorDefinition>& DefinitionClass)
{
	USmartObjectBehaviorDefinition* const* BehaviorDefinition = BehaviorDefinitions.FindByPredicate([&DefinitionClass](USmartObjectBehaviorDefinition* SlotBehaviorDefinition)
		{
			return SlotBehaviorDefinition != nullptr && SlotBehaviorDefinition->GetClass()->IsChildOf(*DefinitionClass);
		});

	return BehaviorDefinition != nullptr ? *BehaviorDefinition : nullptr;
}

