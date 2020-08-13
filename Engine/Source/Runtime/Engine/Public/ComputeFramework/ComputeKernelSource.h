// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ComputeFramework/ComputeKernelPermutationSet.h"
#include "ComputeKernelSource.generated.h"

class FComputeKernelResource;

UCLASS(Abstract, EditInlineNew)
class UComputeKernelSource : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, meta = (ShowOnlyInnerProperties), Category = "Kernel")
	FComputeKernelPermutationSet PermutationSet;

	UPROPERTY(VisibleAnywhere, meta = (ShowOnlyInnerProperties), Category = "Kernel")
	FComputeKernelDefinitionsSet DefinitionsSet;
};
