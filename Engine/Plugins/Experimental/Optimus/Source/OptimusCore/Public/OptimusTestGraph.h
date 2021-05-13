// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ComputeFramework/ComputeGraph.h"
#include "OptimusTestGraph.generated.h"

/** Class containing a hard coded UComputeGraph for testing. Can be removed when full graph editor is working. */
UCLASS()
class OPTIMUSCORE_API UOptimusTestGraph : public UComputeGraph
{
	GENERATED_BODY()

public:
	/** Single user selectable kernel. Only works if our hard coded data interfaces support the kernel. */
	UPROPERTY(EditAnywhere, Category = Graph)
	UComputeKernel* Kernel = nullptr;

	//~ Begin UObject Interface.
	void PostLoad() override;
#if WITH_EDITOR
	void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End UObject Interface.

private:
	void BuildTestGraph();
};
