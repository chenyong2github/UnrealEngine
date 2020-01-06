// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineEditorBlueprintLibrary.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "AssetRegistryModule.h"
#include "FileHelpers.h"
#include "ObjectTools.h"
#include "PackageTools.h"

#define LOCTEXT_NAMESPACE "MoviePipelineEditorBlueprintLibrary"

bool UMoviePipelineEditorBlueprintLibrary::ExportConfigToAsset(const UMoviePipelineMasterConfig* InConfig, const FString& InPackagePath, const FString& InFileName, const bool bInSaveAsset, UMoviePipelineMasterConfig*& OutAsset, FText& OutErrorReason)
{
	if(!InConfig)
	{
		OutErrorReason = LOCTEXT("CantExportNullConfigToPackage", "Can't export a null configuration to a package.");
		return false;
	}
	
	FString FixedAssetName = ObjectTools::SanitizeObjectName(InFileName);
	FString NewPackageName = FPackageName::GetLongPackagePath(InPackagePath) + TEXT("/") + FixedAssetName;

	if (!FPackageName::IsValidLongPackageName(NewPackageName, false, &OutErrorReason))
	{
		return false;
	}

	UPackage* NewPackage = CreatePackage(nullptr, *NewPackageName);
	NewPackage->AddToRoot();
	
	// Duplicate the provided config into this package.
	UMoviePipelineMasterConfig* NewConfig = Cast<UMoviePipelineMasterConfig>(StaticDuplicateObject(InConfig, NewPackage, FName(*InFileName), RF_NoFlags));
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