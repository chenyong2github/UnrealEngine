// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/ObjectMacros.h"
#include "UObject/TextProperty.h"
#include "Internationalization/Text.h"
#include "Containers/UnrealString.h"
#include "MoviePipelineMasterConfig.h"

#include "MoviePipelineEditorBlueprintLibrary.generated.h"

// Forward Declare
class UMoviePipelineMasterConfig;
class UMoviePipelineExecutorJob;

UCLASS(meta=(ScriptName="MoviePipelineEditorLibrary"))
class MOVIERENDERPIPELINEEDITOR_API UMoviePipelineEditorBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
public:
	UFUNCTION(BlueprintCallable, Category = "Movie Render Pipeline")
	static bool ExportConfigToAsset(const UMoviePipelineMasterConfig* InConfig, const FString& InPackagePath, const FString& InFileName, const bool bInSaveAsset, UMoviePipelineMasterConfig*& OutAsset, FText& OutErrorReason);

	/** Checks to see if any of the Jobs try to point to maps that wouldn't be valid on a remote render (ie: unsaved maps) */
	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	static bool IsMapValidForRemoteRender(const TArray<UMoviePipelineExecutorJob*>& InJobs);

	/** Pop a dialog box that specifies that they cannot render due to never saved map. Only shows OK button. */
	UFUNCTION(BlueprintCallable, Category = "Movie Render Pipeline")
	static void WarnUserOfUnsavedMap();

	/** Take the specified Queue, duplicate it and write it to disk in the ../Saved/MovieRenderPipeline/ folder. Returns the duplicated queue. */
	UFUNCTION(BlueprintCallable, Category = "Movie Render Pipeline")
	static UMoviePipelineQueue* SaveQueueToManifestFile(UMoviePipelineQueue* InPipelineQueue, FString& OutManifestFilePath);

	/** Loads the specified manifest file and converts it into an FString to be embedded with HTTP REST requests. Use in combination with SaveQueueToManifestFile. */
	UFUNCTION(BlueprintCallable, Category = "Movie Render Pipeline")
	static FString ConvertManifestFileToString(const FString& InManifestFilePath);
};