// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/ObjectMacros.h"
#include "UObject/TextProperty.h"
#include "Internationalization/Text.h"
#include "Containers/UnrealString.h"
#include "MovieRenderPipelineConfig.h"

#include "MoviePipelineEditorBlueprintLibrary.generated.h"

// Forward Declare
class UMovieRenderPipelineConfig;

UCLASS()
class MOVIERENDERPIPELINEEDITOR_API UMoviePipelineEditorBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
public:
	UFUNCTION(BlueprintCallable, Category = "Movie Render Pipeline")
	static bool ExportConfigToAsset(const UMovieRenderPipelineConfig* InConfig, const FString& InPackagePath, const FString& InFileName, const bool bInSaveAsset, UMovieRenderPipelineConfig*& OutAsset, FText& OutErrorReason);
};