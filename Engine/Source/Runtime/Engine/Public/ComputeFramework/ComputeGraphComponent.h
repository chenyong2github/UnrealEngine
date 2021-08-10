// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "ComputeGraphComponent.generated.h"

class UComputeDataProvider;
class UComputeGraph;

/** 
 * Component which holds an instance of a specific context for a UComputeGraph.
 * This object binds a graph to its data providers, and queues the execution. 
 */
UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent))
class ENGINE_API UComputeGraphComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UComputeGraphComponent();

	/** The Compute Graph asset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Compute")
	TObjectPtr<UComputeGraph> ComputeGraph = nullptr;

	/** The bound Data Provider objects. */
	UPROPERTY(Transient)
	TArray< TObjectPtr<UComputeDataProvider> > DataProviders;

	/**
	 * Create the Data Provider objects for the current ComputeGraph.
	 * @param bSetDefaultBindings Attempt to automate setup of the Data Provider objectss based on the current Actor.
	 */
	UFUNCTION(BlueprintCallable, Category = "Compute")
	void CreateDataProviders(bool bSetDefaultBindings);

	/** Queue the graph for execution at the next render update. */
	UFUNCTION(BlueprintCallable, Category = "Compute")
	void QueueExecute();

protected:
	//~ Begin UActorComponent Interface
	void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	void SendRenderDynamicData_Concurrent() override;
	bool ShouldCreateRenderState() const override { return true; }
	//~ End UActorComponent Interface

private:
	bool bValidProviders = false;
};
