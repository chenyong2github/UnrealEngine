// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Blueprint.h"

#include "DataPrepRecipe.generated.h"

class AActor;
class UWorld;

UCLASS(Experimental, Blueprintable, BlueprintType, meta = (DisplayName = "Data Preparation Recipe"))
class DATAPREPCORE_API UDataprepRecipe : public UObject
{
	GENERATED_BODY()

public:
	UDataprepRecipe();

	/** Begin UObject override */
	virtual bool IsEditorOnly() const override { return true; }
	/** End UObject override */

	/**
	 * Reset array of assets attached to recipe
	 */
	void ResetAssets()
	{
		Assets.Reset();
	}

	/**
	 * Setter on array of assets held by this object
	 */
	void SetAssets(TArray<TWeakObjectPtr<UObject>> InAssets)
	{
		Assets = MoveTemp(InAssets);
	}

	/**
	 * Returns array of assets attached to recipe.
	 * Usually called after the operations of the recipe have been executed
	 */
	TArray<TWeakObjectPtr<UObject>> GetValidAssets(bool bFlushAssets = true);

	/**
	 * Returns world attached to this recipe
	 */
	UWorld* GetTargetWorld()
	{
		return TargetWorld;
	}

	/**
	 * Attaches a world to this recipe
	 */
	void SetTargetWorld(UWorld* InTargetWorld)
	{
		TargetWorld = InTargetWorld;
	}

public:
	/**
	 * Returns all actors contained in its attached world
	 */
	UFUNCTION(BlueprintCallable, Category = "Dataprep | Query", meta = (HideSelfPin = "true"))
	virtual TArray<AActor*> GetActors() const;

	/**
	 * Returns all valid assets contained in attached world
	 */
	UFUNCTION(BlueprintCallable, Category = "Dataprep | Query", meta = (HideSelfPin = "true"))
	virtual TArray<UObject*> GetAssets() const;

	/**
	 * Function used to trigger the execution of the pipeline
	 * An event node associated to this function must be in the pipeline graph to run it.
	 */
	UFUNCTION(BlueprintImplementableEvent)
	void TriggerPipelineTraversal();

private:
	/**
	 * World attached to this recipe.
	 * All queries on actors will be perform from this world.
	 * Transient, Can be null.
	 */
	UWorld* TargetWorld;

	/**
	 * All queries on assets will be perform from this array.
	 */
	TArray<TWeakObjectPtr<UObject>> Assets;
};
