// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Nodes/InterchangeFactoryBaseNode.h"
#include "InterchangeResultsContainer.h"
#include "InterchangeSourceData.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "InterchangePipelineBase.generated.h"

UENUM(BlueprintType)
enum class EInterchangePipelineTask : uint8
{
	PreFactoryImport,
	PostFactoryImport,
	Export
};

UENUM()
enum class EInterchangeReimportType : uint8
{
	AssetReimport,
	SceneReimport,
	AssetCustomLODImport, //The import for custom LOD is there because we use a copy of the asset import data pipeline stack.
	AssetCustomLODReimport,
	AssetAlternateSkinningImport, //The import for custom LOD is there because we use a copy of the asset import data pipeline stack.
	AssetAlternateSkinningReimport,
};

UCLASS(BlueprintType, Blueprintable, editinlinenew, Experimental, Abstract)
class INTERCHANGECORE_API UInterchangePipelineBase : public UObject
{
	GENERATED_BODY()

public:

	/**
	 * Non virtual helper to allow blueprint to implement event base function to implement a pre import pipeline,
	 * It is call after the translation and before we parse the graph to call the factory. This is where factory node should be created
	 * by the pipeline. Each factory node should be send to a a interchange factory to create an unreal asset.
	 * @note - the Interchange manager is calling this function not the virtual one that is call by the default implementation.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interchange | Pipeline")
	void ScriptedExecutePreImportPipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas);
	/** The default implementation (call if the blueprint do not have any implementation) will call the virtual ExecuteImportPipeline */
	void ScriptedExecutePreImportPipeline_Implementation(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas)
	{
		//By default we call the virtual import pipeline execution
		ExecutePreImportPipeline(BaseNodeContainer, SourceDatas);
	}

	/**
	 * Non virtual helper to allow blueprint to implement event base function to implement a post import pipeline,
	 * It is call after we completely import an asset. PostEditChange is already called. Some assets uses asynchronous build.
	 * This can be useful if you need builded data of an asset to finish the setup of another asset.
	 * @example - PhysicsAsset need skeletal mesh render data to be build properly.
	 * @note - the Interchange manager is calling this function not the virtual one that is call by the default implementation.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interchange | Pipeline")
	void ScriptedExecutePostImportPipeline(const UInterchangeBaseNodeContainer* BaseNodeContainer, const FString& FactoryNodeKey, UObject* CreatedAsset, bool bIsAReimport);
	/** The default implementation (call if the blueprint do not have any implementation) will call the virtual ExecuteImportPipeline */
	void ScriptedExecutePostImportPipeline_Implementation(const UInterchangeBaseNodeContainer* BaseNodeContainer, const FString& FactoryNodeKey, UObject* CreatedAsset, bool bIsAReimport)
	{
		//By default we call the virtual import pipeline execution
		ExecutePostImportPipeline(BaseNodeContainer, FactoryNodeKey, CreatedAsset, bIsAReimport);
	}

	/**
	 * Non virtual helper to allow blueprint to implement event base function let the interchange know if it can run asynchronously.
	 * the Interchange manager is calling this function not the virtual one that is call by the default implementation.
	 */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Interchange | Pipeline")
	void ScriptedExecuteExportPipeline(UInterchangeBaseNodeContainer* BaseNodeContainer);

	/** The default implementation (call if the blueprint do not have any implementation) will call the virtual ExecuteExportPipeline */
	void ScriptedExecuteExportPipeline_Implementation(UInterchangeBaseNodeContainer* BaseNodeContainer)
	{
		ExecuteExportPipeline(BaseNodeContainer);
	}

	/**
	 * Non virtual helper to allow blueprint to implement event base function let the interchange know if it can run asynchronously.
	 * the Interchange manager is calling this function not the virtual one that is call by the default implementation.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interchange | Pipeline")
	bool ScriptedCanExecuteOnAnyThread(EInterchangePipelineTask PipelineTask);

	/** The default implementation (call if the blueprint do not have any implementation) will call the virtual CanExecuteAsync */
	bool ScriptedCanExecuteOnAnyThread_Implementation(EInterchangePipelineTask PipelineTask)
	{
		return CanExecuteOnAnyThread(PipelineTask);
	}

	/**
	 * Non virtual helper to allow blueprint to implement event base function.
	 * the Interchange framework is calling this function not the virtual one that is called by the default implementation.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interchange | Pipeline")
	void ScriptedSetReimportSourceIndex(UClass* ReimportObjectClass, const int32 SourceFileIndex);

	/** The default implementation (call if the blueprint do not have any implementation) will call the virtual SetReimportContentFromSourceIndex */
	void ScriptedSetReimportSourceIndex_Implementation(UClass* ReimportObjectClass, const int32 SourceFileIndex)
	{
		SetReimportSourceIndex(ReimportObjectClass, SourceFileIndex);
	}

