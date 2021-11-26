// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmartObjectDefinition.h"

#include "AIHelpers.h"
#include "SmartObjectTypes.h"

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
		const FSmartObjectSlot& Slot = Slots[i];
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
			const FSmartObjectSlot& Slot = Slots[i];
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

TOptional<FTransform> USmartObjectDefinition::GetSlotTransform(const FTransform& OwnerTransform, const FSmartObjectSlotIndex SlotIndex) const
{
	TOptional<FTransform> Transform;

	if (!ensureMsgf(Slots.IsValidIndex(SlotIndex), TEXT("Requesting slot transform for a slot index out of range: %s"), *SlotIndex.Describe()))
	{
		return Transform;
	}

	const FSmartObjectSlot& Slot = Slots[SlotIndex];
	if (const TOptional<float> Yaw = UE::AI::GetYawFromVector(Slot.Direction))
	{
		const FTransform SlotToComponentTransform(FQuat(FVector::UpVector, Yaw.GetValue()), Slot.Offset);
		Transform = SlotToComponentTransform * OwnerTransform;
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

FString USmartObjectDefinition::Describe() const
{
	return FString::Printf(TEXT("NumSlots=%d NumDefs=%d HasUserFilter=%s HasObjectFilter=%s"),
		Slots.Num(),
		DefaultBehaviorDefinitions.Num(),
		*LexToString(!UserTagFilter.IsEmpty()),
		*LexToString(!ObjectTagFilter.IsEmpty()));
}
