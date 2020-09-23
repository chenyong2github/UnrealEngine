// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeBaseNode.h"
#include "InterchangeTranslatorBase.h"
#include "UObject/Class.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "InterchangeFactoryBase.generated.h"

UCLASS(BlueprintType, Blueprintable, Abstract)
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
		const UInterchangeBaseNode* AssetNode = nullptr;

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
	};

	/**
	 * Create an empty asset from a Node data, this function will be call in the main thread in same time has we create the package.
	 *
	 * @param Arguments - The structure containing all necessary arguments, see the structure definition for the documentation
	 * @return the created UObject or nullptr if there is an error, see LOG for any error detail.
	 *
	 * @Note override function should verify the AssetNode in Arguments match the expected type for the factory.
	 */
	virtual UObject* CreateEmptyAsset(const FCreateAssetParams& Arguments) const
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
	virtual UObject* CreateAsset(const FCreateAssetParams& Arguments) const
	{
		return nullptr;
	}

	/** Return true if the factory can create the asset asynchronously on any thread, false if it need to be on the main thread */
	virtual bool CanExecuteOnAnyThread() const
	{
		return true;
	}

	/**
	 * Parameters to pass to CreateAsset function
	 */
	struct FPostImportGameThreadCallbackParams
	{
		/** The source data, mainly use to set the asset import data file. TODO: we have to refactor UAssetImportData, the source data should be the base class for this now */
		const UInterchangeSourceData* SourceData = nullptr;

		/** The UObject  we want to execute code on*/
		UObject* ReimportObject = nullptr;
	};
	/* This function is call in the completion task on the main thread, use it to call main thread post creation step for your assets*/
	virtual void PostImportGameThreadCallback(const FPostImportGameThreadCallbackParams& Arguments) const
	{
		check(IsInGameThread());
		return;
	}
};
