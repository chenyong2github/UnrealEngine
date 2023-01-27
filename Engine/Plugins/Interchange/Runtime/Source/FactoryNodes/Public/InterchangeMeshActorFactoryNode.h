// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InterchangeActorFactoryNode.h"

#include "InterchangeMeshActorFactoryNode.generated.h"

UCLASS(BlueprintType, Experimental)
class INTERCHANGEFACTORYNODES_API UInterchangeMeshActorFactoryNode : public UInterchangeActorFactoryNode
{
	GENERATED_BODY()

public:
	UInterchangeMeshActorFactoryNode();

	/**
	 * Override serialize to restore SlotMaterialDependencies on load.
	 */
	virtual void Serialize(FArchive& Ar) override
	{
		Super::Serialize(Ar);

		if (Ar.IsLoading() && bIsInitialized)
		{
			SlotMaterialDependencies.RebuildCache();
		}
	}

	/**
	 * Allow to retrieve the correspondence table between slot names and assigned materials for this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	void GetSlotMaterialDependencies(TMap<FString, FString>& OutMaterialDependencies) const;

	/**
	 * Allow to retrieve one Material dependency for a given slot of this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	bool GetSlotMaterialDependencyUid(const FString& SlotName, FString& OutMaterialDependency) const;

	/**
	 * Add one Material dependency to a specific slot name of this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	bool SetSlotMaterialDependencyUid(const FString& SlotName, const FString& MaterialDependencyUid);

	/**
	 * Remove the Material dependency associated with the given slot name from this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	bool RemoveSlotMaterialDependencyUid(const FString& SlotName);

	/** Set MorphTarget with given weight. */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool SetMorphTargetCurveWeight(const FString& MorphTargetName, const float& Weight);

	/** Set MorphTarget with given weight. */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	void SetMorphTargetCurveWeights(const TMap<FString, float>& InMorphTargetCurveWeights);

	/** Get MorphTargets and their weights. */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	void GetMorphTargetCurveWeights(TMap<FString, float>& OutMorphTargetCurveWeights) const;

private:
	UE::Interchange::TMapAttributeHelper<FString, FString> SlotMaterialDependencies;
	UE::Interchange::TMapAttributeHelper<FString, float> MorphTargetCurveWeights;
};