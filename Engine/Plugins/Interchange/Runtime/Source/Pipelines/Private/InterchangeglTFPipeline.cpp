// Copyright Epic Games, Inc. All Rights Reserved. 

#include "InterchangeglTFPipeline.h"

#include "InterchangePipelineLog.h"

#include "InterchangeMeshFactoryNode.h"

void UInterchangeglTFPipeline::ExecutePipeline(UInterchangeBaseNodeContainer* NodeContainer, const TArray<UInterchangeSourceData*>& InSourceDatas)
{
	Super::ExecutePipeline(NodeContainer, InSourceDatas);

	// TODO: Put logic to convert material graphs to material instances when applicable.
}

