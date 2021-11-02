// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InterchangeResultsContainer.h"
#include "UObject/Class.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "InterchangeFactoryBase.generated.h"

class ULevel;
class UInterchangeBaseNode;
class UInterchangeBaseNodeContainer;
class UInterchangePipelineBase;
class UInterchangeSourceData;
class UInterchangeTranslatorBase;


UENUM()
enum class EReimportStrategyFlags : uint8
{
	ApplyNoProperties, //Do not apply any property when re-importing, simply change the source data
	ApplyPipelineProperties, //Always apply all pipeline specified properties
	ApplyEditorChangedProperties //Always apply all pipeline properties, but leave the properties modified in editor since the last import
};

UCLASS(BlueprintType, Blueprintable, Abstract, Experimental)
class INTERCHANGECORE_API UInterchangeFactoryBase : public UObject
{
	GENERATED_BODY()
public:

	/**
	 * return the UClass this factory can create.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Factory")
	virtual UClass* GetFactoryClass() const
	{
		return nullptr;
	}

	/**
	 * Parameters to pass to CreateAsset function
	 */
	struct FCreateAssetParams
	{
		/** The package where to create the asset, if null it will put it in the transient package */
		UObject* Parent = nullptr;

		/** The name we want to give to the asset we will create */
		FString AssetName = FString();

		/** The base node that describe how to create the asset */
		UInterchangeBaseNode* AssetNode = nullptr;

		/** The translator is use to retrieve the PayLoad data in case the factory need it */
		const UInterchangeTranslatorBase* Translator = nullptr;

		/** The source data, mainly use to set the asset import data file. TODO: we have to refactor UAssetImportData, the source data should be the base class for this now */
		const UInterchangeSourceData* SourceData = nullptr;

		/** The node container associate with the current source index */
		const UInterchangeBaseNodeContainer* NodeContainer = nullptr;

		/**
		 * If when we try to create the package we found out the asset already exist, this field will contain
		 * the asset we want to re-import. The re-import should just change the source data and not any asset settings.
		 */
		UObject* ReimportObject = nullptr;

		EReimportStrategyFlags ReimportStrategyFlags;
	};

	/**
	 * Create an empty asset from a Node data, this function will be call in the main thread in same time has we create the package.
	 * The asset create here must have the internal async flag set because the object can be setup in an asynchronous thread and must be consider like an async object until
	 * The completion task on the object is finish.
	 *
	 * @param Arguments - The structure containing all necessary arguments, see the structure definition for the documentation
	 * @return the created UObject or nullptr if there is an error, see LOG for any error detail.
	 *
	 * @Note override function should verify the AssetNode in Arguments match the expected type for the factory.
	 */
	virtual UObject* CreateEmptyAsset(const FCreateAssetParams& Arguments)
	{
		return nullptr;
	}

	/**
	 * Create an asset from a Node data, this function must be multithread safe, it cannot use member all the data must be pass in the FCreateAssetParams structure
	 *
	 * @param Arguments - The structure containing all necessary arguments, see the structure definition for the documentation
	 * @return the created UObject or nullptr if there is an error, see LOG for any error detail.
	 *
	 * @Note override function should verify the AssetNode in Arguments match the expected type for the factory.
	 */
	virtual UObject* CreateAsset(const FCreateAssetParams& Arguments)
	{
		return nullptr;
	}

	/**
	 * Parameters to pass to SpawnActor function
	 */
	struct FCreateSceneObjectsParams
	{
		/** The level in which to create the scene objects */
		ULevel* Level = nullptr;

		/** The name we want to give to the actor that we will create */
		FString ObjectName;

		/** The base node that describe how to create the asset */
		UInterchangeBaseNode* ObjectNode = nullptr;

		/** The node container associated with the current source index */
		const UInterchangeBaseNodeContainer* NodeContainer = nullptr;

		/** Whether to create the scene objects for the child nodes or not */
		bool bCreateSceneObjectsForChildren = false;
	};

	/**
	 * Creates the scene object from a Scene Node data.
	 * If FCreateSceneObjectsParams::bCreateSceneObjectsForChildren is true, will also create the scene objects for our children.
	 *
	 * @param Arguments - The structure containing all necessary arguments, see the structure definition for the documentation.
	 * @return The node uids and the scene objects that we created from them.
	 */
	virtual TMap<FString, UObject*> CreateSceneObjects(const FCreateSceneObjectsParams& Arguments)
	{
		return {};
	}

	/** Return true if the factory can create the asset asynchronously on any thread, false if it need to be on the main thread */
	virtual bool CanExecuteOnAnyThread() const
	{
		return true;
	}

	/**
	 * Parameters to pass to CreateAsset function
	 */
	struct FImportPreCompletedCallbackParams
	{
		/** The source data, mainly use to set the asset import data file. TODO: we have to refactor UAssetImportData, the source data should be the base class for this now */
		const UInterchangeSourceData* SourceData = nullptr;
		UInterchangeBaseNode* FactoryNode = nullptr;


		/** The UObject  we want to execute code on*/
		UObject* ImportedObject = nullptr;
		FString NodeUniqueID;
		UInterchangeBaseNodeContainer* NodeContainer = nullptr;
		TArray<UInterchangePipelineBase*> Pipelines;


		EReimportStrategyFlags ReimportStrategyFlags;
		bool bIsReimport  = false;
;
	};

	/*
	 * This function is call in the pre completion task on the main thread, use it to call main thread post creation step for your assets
	 * @note - This function is called when starting the pre completion task (before PostEditChange is called for the asset).
	 */
	virtual void PreImportPreCompletedCallback(const FImportPreCompletedCallbackParams& Arguments)
	{
		check(IsInGameThread());
		return;
	}
	
	/*
	 * This function is call in the pre completion task on the main thread, use it to call main thread post creation step for your assets
	 * @note - This function is called at the end of the pre completion task (after PostEditChange is called for the asset).
	 */
	virtual void PostImportPreCompletedCallback(const FImportPreCompletedCallbackParams& Arguments)
	{
		check(IsInGameThread());
		return;
	}

	/**
	 * This function is used to add the given message object directly into the results for this operation.
	 */
	template <typename T>
	T* AddMessage()
	{
		check(Results != nullptr);
		T* Item = Results->Add<T>();
		return Item;
	}


	void AddMessage(UInterchangeResult* Item)
	{
		check(Results != nullptr);
		Results->Add(Item);
	}
	

	void SetResultsContainer(UInterchangeResultsContainer* InResults)
	{
		Results = InResults;
	}


	UPROPERTY()
	TObjectPtr<UInterchangeResultsContainer> Results;
};
