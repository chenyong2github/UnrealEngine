// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "InterchangePipelineBase.h"
#include "InterchangePipelineConfigurationBase.h"

#include "InterchangeProjectSettings.generated.h"

USTRUCT()
struct FInterchangePipelineStack
{
	GENERATED_BODY()

	/** The starting mesh for the blueprint **/
	UPROPERTY(EditAnywhere, config, Category = Interchange)
	TArray<TSoftClassPtr<UInterchangePipelineBase>> Pipelines;
};

UCLASS(config=Engine, meta=(DisplayName=Interchange), MinimalAPI)
class UInterchangeProjectSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** All the available pipeline stacks you want to use to import with interchange. The chosen pipeline stack execute all the pipelines from top to bottom order. You can order them by using the grip on the left of any pipelines.*/
	UPROPERTY(EditAnywhere, config, Category = Interchange)
	TMap<FName, FInterchangePipelineStack> PipelineStacks;

	/** This tell interchange which pipeline to select when importing assets.*/
	UPROPERTY(EditAnywhere, config, Category = Interchange)
	FName DefaultPipelineStack;

#if WITH_EDITORONLY_DATA
	/** This tell interchange which pipeline configuration dialog to popup when we need to configure the pipelines.*/
	UPROPERTY(EditAnywhere, config, Category = Interchange)
	TSoftClassPtr <UInterchangePipelineConfigurationBase> PipelineConfigurationDialogClass;

	/** If enabled, the pipeline stacks configuration dialog will show every time interchange must choose a pipeline to import or re-import. If disabled interchange will use the DefaultPipelineStack.*/
	UPROPERTY(EditAnywhere, config, Category = Interchange)
	bool bShowPipelineStacksConfigurationDialog;
#endif

	/** If checked, will use Interchange when importing into level.*/
	UPROPERTY(EditAnywhere, config, Category = "Interchange (Experimental)")
	bool bUseInterchangeWhenImportingIntoLevel;

	/** This tell interchange which pipeline to select when importing scenes.*/
	UPROPERTY(EditAnywhere, config, Category = "Interchange (Experimental)")
	FName DefaultScenePipelineStack;
};