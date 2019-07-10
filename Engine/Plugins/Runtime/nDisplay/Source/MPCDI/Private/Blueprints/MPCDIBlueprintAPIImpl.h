// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

#include "Blueprints/IMPCDIBlueprintAPI.h"
#include "MPCDIBlueprintAPIImpl.generated.h"


/**
 * Blueprint API interface implementation
 */
UCLASS()
class UMPCDIAPIImpl
	: public UObject
	, public IMPCDIBlueprintAPI
{
	GENERATED_BODY()

public:
	/**
	* Binds multiple device channels to multiple keys
	*
	* @return true if success
	*/
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get MPCDI Mesh Data"), Category = "MPCDI")
	virtual void GetMPCDIMeshData(const FString& MPCDIFile, const FString& BufferName, const FString& RegionName, FMPCDIGeometryExportData& MeshData) override;
};