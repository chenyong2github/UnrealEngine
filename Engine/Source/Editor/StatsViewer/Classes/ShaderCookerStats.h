// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "UObject/WeakObjectPtr.h"
#include "ShaderCookerStats.generated.h"


UENUM()
enum EShaderCookerStatsSets
{
	EShaderCookerStatsSets_Default				UMETA(DisplayName = "Default", ToolTip = "Shader Cooker Sets"),
};

/** Statistics page for shader cooker stats. */
UCLASS(Transient, MinimalAPI, meta=( DisplayName = "Shader Cooker Stats", ObjectSetType = "EShaderCookerStatsSets" ) )
class UShaderCookerStats : public UObject
{
	GENERATED_UCLASS_BODY()

	/** Material name */
	UPROPERTY(VisibleAnywhere, AssetRegistrySearchable, Category = "Stats", meta = (ColumnWidth = "200"))
	FString Name;

	/** Material platform */
	UPROPERTY(VisibleAnywhere, AssetRegistrySearchable, Category = "Stats", meta = (ColumnWidth = "50"))
	FString Platform;


	/** Number of shaders compilations */
	UPROPERTY(VisibleAnywhere, AssetRegistrySearchable, Category = "Stats", meta = (DisplayName = "Compiled", ColumnWidth = "74", ShowTotal = "true"))
	int32 Compiled;

	/** Number of shaders cooked*/
	UPROPERTY(VisibleAnywhere, AssetRegistrySearchable, Category = "Stats", meta = (DisplayName = "Cooked", ColumnWidth = "74", ShowTotal = "true"))
	int32 Cooked;

	/** Number of permutations */
	UPROPERTY(VisibleAnywhere, AssetRegistrySearchable, Category = "Stats", meta = (DisplayName = "Permutations", ColumnWidth = "74", ShowTotal = "true"))
	int32 Permutations;

	/** Material path */
	UPROPERTY(VisibleAnywhere, AssetRegistrySearchable, Category = "Stats", meta = (ColumnWidth = "200"))
	FString Path;


};
