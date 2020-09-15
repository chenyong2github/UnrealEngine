// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IAnimBlueprintCompilerHandler.h"
#include "AnimGraphNode_Base.h"

class IAnimBlueprintCompilerCreationContext;
class IAnimBlueprintCompilationContext;
class IAnimBlueprintGeneratedClassCompiledData;

class FAnimBlueprintCompilerHandler_LinkedAnimGraph : public IAnimBlueprintCompilerHandler
{
public:
	FAnimBlueprintCompilerHandler_LinkedAnimGraph(IAnimBlueprintCompilerCreationContext& InCreationContext);

private:
	void PreProcessAnimationNodes(TArrayView<UAnimGraphNode_Base*> InAnimNodes, IAnimBlueprintCompilationContext& InCompilationContext, IAnimBlueprintGeneratedClassCompiledData& OutCompiledData);
};