	/**
	 * Non scripted class should return false here, we have the default to true because scripted class cannot override
	 * this function since it can be call in a asynchronous thread, which python cannot be executed.
	 *
	 * We cannot call ScriptedCanExecuteOnAnyThread for a scripted python pipeline from the task parsing async thread.
	 * This function allow us to not call it and force the ScriptedExecutePostImportPipeline to execute on the game thread.
	 */
	virtual bool IsScripted()
	{
		return true;
	}

	void LoadSettings(const FName PipelineStackName);

	void SaveSettings(const FName PipelineStackName);

	/**
	 * This function is called before showing the import dialog it is not called when doing a re-import.
	 */
	virtual void PreDialogCleanup(const FName PipelineStackName) {}

	/**
	 * This function should return true if all the pipeline settings are in a valid state to start the import.
	 * The Interchange Pipeline Configuration Dialog will call this to know if the Import button can be enable.
	 */
	virtual bool IsSettingsAreValid(TOptional<FText>& OutInvalidReason) const
	{
		return true;
	}

	/**
	 * This function is call only when we do a re-import before we show the pipeline dialog. Pipeline that override it can change the existing settings according to the re-import type.
	 * The function is also call when we import or re-import custom LOD and alternate skinning.
	 * @Param ReimportType - Tell pipeline what re-import type the user want to achieve.
	 * @Param ReimportAsset - This is an optional parameter which is set when re-importing an asset.
	 */
	virtual void AdjustSettingsForReimportType(EInterchangeReimportType ReimportType, TObjectPtr<UObject> ReimportAsset) {}

	/**
	 * This function is used to add the given message object directly into the results for this operation.
	 */
	template <typename T>
	T* AddMessage() const
	{
		check(Results != nullptr);
		T* Item = Results->Add<T>();
		return Item;
	}

	void AddMessage(UInterchangeResult* Item) const
	{
		check(Results != nullptr);
		Results->Add(Item);
	}
	
	void SetResultsContainer(UInterchangeResultsContainer* InResults)
	{
		Results = InResults;
	}

	bool SetLockedPropertyStatus(const FName PropertyPath, bool bLocked);
	bool GetLockedPropertyStatus(const FName PropertyPath) const;

	/**
	 * Return the property name of the LockedProperties map
	 */
	static FName GetLockedPropertiesPropertyName();

	/**
	 * If true, the property editor for this pipeline instance will allow locked properties edition.
	 * If false, the property editor for this pipeline instance will set locked properties in read only.
	 * 
	 * Note: If you open in the content browser a pipeline asset you will be able to edit locked properties.
	 *       If you import a file with interchange, the import dialog will show locked properties in read only mode.
	 */
	bool bAllowLockedPropertiesEdition = true;

protected:

	/**
	 * This function can modify the BaseNodeContainer to create a pipeline that will set the graph and the nodes options has it want it to be imported by the factories
	 * The interchange manager is not calling this function directly. It is calling the blueprint native event in case this object is a blueprint derive object.
	 * By default the scripted implementation is calling this virtual pipeline.
	 */
	virtual void ExecutePreImportPipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas)
	{
	}

	/**
	 * This function can read the node data and apply some change to the imported asset. This is called after the factory creates the asset and configures the asset properties.
	 * The interchange manager is not calling this function directly. It is calling the blueprint native event in case this object is a blueprint derived object.
	 * By default the scripted implementation is calling this virtual pipeline.
	 */
	virtual void ExecutePostImportPipeline(const UInterchangeBaseNodeContainer* BaseNodeContainer, const FString& NodeKey, UObject* CreatedAsset, bool bIsAReimport)
	{
	}

	/**
	 * This function tell the interchange manager if we can execute this pipeline in async mode. If it return false, the ScriptedExecuteImportPipeline
	 * will be call on the main thread (GameThread), if true it will be run in a background thread and possibly in parallel. If there is multiple
	 * import process in same time.
	 *
	 */
	virtual bool CanExecuteOnAnyThread(EInterchangePipelineTask PipelineTask)
	{
		return true;
	}

	virtual void SetReimportSourceIndex(UClass* ReimportObjectClass, const int32 SourceFileIndex)
	{
	}

	/** This function can modify the BaseNodeContainer to create a pipeline that will set/validate the graph nodes hierarchy and options.*/
	virtual void ExecuteExportPipeline(UInterchangeBaseNodeContainer* BaseNodeContainer)
	{
	}

	void LoadSettingsInternal(const FName PipelineStackName, const FString& ConfigFilename, TMap<FName, bool>& ParentLockedProperties);

	void SaveSettingsInternal(const FName PipelineStackName, const FString& ConfigFilename);

	UPROPERTY()
	TObjectPtr<UInterchangeResultsContainer> Results;

	/* Map of property path and lock status. Any properties that have a true lock status will be readonly when showing the import dialog */
	UPROPERTY(BlueprintReadWrite, Category = "Pipeline Properties Manager")
	TMap<FName, bool> LockedProperties;
};
