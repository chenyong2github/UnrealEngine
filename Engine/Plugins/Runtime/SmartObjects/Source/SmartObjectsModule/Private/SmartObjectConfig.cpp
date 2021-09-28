// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmartObjectConfig.h"

#include "AIHelpers.h"
#include "SmartObjectTypes.h"
#include "Templates/SubclassOf.h"

bool FSmartObjectConfig::Validate() const
{
	if (Slots.Num() == 0)
	{
		UE_LOG(LogSmartObject, Error, TEXT("Need to provide at least one slot configuration"));
		return false;
	}

	int32 NullEntryIndex;
	const bool bNullEntryFound = DefaultBehaviorConfigurations.Find(nullptr, NullEntryIndex);
	if (bNullEntryFound)
	{
		UE_LOG(LogSmartObject, Error, TEXT("Null config entry found at index %d in default behavior list"), NullEntryIndex);
		return false;
	}

	if (DefaultBehaviorConfigurations.Num() == 0)
	{
		for (int i = 0; i < Slots.Num(); ++i)
		{
			const FSmartObjectSlot& Slot = Slots[i];
			if (Slot.BehaviorConfigurations.Num() == 0)
			{
				UE_LOG(LogSmartObject, Error, TEXT("Slot at index %d needs to provide a behavior since there is no default behavior in the config"), i);
				return false;
			}

			int32 NullSlotEntryIndex;
			const bool bNullSlotEntryFound = DefaultBehaviorConfigurations.Find(nullptr, NullSlotEntryIndex);
			if (bNullSlotEntryFound)
			{
				UE_LOG(LogSmartObject, Error, TEXT("Null config entry found at index %d in behavior list of slot %d"), i, NullSlotEntryIndex);
				return false;
			}
		}
	}

	return true;
}

const USmartObjectBehaviorConfigBase* FSmartObjectConfig::GetBehaviorConfig(const FSmartObjectSlotIndex& SlotIndex,
																			const TSubclassOf<USmartObjectBehaviorConfigBase>& ConfigurationClass) const
{
	const USmartObjectBehaviorConfigBase* Config = nullptr;
	if (Slots.IsValidIndex(SlotIndex))
	{
		Config = GetBehaviorConfigByType(Slots[SlotIndex].BehaviorConfigurations, ConfigurationClass);
	}

	if (Config == nullptr)
	{
		Config = GetBehaviorConfigByType(DefaultBehaviorConfigurations, ConfigurationClass);
	}

	return Config;
}

TOptional<FTransform> FSmartObjectConfig::GetSlotTransform(const FTransform& OwnerTransform, const FSmartObjectSlotIndex SlotIndex) const
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

const USmartObjectBehaviorConfigBase* FSmartObjectConfig::GetBehaviorConfigByType(const TArray<USmartObjectBehaviorConfigBase*>& BehaviorConfigs,
																				 const TSubclassOf<USmartObjectBehaviorConfigBase>& ConfigurationClass)
{
	USmartObjectBehaviorConfigBase* const* FoundConfig = BehaviorConfigs.FindByPredicate([&ConfigurationClass](USmartObjectBehaviorConfigBase* SlotConfig)
		{
			return SlotConfig != nullptr && SlotConfig->GetClass()->IsChildOf(*ConfigurationClass);
		});

	return FoundConfig != nullptr ? *FoundConfig : nullptr;
}

FString FSmartObjectConfig::Describe() const
{
	return FString::Printf(TEXT("NumSlots=%d NumCfgs=%d HasUserFilter=%s HasObjectFilter=%s"), 
		Slots.Num(),
		DefaultBehaviorConfigurations.Num(),
		*LexToString(!UserTagFilter.IsEmpty()),
		*LexToString(!ObjectTagFilter.IsEmpty()));
}
