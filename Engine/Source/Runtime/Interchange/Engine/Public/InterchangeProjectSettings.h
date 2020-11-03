// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "InterchangePipelineBase.h"

#include "InterchangeProjectSettings.generated.h"

UCLASS(config=Engine, meta=(DisplayName=Interchange), MinimalAPI)
class UInterchangeProjectSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Stack the pipeline you want to use to import asset with interchange. The pipelines are execute from top to bottom order. You can order them by using the grip on the left of any pipelines.*/
	UPROPERTY(EditAnywhere, config, Category = Interchange, meta = (DisplayName = "Import pipeline stack"))
	TArray<TSoftClassPtr<UInterchangePipelineBase>> PipelineStack;
};