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
class UComputeGraphComponent : public UActorComponent
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
	 * Set a Data Provider object to be available on the next graph execution. 
	 * todo[CF]: Can we automate the setup of data providers from information in the graph and other metadata?
	 */
	UFUNCTION(BlueprintCallable, Category = "Compute")
	void SetDataProvider(int32 Index, UComputeDataProvider* DataProvider);

	/** Queue the graph for execution at the next render update. */
	UFUNCTION(BlueprintCallable, Category = "Compute")
	void QueueExecute();

protected:
	//~ Begin UActorComponent Interface
	void SendRenderDynamicData_Concurrent() override;
	bool ShouldCreateRenderState() const override {	return true; }
	//~ End UActorComponent Interface
};
