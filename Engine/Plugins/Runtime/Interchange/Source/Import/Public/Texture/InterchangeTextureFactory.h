// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InterchangeFactoryBase.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "Texture/InterchangeTexturePayloadData.h"

#include "InterchangeTextureFactory.generated.h"

UCLASS(BlueprintType)
class INTERCHANGEIMPORT_API UInterchangeTextureFactory : public UInterchangeFactoryBase
{
	GENERATED_BODY()
public:

	//////////////////////////////////////////////////////////////////////////
	// Interchange factory base interface begin

	virtual UClass* GetFactoryClass() const override;
	virtual UObject* CreateEmptyAsset(const FCreateAssetParams& Arguments) const override;
	virtual UObject* CreateAsset(const FCreateAssetParams& Arguments) const override;
	virtual void PreImportPreCompletedCallback(const FImportPreCompletedCallbackParams& Arguments) const override;
	// Interchange factory base interface end
	//////////////////////////////////////////////////////////////////////////
};


