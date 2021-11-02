// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "InterchangePipelineConfigurationBase.generated.h"

UENUM(BlueprintType, Experimental)
enum class EInterchangePipelineConfigurationDialogResult : uint8
{
	Cancel		UMETA(DisplayName = "Cancel"),
	Import		UMETA(DisplayName = "Import"),
	ImportAll	UMETA(DisplayName = "Import All"),
};

UCLASS(BlueprintType, Blueprintable, Experimental)
class INTERCHANGEENGINE_API UInterchangePipelineConfigurationBase : public UObject
{
	GENERATED_BODY()

public:

	/**
	 * Non virtual helper to allow blueprint to implement event base function to implement the ShowPipelineConfigurationDialog,
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interchange | Translator")
	EInterchangePipelineConfigurationDialogResult ScriptedShowPipelineConfigurationDialog();
	/** The default implementation (call if the blueprint do not have any implementation) will call the virtual ExecuteImportPipeline */
	EInterchangePipelineConfigurationDialogResult ScriptedShowPipelineConfigurationDialog_Implementation()
	{
		//By default we call the virtual import pipeline execution
		return ShowPipelineConfigurationDialog();
	}

protected:

	/**
	 * This function show a dialog use to configure pipeline stacks and return a stack name that tell the caller the user choice.
	 */
	virtual EInterchangePipelineConfigurationDialogResult ShowPipelineConfigurationDialog()
	{ 
		//Not implemented
		return EInterchangePipelineConfigurationDialogResult::Cancel;
	}
};
