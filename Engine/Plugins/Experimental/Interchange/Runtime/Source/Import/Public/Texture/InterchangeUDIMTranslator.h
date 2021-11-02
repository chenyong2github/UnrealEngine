// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InterchangeTranslatorBase.h"
#include "Texture/InterchangeBlockedTexturePayloadData.h"
#include "Texture/InterchangeBlockedTexturePayloadInterface.h"

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "InterchangeUDIMTranslator.generated.h"

class UInterchangeBaseNodeContainer;
class UInterchangeSourceData;

UCLASS(BlueprintType, Experimental)
class INTERCHANGEIMPORT_API UInterchangeUDIMTranslator : public UInterchangeTranslatorBase, public IInterchangeBlockedTexturePayloadInterface
{
	GENERATED_BODY()

public:

	UInterchangeUDIMTranslator();

	/*
	 * return true if the translator can translate the specified source data.
	 */
	virtual bool CanImportSourceData(const UInterchangeSourceData* InSourceData) const override;

	/**
	 * Translates the associated source data into a node hold by the specified nodes container.
	 *
	 * @param BaseNodeContainer - The unreal objects descriptions container where to put the translated source data.
	 * @return true if the translator can translate the source data, false otherwise.
	 */
	virtual bool Translate(UInterchangeBaseNodeContainer& BaseNodeContainer) const override;

	/**
	 * Once the translation is done, the import process need a way to retrieve payload data.
	 * This payload will be use by the factories to create the asset.
	 *
	 * @param SourceData - The sources data containing the data to translate
	 * @param PayloadKey - The key to retrieve the a particular payload contain into the specified source data.
	 * @return a PayloadData containing the import blocked image data. The TOptional will not be set if there is an error.
	 */
	virtual TOptional<UE::Interchange::FImportBlockedImage> GetBlockedTexturePayloadData(const TMap<int32, FString>& InSourcesData,  const UInterchangeSourceData* OriginalFile) const override;

	// The pattern used to identify the UDIMs files
	UPROPERTY(Transient)
	FString UdimRegexPattern;
};

