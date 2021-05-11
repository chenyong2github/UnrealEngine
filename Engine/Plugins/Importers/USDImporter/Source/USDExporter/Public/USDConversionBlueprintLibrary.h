// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/EngineTypes.h"

#include "USDConversionBlueprintLibrary.generated.h"

/** Wrapped static conversion functions from the UsdUtilities module, so that they can be used via scripting */
UCLASS(meta=(ScriptName="UsdConversionLibrary"))
class USDEXPORTER_API UUsdConversionBlueprintLibrary : public UObject
{
	GENERATED_BODY()

public:

	UFUNCTION( BlueprintCallable, Category = "World utils" )
	static TSet<AActor*> GetActorsToConvert( UWorld* World );

	UFUNCTION( BlueprintCallable, Category = "Layer utils" )
	static FString MakePathRelativeToLayer( const FString& AnchorLayerPath, const FString& PathToMakeRelative );

	UFUNCTION( BlueprintCallable, Category = "Layer utils" )
	static void InsertSubLayer( const FString& ParentLayerPath, const FString& SubLayerPath, int32 Index = -1 );

	UFUNCTION( BlueprintCallable, Category = "Layer utils" )
	static void AddPayload( const FString& ReferencingStagePath, const FString& ReferencingPrimPath, const FString& TargetStagePath );
};
