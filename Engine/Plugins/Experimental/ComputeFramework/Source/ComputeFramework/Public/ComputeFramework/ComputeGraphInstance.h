// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComputeGraphInstance.generated.h"

class FSceneInterface;
class UComputeDataProvider;
class UComputeGraph;

/** 
 * Class to store a set of data provider bindings for UComputeGraph and to
 * enqueue work to the ComputeFramework's compute system.
 */
USTRUCT()
struct COMPUTEFRAMEWORK_API FComputeGraphInstance
{
	GENERATED_USTRUCT_BODY();

public:
	/** Create the Data Provider objects for the ComputeGraph. */
	void CreateDataProviders(UComputeGraph* InComputeGraph, UObject* InBindingObject);

	/** Create the Data Provider objects. */
	void DestroyDataProviders();

	/** Returns true if the Data Provider objects are all created and valid. */
	bool ValidateDataProviders(UComputeGraph* InComputeGraph) const;

	/** Enqueue the ComputeGraph work. */
	bool EnqueueWork(UComputeGraph* InComputeGraph, FSceneInterface const* Scene);

private:
	/** The currently bound Data Provider objects. */
	UPROPERTY(Transient)
	TArray< TObjectPtr<UComputeDataProvider> > DataProviders;
};
