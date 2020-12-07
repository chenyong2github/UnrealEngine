// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InterchangeFactoryBase.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "InterchangeSkeletonFactory.generated.h"

UCLASS(BlueprintType)
class INTERCHANGEIMPORTPLUGIN_API UInterchangeSkeletonFactory : public UInterchangeFactoryBase
{
	GENERATED_BODY()
public:

	//////////////////////////////////////////////////////////////////////////
	// Interchange factory base interface begin

	virtual UClass* GetFactoryClass() const override;
	virtual UObject* CreateEmptyAsset(const FCreateAssetParams& Arguments) const override;
	virtual UObject* CreateAsset(const FCreateAssetParams& Arguments) const override;
	//virtual void PostImportGameThreadCallback(const FPostImportGameThreadCallbackParams& Arguments) const override;

	// Interchange factory base interface end
	//////////////////////////////////////////////////////////////////////////
};


