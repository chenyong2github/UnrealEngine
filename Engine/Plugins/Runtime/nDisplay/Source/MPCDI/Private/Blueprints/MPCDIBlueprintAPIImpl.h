// Copyright Epic Games, Inc. All Rights Reserved.

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
	* Return MPCDI mesh data
	*
	*/
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get MPCDI Mesh Data"), Category = "MPCDI")
	virtual bool GetMPCDIMeshData(const FString& MPCDIFile, const FString& BufferName, const FString& RegionName, FMPCDIGeometryExportData& MeshData) override;

	/**
	* Return PFM mesh data
	*
	*/
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get PFM Mesh Data"), Category = "MPCDI")
	virtual bool GetPFMMeshData(const FString& PFMFile, FMPCDIGeometryExportData& MeshData, float PFMScale = 1, bool bIsMPCDIAxis = true) override;

	/**
	* Set MPCDI mesh data at runtime
	*
	*/
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set MPCDI Mesh Data"), Category = "MPCDI")
	virtual void SetMPCDIMeshData(const FString& MPCDIFile, const FString& BufferName, const FString& RegionName, const FMPCDIGeometryImportData& MeshData) override;

	/**
	* Reload changed external files
	*
	*/
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Reload Changed External Files"), Category = "MPCDI")
	virtual void ReloadChangedExternalFiles() override;


};