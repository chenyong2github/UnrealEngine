// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "InterchangePipelineBase.h"
#include "InterchangeSourceData.h"
#include "Nodes/InterchangeBaseNodeContainer.h"

#include "InterchangeglTFPipeline.generated.h"

UCLASS(BlueprintType)
class INTERCHANGEPIPELINES_API UInterchangeglTFPipeline : public UInterchangePipelineBase
{
	GENERATED_BODY()

public:
	// TODO: Put properties to manage the creation or not of material instances instead of material graphs.

protected:
	virtual void ExecutePipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas) override;

	virtual bool CanExecuteOnAnyThread(EInterchangePipelineTask PipelineTask) override
	{
		// This pipeline creates UObjects and assets. Not safe to execute outside of main thread.
		return true;
	}
};