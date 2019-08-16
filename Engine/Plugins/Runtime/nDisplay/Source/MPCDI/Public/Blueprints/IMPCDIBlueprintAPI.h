// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Blueprints/MPCDIContainers.h"
#include "IMPCDIBlueprintAPI.generated.h"


UINTERFACE(meta = (CannotImplementInterfaceInBlueprint))
class UMPCDIBlueprintAPI : public UInterface
{
	GENERATED_BODY()
};


class IMPCDIBlueprintAPI
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get MPCDI Mesh Data"), Category = "MPCDI")
	virtual bool GetMPCDIMeshData(const FString& MPCDIFile, const FString& BufferName, const FString& RegionName, FMPCDIGeometryExportData& MeshData) = 0;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get PFM Mesh Data"), Category = "MPCDI")
	virtual bool GetPFMMeshData(const FString& PFMFile, FMPCDIGeometryExportData& MeshData, float PFMScale=1, bool bIsMPCDIAxis= true) = 0;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set MPCDI Mesh Data"), Category = "MPCDI")
	virtual void SetMPCDIMeshData(const FString& MPCDIFile, const FString& BufferName, const FString& RegionName, const FMPCDIGeometryImportData& MeshData) = 0;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Reload Changed External Files"), Category = "MPCDI")
	virtual void ReloadChangedExternalFiles() = 0;
};
