// Copyright Epic Games, Inc. All Rights Reserved.

/**
 *
 */

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/EngineTypes.h"
#include "ProceduralFoliageEditorLibrary.generated.h"

class AProceduralFoliageVolume;
class UProceduralFoliageComponent;

UCLASS()
class UProceduralFoliageEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="Foliage")
	static void ResimulateProceduralFoliageVolumes(const TArray<AProceduralFoliageVolume*>& ProceduralFoliageVolumes);

	UFUNCTION(BlueprintCallable, Category="Foliage")
	static void ResimulateProceduralFoliageComponents(const TArray<UProceduralFoliageComponent*>& ProceduralFoliageComponents);

	UFUNCTION(BlueprintCallable, Category="Foliage")
	static void ClearProceduralFoliageVolumes(const TArray<AProceduralFoliageVolume*>& ProceduralFoliageVolumes);

	UFUNCTION(BlueprintCallable, Category = "Foliage")
	static void ClearProceduralFoliageComponents(const TArray<UProceduralFoliageComponent*>& ProceduralFoliageComponents);
};
