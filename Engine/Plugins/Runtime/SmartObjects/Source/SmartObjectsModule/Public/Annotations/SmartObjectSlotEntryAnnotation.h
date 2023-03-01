// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SmartObjectAnnotation.h"
#include "GameplayTagContainer.h"
#include "SmartObjectSlotEntryAnnotation.generated.h"

/**
 * Annotation to define a navigation entry for a Smart Object Slot.
 * This can be used to add multiple entry points to a slot, or to validate the entries against navigation data. 
 */
USTRUCT()
struct SMARTOBJECTSMODULE_API FSmartObjectSlotEntryAnnotation : public FSmartObjectSlotAnnotation
{
	GENERATED_BODY()

	FSmartObjectSlotEntryAnnotation()
		: bIsEntry(true)
		, bIsExit(true)
	{
	}

#if WITH_EDITOR
	virtual void DrawVisualization(FSmartObjectVisualizationContext& VisContext) const override;
	virtual void DrawVisualizationHUD(FSmartObjectVisualizationContext& VisContext) const override;
	virtual void AdjustWorldTransform(const FTransform& SlotTransform, const FVector& DeltaTranslation, const FRotator& DeltaRotation) override;
#endif
	
	virtual TOptional<FTransform> GetWorldTransform(const FTransform& SlotTransform) const override;
	virtual FVector GetWorldLocation(const FTransform& SlotTransform) const;
	virtual FRotator GetWorldRotation(const FTransform& SlotTransform) const;

#if WITH_GAMEPLAY_DEBUGGER
	virtual void CollectDataForGameplayDebugger(FGameplayDebuggerCategory& Category, const FTransform& SlotTransform, const FVector ViewLocation, const FVector ViewDirection, AActor* DebugActor) const;
#endif // WITH_GAMEPLAY_DEBUGGER	

	/** Local space offset of the entry. */
	UPROPERTY(EditAnywhere, Category="Default")
	FVector3f Offset = FVector3f(0.f);

	/** Local space rotation of the entry. */
	UPROPERTY(EditAnywhere, Category="Default")
	FRotator3f Rotation = FRotator3f(0.f);

	/** Tag that can be used to identify the entry. */
	UPROPERTY(EditAnywhere, Category="Default")
	FGameplayTag Tag;

	/** Set to true if the entry can be used to enter the slot. */
	UPROPERTY(EditAnywhere, Category="Default")
	uint8 bIsEntry : 1;

	/** Set to true if the entry can be used to exit the slot. */
	UPROPERTY(EditAnywhere, Category="Default")
	uint8 bIsExit : 1;
};
