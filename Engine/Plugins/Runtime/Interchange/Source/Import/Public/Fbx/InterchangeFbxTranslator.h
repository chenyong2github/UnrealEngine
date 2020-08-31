// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "InterchangeTranslatorBase.h"
#include "InterchangeDispatcher.h"
#include "Material/MaterialPayLoad.h"
#include "Mesh/SkeletalMeshPayload.h"
#include "Mesh/StaticMeshPayload.h"

#include "InterchangeFbxTranslator.generated.h"

UCLASS(BlueprintType)
class INTERCHANGEIMPORTPLUGIN_API UInterchangeFbxTranslator : public UInterchangeTranslatorBase
{
	GENERATED_BODY()
public:
	UInterchangeFbxTranslator(const class FObjectInitializer& ObjectInitializer);

	/** Begin UInterchangeTranslatorBase API*/
	virtual bool CanImportSourceData(const UInterchangeSourceData* InSourceData) const override;
	virtual bool Translate(const UInterchangeSourceData* SourceData, Interchange::FBaseNodeContainer& BaseNodeContainer) const override;
	virtual void ImportFinish() override;
	/** End UInterchangeTranslatorBase API*/

	/**
	 * Once the translation is done, the import process need a way to retrieve payload data.
	 * This payload will be use by the factories to create the asset.
	 *
	 * @param SourceData - The source data containing the data to translate
	 * @param PayloadKey - The key to retrieve the a particular payload contain into the specified source data.
	 * @return a PayloadData containing the mesh data to import. The TOptional will not be set if there is an error.
	 */
	virtual TOptional<Interchange::FStaticMeshPayloadData> GetStaticMeshPayloadData(const UInterchangeSourceData* SourceData, const FString& PayLoadKey) const;

	/**
	 * Once the translation is done, the import process need a way to retrieve payload data.
	 * This payload will be use by the factories to create the asset.
	 *
	 * @param SourceData - The source data containing the data to translate
	 * @param PayloadKey - The key to retrieve the a particular payload contain into the specified source data.
	 * @return a PayloadData containing the mesh data to import. The TOptional will not be set if there is an error.
	 */
	virtual TOptional<Interchange::FSkeletalMeshPayloadData> GetSkeletalMeshPayloadData(const UInterchangeSourceData* SourceData, const FString& PayLoadKey) const;

	/**
	 * Once the translation is done, the import process need a way to retrieve payload data.
	 * This payload will be use by the factories to create the asset.
	 *
	 * @param SourceData - The source data containing the data to translate
	 * @param PayloadKey - The key to retrieve the a particular payload contain into the specified source data.
	 * @return a PayloadData containing the mesh data to import. The TOptional will not be set if there is an error.
	 */
	virtual TOptional<Interchange::FMaterialPayloadData> GetMaterialPayloadData(const UInterchangeSourceData* SourceData, const FString& PayLoadKey) const;

private:

	FString CreateLoadFbxFileCommand(const FString& FbxFilePath) const;

	//Dispatcher is mutable since it is create during the Translate operation
	//We do not want to allocate the dispatcher and start the InterchangeWorker process
	//in the constructor because Archetype, CDO and registered translators will
	//never translate a source.
	mutable TUniquePtr<InterchangeDispatcher::FInterchangeDispatcher> Dispatcher;
};


