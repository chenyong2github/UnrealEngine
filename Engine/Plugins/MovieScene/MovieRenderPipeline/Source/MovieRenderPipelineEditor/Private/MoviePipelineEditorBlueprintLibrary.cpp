// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineEditorBlueprintLibrary.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "MovieRenderPipelineConfig.h"
#include "AssetRegistryModule.h"
#include "FileHelpers.h"

#define LOCTEXT_NAMESPACE "MoviePipelineEditorBlueprintLibrary"

bool UMoviePipelineEditorBlueprintLibrary::ExportConfigToAsset(const UMovieRenderPipelineConfig* InConfig, const FString& InPackagePath, const FString& InFileName, const bool bInSaveAsset, UMovieRenderPipelineConfig*& OutAsset, FText& OutErrorReason)
{
	if(!InConfig)
	{
		OutErrorReason = LOCTEXT("CantExportNullConfigToPackage", "Can't export a null configuration to a package.");
		return false;
	}
	
	
	if (!FPackageName::IsValidLongPackageName(InPackagePath, false, &OutErrorReason))
	{
		return false;
	}


	UPackage* NewPackage = CreatePackage(nullptr, *InPackagePath);
	NewPackage->AddToRoot();
	
	// Duplicate the provided config into this package.
	UMovieRenderPipelineConfig* NewConfig = Cast<UMovieRenderPipelineConfig>(StaticDuplicateObject(InConfig, NewPackage, FName(*InFileName), RF_NoFlags));
	NewConfig->SetFlags(RF_Public | RF_Transactional | RF_Standalone);
	NewConfig->MarkPackageDirty();

	// Mark it so it shows up in the Content Browser immediately
	FAssetRegistryModule::AssetCreated(NewConfig);

	// If they want to save, ask them to save (and add to version control)
	if (bInSaveAsset)
	{
		TArray<UPackage*> Packages;
		Packages.Add(NewConfig->GetOutermost());

		return UEditorLoadingAndSavingUtils::SavePackages(Packages, true);
	}

	return true;
}

#undef LOCTEXT_NAMESPACE // "MoviePipelineEditorBlueprintLibrary"