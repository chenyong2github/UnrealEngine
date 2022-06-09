// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ComputeFramework/ShaderParamTypeDefinition.h"
#include "ComputeSource.generated.h"

/** 
 * Class representing some source for inclusion in a UComputeKernel.
 * Source can be created by different authoring mechanisms. (HLSL text, VPL graph, ML Meta Lang, etc.)
 */
UCLASS(Abstract)
class COMPUTEFRAMEWORK_API UComputeSource : public UObject
{
	GENERATED_BODY()

public:
	/** Get source code. */
	virtual FString GetSource() const { return FString(); }
	/** Get an array of source objects. This allows us to specify source dependencies. */
	virtual void GetAdditionalSources(TArray<UComputeSource*>& OutSources) const {};
};
