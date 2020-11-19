// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "InterchangeSourceData.h"
#include "Mesh/InterchangeStaticMeshPayload.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Interface.h"

#include "InterchangeStaticMeshPayloadInterface.generated.h"

UINTERFACE()
class INTERCHANGEIMPORTPLUGIN_API UInterchangeStaticMeshPayloadInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Static mesh payload interface. Derive from this interface if your payload can import static mesh
 */
class INTERCHANGEIMPORTPLUGIN_API IInterchangeStaticMeshPayloadInterface
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
	virtual TOptional<UE::Interchange::FStaticMeshPayloadData> GetStaticMeshPayloadData(const UInterchangeSourceData* SourceData, const FString& PayloadKey) const = 0;
};


