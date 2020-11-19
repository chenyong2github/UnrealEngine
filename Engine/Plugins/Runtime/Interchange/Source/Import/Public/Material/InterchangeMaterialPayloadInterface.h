// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "InterchangeSourceData.h"
#include "Material/InterchangeMaterialPayloadData.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Interface.h"

#include "InterchangeMaterialPayloadInterface.generated.h"

UINTERFACE()
class INTERCHANGEIMPORTPLUGIN_API UInterchangeMaterialPayloadInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Material translator interface. Derive from it if your translator can import materials
 */
class INTERCHANGEIMPORTPLUGIN_API IInterchangeMaterialPayloadInterface
{
	GENERATED_BODY()
public:

	/**
	 * Once the translation is done, the import process need a way to retrieve payload data.
	 * This payload will be use by the factories to create the asset.
	 *
	 * @param SourceData - The source data containing the data to translate
	 * @param PayloadKey - The key to retrieve the a particular payload contain into the specified source data.
	 * @return a PayloadData containing the import image data. The TOptional will not be set if there is an error.
	 */
	virtual TOptional<UE::Interchange::FMaterialPayloadData> GetMaterialPayloadData(const UInterchangeSourceData* SourceData, const FString& PayloadKey) const = 0;
};


