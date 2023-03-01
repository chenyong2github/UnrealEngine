// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayInteractionsTypes.h"
#include "Annotations/SmartObjectSlotEntryAnnotation.h"
#include "SmartObjectSubsystem.h"
#include "StateTreeTask_FindSlotEntryForActor.generated.h"

class USmartObjectSubsystem;
class UNavigationQueryFilter;

USTRUCT()
struct FStateTreeTask_FindSlotEntryForActor_InstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<AActor> UserActor = nullptr;
	
	/** Slot to use as reference to find the result slot. */
	UPROPERTY(EditAnywhere, Category = "Input")
	FSmartObjectSlotHandle ReferenceSlot;

	UPROPERTY(EditAnywhere, Category = "Output")
	FTransform EntryTransform;

	UPROPERTY(EditAnywhere, Category = "Output")
	FGameplayTag EntryTag;
};

/**
 * Finds entry location for a Smart Object slot. The query will use slot entry annotations as
 * candidates. Each candidate is ranked (e.g. based on distance), and optionally validated to be close to a navigable space.
 */
USTRUCT(meta = (DisplayName = "Find Slot Entry", Category="Gameplay Interactions|Smart Object"))
struct FStateTreeTask_FindSlotEntryForActor : public FGameplayInteractionStateTreeTask
{
	GENERATED_BODY()

	FStateTreeTask_FindSlotEntryForActor();
	
	using FInstanceDataType = FStateTreeTask_FindSlotEntryForActor_InstanceData;

protected:
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

	bool UpdateResult(const FStateTreeExecutionContext& Context) const;

	/** Method to select an entry when multiple entries are present. */
	UPROPERTY(EditAnywhere, Category="Default")
	FSmartObjectSlotEntrySelectionMethod SelectMethod = FSmartObjectSlotEntrySelectionMethod::First;

	/** If true, the result is required to be in or close to a navigable space. */
	UPROPERTY(EditAnywhere, Category="Default")
	bool bRequireResultInNavigableSpace = true;

	/** If true, include slot location as candidate if no entry annotation is present. */
	UPROPERTY(EditAnywhere, Category="Default")
	bool bUseSlotLocationAsFallbackCandidate = false;

	/** If true, entry annotations marked as entry are included as candidates. */
	UPROPERTY(EditAnywhere, Category="Default")
	bool bIncludeEntriesAsCandidates = true;

	/** If true, entry annotations marked as exit are included as candidates. */
	UPROPERTY(EditAnywhere, Category="Default")
	bool bIncludeExistsAsCandidates = true;

	/** Navigation filter to apply to navigation queries. */
	UPROPERTY(EditAnywhere, Category="Default")
	TSubclassOf<UNavigationQueryFilter> NavigationFilter = nullptr;

	/** Defines how far from entry annotation location can be from a navigable location to still considered valid. */
	UPROPERTY(EditAnywhere, Category="Default")
	FVector3f NavigationValidationExtents = FSmartObjectSlotEntryRequest::DefaultValidationExtents;

	/** Local rotation to apply for the entry. E.g. when querying an exit, we might want to rotate in 180 around Z to get the proper exit direction. */
	UPROPERTY(EditAnywhere, Category="Default")
	FRotator3f ResultRotationAdjustment;

	/** Handle to retrieve USmartObjectSubsystem. */
	TStateTreeExternalDataHandle<USmartObjectSubsystem> SmartObjectSubsystemHandle;
};
