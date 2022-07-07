// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "InterchangeTranslatorBase.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Texture/InterchangeTexturePayloadData.h"
#include "Texture/InterchangeTexturePayloadInterface.h"

#include "InterchangeImageWrapperTranslator.generated.h"

/**
 * A translator for most of the image wrapper supported formats
 */
UCLASS(BlueprintType, Experimental)
class INTERCHANGEIMPORT_API UInterchangeImageWrapperTranslator : public UInterchangeTranslatorBase, public IInterchangeTexturePayloadInterface
{
	GENERATED_BODY()
public:

	virtual TArray<FString> GetSupportedFormats() const override;

	virtual bool DoesSupportAssetType(EInterchangeTranslatorAssetType AssetType) const override { return AssetType == EInterchangeTranslatorAssetType::Textures; }

	/**
	 * Translate the associated source data into a node hold by the specified nodes container.
	 *
	 * @param BaseNodeContainer - The unreal objects descriptions container where to put the translated source data.
	 * @return true if the translator can translate the source data, false otherwise.
	 */
	virtual bool Translate(UInterchangeBaseNodeContainer& BaseNodeContainer) const override;

	/* IInterchangeTexturePayloadInterface Begin */

	/**
	 * Once the translation is done, the import process need a way to retrieve payload data.
	 * This payload will be use by the factories to create the asset.
	 *
	 * @param PayloadSourceData - The source data containing the data to translate
	 * @param PayloadKey - The key to retrieve the a particular payload contain into the specified source data.
	 * @return a PayloadData containing the import image data. The TOptional will not be set if there is an error.
	 */
	virtual TOptional<UE::Interchange::FImportImage> GetTexturePayloadData(const UInterchangeSourceData* PayloadSourceData, const FString& PayLoadKey) const override;

	/* IInterchangeTexturePayloadInterface End */

	static TOptional<UE::Interchange::FImportImage> GetTexturePayloadDataFromBuffer(const TArray64<uint8>& SourceDataBuffer);
};