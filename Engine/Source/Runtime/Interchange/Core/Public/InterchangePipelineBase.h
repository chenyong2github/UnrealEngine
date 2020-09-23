// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "InterchangeSourceData.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "InterchangePipelineBase.generated.h"

UCLASS(BlueprintType, Blueprintable)
class INTERCHANGECORE_API UInterchangePipelineBase : public UObject
{
	GENERATED_BODY()

public:

	/**
	 * Non virtual helper to allow blueprint to implement event base function to implement a pipeline,
	 * the Interchange manager is calling this function not the virtual one that is call by the default implementation.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interchange | Translator")
	bool ScriptedExecuteImportPipeline(UInterchangeBaseNodeContainer* BaseNodeContainerAdapter);
	/** The default implementation (call if the blueprint do not have any implementation) will call the virtual ExecuteImportPipeline */
	bool ScriptedExecuteImportPipeline_Implementation(UInterchangeBaseNodeContainer* BaseNodeContainerAdapter)
	{
		//By default we call the virtual import pipeline execution
		return ExecuteImportPipeline(BaseNodeContainerAdapter);
	}

	/**
	 * Non virtual helper to allow blueprint to implement event base function let the interchange know if it can run asynchronously.
	 * the Interchange manager is calling this function not the virtual one that is call by the default implementation.
	 */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Interchange | Translator")
	bool ScriptedExecuteExportPipeline(UInterchangeBaseNodeContainer* BaseNodeContainerAdapter);

	/** The default implementation (call if the blueprint do not have any implementation) will call the virtual ExecuteExportPipeline */
	bool ScriptedExecuteExportPipeline_Implementation(UInterchangeBaseNodeContainer* BaseNodeContainerAdapter)
	{
		return ExecuteExportPipeline(BaseNodeContainerAdapter);
	}

	/**
	 * Non virtual helper to allow blueprint to implement event base function let the interchange know if it can run asynchronously.
	 * the Interchange manager is calling this function not the virtual one that is call by the default implementation.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interchange | Translator")
	bool ScriptedCanExecuteOnAnyThread();

	/** The default implementation (call if the blueprint do not have any implementation) will call the virtual CanExecuteAsync */
	bool ScriptedCanExecuteOnAnyThread_Implementation()
	{
		return CanExecuteOnAnyThread();
	}
protected:

	/**
	 * This function can modify the BaseNodeContainer to create a pipeline that will set the graph and the nodes options has it want it to be imported by the factories
	 * The interchange manager is not calling this function directly. It is calling the blueprint native event in case this object is a blueprint derive object.
	 * By default the scripted implementation is calling this virtual pipeline.
	 */
	virtual bool ExecuteImportPipeline(UInterchangeBaseNodeContainer* BaseNodeContainerAdapter)
	{
		return false;
	}

	/**
	 * This function tell the interchange manager if we can execute this pipeline in async mode. If it return false, the ScriptedExecuteImportPipeline
	 * will be call on the main thread (GameThread), if true it will be run in a background thread and possibly in parallel. If there is multiple
	 * import process in same time.
	 *
	 */
	virtual bool CanExecuteOnAnyThread()
	{
		return true;
	}

	/** This function can modify the BaseNodeContainer to create a pipeline that will set/validate the graph nodes hierarchy and options.*/
	virtual bool ExecuteExportPipeline(UInterchangeBaseNodeContainer* BaseNodeContainerAdapter)
	{
		return false;
	}
};
