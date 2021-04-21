// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimGraphNode_Base.h"
#include "IAnimBlueprintCompilerHandler.h"

class UAnimGraphNode_SaveCachedPose;
class IAnimBlueprintCompilerCreationContext;
class IAnimBlueprintCompilationContext;
class IAnimBlueprintGeneratedClassCompiledData;

// Handler to propogate attributes from outputs to inputs and to build a static debug record of their path through the graph
class FAnimBlueprintCompilerHandler_Attributes : public IAnimBlueprintCompilerHandler
{
public:
	FAnimBlueprintCompilerHandler_Attributes(IAnimBlueprintCompilerCreationContext& InCreationContext);

private:
	void PreProcessAnimationNodes(TArrayView<UAnimGraphNode_Base*> InAnimNodes, IAnimBlueprintCompilationContext& InCompilationContext, IAnimBlueprintGeneratedClassCompiledData& OutCompiledData);
};