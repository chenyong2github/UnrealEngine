// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DatasmithImportOptions.h"

#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/TextProperty.h"

#include "CoreTechBlueprintLibrary.generated.h"

class UStaticMesh;

UCLASS(Blueprintable, BlueprintType, meta = (DisplayName = "CAD Surface Operations Library"))
class DATASMITHCORETECHEXTENSION_API UCoreTechBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Re-tessellate LOD 0 of a static mesh if it contains parametric surface data.
	 * @param	StaticMesh				Static mesh to re-tessellate.
	 * @param	TessellationSettings	Tessellation settings to use.
	 * @param	bPostChanges			If true, .
	 * @param	OutReason				Text describing the reason of failure.
	 * @return True if successful, false otherwise
	 */
	UFUNCTION(BlueprintPure, Category = "Datasmith | Surface Operations" )
	static bool RetessellateStaticMesh( UStaticMesh* StaticMesh, const FDatasmithTessellationOptions& TessellationSettings, bool bPostChanges, FText& OutReason );
};
