// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InterchangeFactoryBase.h"
#include "Misc/TVariant.h"
#include "Texture/InterchangeBlockedTexturePayloadData.h"
#include "Texture/InterchangeSlicedTexturePayloadData.h"
#include "Texture/InterchangeTextureLightProfilePayloadData.h"
#include "Texture/InterchangeTextureLightProfilePayloadData.h"
#include "Texture/InterchangeTexturePayloadData.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "Virtualization/VirtualizedBulkData.h"

#include "InterchangeTextureFactory.generated.h"

namespace UE::Interchange::Private::InterchangeTextureFactory
{
	using FTexturePayloadVariant = TVariant<FEmptyVariantState
		, TOptional<FImportImage>
		, TOptional<FImportBlockedImage>
		, TOptional<FImportSlicedImage>
		, TOptional<FImportLightProfile>>;

	struct FProcessedPayload
	{
		FProcessedPayload() = default;
		FProcessedPayload(FProcessedPayload&&) = default;
		FProcessedPayload& operator=(FProcessedPayload&&) = default;

		FProcessedPayload(const FProcessedPayload&) = delete;
		FProcessedPayload& operator=(const FProcessedPayload&) = delete;

		FProcessedPayload& operator=(UE::Interchange::Private::InterchangeTextureFactory::FTexturePayloadVariant&& InPayloadVariant);

		UE::Interchange::Private::InterchangeTextureFactory::FTexturePayloadVariant SettingsFromPayload;
		UE::Virtualization::FVirtualizedUntypedBulkData::FSharedBufferWithID PayloadAndId;
	};
}

UCLASS(BlueprintType)
class INTERCHANGEIMPORT_API UInterchangeTextureFactory : public UInterchangeFactoryBase
{
	GENERATED_BODY()
public:

	//////////////////////////////////////////////////////////////////////////
	// Interchange factory base interface begin

	virtual UClass* GetFactoryClass() const override;
	virtual UObject* CreateEmptyAsset(const FCreateAssetParams& Arguments) override;
	virtual UObject* CreateAsset(const FCreateAssetParams& Arguments) override;
	virtual void PreImportPreCompletedCallback(const FImportPreCompletedCallbackParams& Arguments) override;
	// Interchange factory base interface end
	//////////////////////////////////////////////////////////////////////////

private:
	UE::Interchange::Private::InterchangeTextureFactory::FProcessedPayload ProcessedPayload;
};


