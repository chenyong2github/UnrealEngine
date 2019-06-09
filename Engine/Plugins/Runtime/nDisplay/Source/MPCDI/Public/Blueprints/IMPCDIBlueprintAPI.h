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
	virtual void GetMPCDIMeshData(const FString& MPCDIFile, const FString& BufferName, const FString& RegionName, FMPCDIGeometryExportData& MeshData) = 0;
};
