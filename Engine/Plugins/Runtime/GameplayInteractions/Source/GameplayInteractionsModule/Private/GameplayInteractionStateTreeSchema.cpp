// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayInteractionStateTreeSchema.h"
#include "GameplayInteractionsTypes.h"
#include "SmartObjectRuntime.h"
#include "StateTreeConditionBase.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeTaskBase.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "Subsystems/WorldSubsystem.h"

UGameplayInteractionStateTreeSchema::UGameplayInteractionStateTreeSchema()
	: NamedExternalDataDescs({
		{UE::GameplayInteraction::Names::InteractableActor,			AActor::StaticClass(),								FGuid(TEXT("870E433F-9931-4B95-982B-78B01B63BBD1"))},
		{UE::GameplayInteraction::Names::SmartObjectClaimedHandle,	FSmartObjectClaimHandle::StaticStruct(),			FGuid(TEXT("13BAB427-26DB-4A4A-BD5F-937EDB39F841"))},
		{UE::GameplayInteraction::Names::AbortContext,				FGameplayInteractionAbortContext::StaticStruct(),	FGuid(TEXT("EED35411-85E8-44A0-95BE-6DB5B63F51BC"))}
	})
{
}

bool UGameplayInteractionStateTreeSchema::IsStructAllowed(const UScriptStruct* InScriptStruct) const
{
	return InScriptStruct->IsChildOf(FStateTreeConditionCommonBase::StaticStruct())
			|| InScriptStruct->IsChildOf(FStateTreeEvaluatorCommonBase::StaticStruct())
			|| InScriptStruct->IsChildOf(FStateTreeTaskCommonBase::StaticStruct())
			|| InScriptStruct->IsChildOf(FGameplayInteractionStateTreeTask::StaticStruct());
}

bool UGameplayInteractionStateTreeSchema::IsClassAllowed(const UClass* InClass) const
{
	return IsChildOfBlueprintBase(InClass);
}

bool UGameplayInteractionStateTreeSchema::IsExternalItemAllowed(const UStruct& InStruct) const
{
	return InStruct.IsChildOf(AActor::StaticClass())
			|| InStruct.IsChildOf(UActorComponent::StaticClass())
			|| InStruct.IsChildOf(UWorldSubsystem::StaticClass());
}
