// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldConditions/SmartObjectWorldConditionSchema.h"
#include "WorldConditions/SmartObjectWorldConditionBase.h"
#include "SmartObjectSubsystem.h"
#include "WorldConditionContext.h"

USmartObjectWorldConditionSchema::USmartObjectWorldConditionSchema(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	UserActorRef = AddContextDataDesc(TEXT("UserActor"), AActor::StaticClass(), EWorldConditionContextDataType::Dynamic);
	UserTagsRef = AddContextDataDesc(TEXT("UserTags"), FGameplayTagContainer::StaticStruct(), EWorldConditionContextDataType::Dynamic);
	SmartObjectHandleRef = AddContextDataDesc(TEXT("SmartObjectHandle"), FSmartObjectHandle::StaticStruct(), EWorldConditionContextDataType::Persistent);
	SlotHandleRef = AddContextDataDesc(TEXT("SlotHandle"), FSmartObjectSlotHandle::StaticStruct(), EWorldConditionContextDataType::Persistent);
}

bool USmartObjectWorldConditionSchema::IsStructAllowed(const UScriptStruct* InScriptStruct) const
{
	check(InScriptStruct);
	return Super::IsStructAllowed(InScriptStruct)
		|| InScriptStruct->IsChildOf(TBaseStructure<FWorldConditionCommonBase>::Get())
		|| InScriptStruct->IsChildOf(TBaseStructure<FWorldConditionCommonActorBase>::Get())
		|| InScriptStruct->IsChildOf(TBaseStructure<FSmartObjectWorldConditionBase>::Get());
}
