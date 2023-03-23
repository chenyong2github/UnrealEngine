// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Templates/SubclassOf.h"
#include "NavAreas/NavArea.h"
#include "NavRelevantComponent.h"
#include "NavModifierComponent.generated.h"

struct FNavigationRelevantData;

UCLASS(ClassGroup = (Navigation), meta = (BlueprintSpawnableComponent), hidecategories = (Activation), config = Engine, defaultconfig)
class NAVIGATIONSYSTEM_API UNavModifierComponent : public UNavRelevantComponent
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Navigation)
	TSubclassOf<UNavArea> AreaClass;

	/** box extent used ONLY when owning actor doesn't have collision component */
	UPROPERTY(EditAnywhere, Category = Navigation)
	FVector FailsafeExtent;

	/** Experimental: Indicates which navmesh resolution should be used around the actor. */
	UPROPERTY(EditAnywhere, Category = Navigation, AdvancedDisplay)
	ENavigationDataResolution NavMeshResolution;

	/** Setting to 'true' will result in expanding lower bounding box of the nav 
	 *	modifier by agent's height, before applying to navmesh */
	UPROPERTY(config, EditAnywhere, Category = Navigation)
	uint8 bIncludeAgentHeight : 1;

	virtual void CalcAndCacheBounds() const override;
	virtual void GetNavigationData(FNavigationRelevantData& Data) const override;

	UFUNCTION(BlueprintCallable, Category = "AI|Navigation")
	void SetAreaClass(TSubclassOf<UNavArea> NewAreaClass);

protected:
	void OnTransformUpdated(USceneComponent* RootComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport);

#if WITH_EDITOR
	void OnNavAreaRegistered(const UWorld& World, const UClass* NavAreaClass);
	void OnNavAreaUnregistered(const UWorld& World, const UClass* NavAreaClass);
#endif // WITH_EDITOR 

	//~ Begin UActorComponent Interface
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	//~ End UActorComponent Interface

	struct FRotatedBox
	{
		FBox Box;
		FQuat Quat;

		FRotatedBox() {}
		FRotatedBox(const FBox& InBox, const FQuat& InQuat) : Box(InBox), Quat(InQuat) {}
	};

	mutable TArray<FRotatedBox> ComponentBounds;
	mutable FDelegateHandle TransformUpdateHandle;
	/** cached in CalcAndCacheBounds and tested in GetNavigationData to see if
	 *	cached data is still valid */
	mutable FTransform CachedTransform;

#if WITH_EDITOR
	FDelegateHandle OnNavAreaRegisteredDelegateHandle;
	FDelegateHandle OnNavAreaUnregisteredDelegateHandle;
#endif // WITH_EDITOR 
};
