// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ComputeFramework/ComputeGraph.h"
#include "OptimusTestGraph.generated.h"

/** Class containing a hard coded UComputeGraph for testing. Can be removed when full graph editor is working. */
UCLASS()
class OPTIMUSCORE_API UOptimusTestGraph : public UComputeGraph
{
	GENERATED_BODY()

protected:
	/** Single user selectable kernel. Only works if our hard coded data interfaces support the kernel. */
	UPROPERTY(EditAnywhere, Category = Graph)
	UComputeKernel* Kernel = nullptr;

#if WITH_EDITOR
	//~ Begin UObject Interface.
	void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UObject Interface.

	void BuildTestGraph();
#endif // WITH_EDITOR
};
