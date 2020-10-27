// Copyright Epic Games, Inc. All Rights Reserved.

#include "SceneInterface.h"
#include "RenderGraphBuilder.h"

TArray<FPrimitiveComponentId> FSceneInterface::GetScenePrimitiveComponentIds() const
{ 
	return TArray<FPrimitiveComponentId>(); 
}

void FSceneInterface::UpdateAllPrimitiveSceneInfos(FRHICommandListImmediate& RHICmdList, bool bAsyncCreateLPIs)
{
	FMemMark Mark(FMemStack::Get());
	FRDGBuilder GraphBuilder(RHICmdList);
	UpdateAllPrimitiveSceneInfos(GraphBuilder, bAsyncCreateLPIs);
	GraphBuilder.Execute();
}