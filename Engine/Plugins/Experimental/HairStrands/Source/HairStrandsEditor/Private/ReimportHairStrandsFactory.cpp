// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ReimportHairStrandsFactory.h"

#include "EditorFramework/AssetImportData.h"
#include "HairDescription.h"
#include "GroomAsset.h"
#include "HairStrandsImporter.h"
#include "HairStrandsTranslator.h"
#include "Misc/ScopedSlowTask.h"

#define LOCTEXT_NAMESPACE "HairStrandsFactory"

UReimportHairStrandsFactory::UReimportHairStrandsFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bEditorImport = false;

	// The HairStrandsFactory should come before the Reimport factory
	ImportPriority -= 1;
}

bool UReimportHairStrandsFactory::FactoryCanImport(const FString& Filename)
{
	return false;
}

bool UReimportHairStrandsFactory::CanReimport(UObject* Obj, TArray<FString>& OutFilenames)
{
	// Lazy init the translators before first use of the CDO
	if (HasAnyFlags(RF_ClassDefaultObject) && Formats.Num() == 0)
	{
		InitTranslators();
	}

	UAssetImportData* ImportData = nullptr;
	if (UGroomAsset* HairAsset = Cast<UGroomAsset>(Obj))
	{
		ImportData = HairAsset->AssetImportData;
	}

	if (ImportData)
	{
		if (GetTranslator(ImportData->GetFirstFilename()).IsValid())
		{
			ImportData->ExtractFilenames(OutFilenames);
			return true;
		}
	}

	return false;
}

void UReimportHairStrandsFactory::SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths)
{
	UGroomAsset* Asset = Cast<UGroomAsset>(Obj);
	if (Asset && Asset->AssetImportData && ensure(NewReimportPaths.Num() == 1))
	{
		Asset->AssetImportData->UpdateFilenameOnly(NewReimportPaths[0]);
	}
}

EReimportResult::Type UReimportHairStrandsFactory::Reimport(UObject* Obj)
{
	if (UGroomAsset* HairAsset = Cast<UGroomAsset>(Obj))
	{
		if (!HairAsset || !HairAsset->AssetImportData)
		{
			return EReimportResult::Failed;
		}

		CurrentFilename = HairAsset->AssetImportData->GetFirstFilename();

		TSharedPtr<IHairStrandsTranslator> SelectedTranslator = GetTranslator(CurrentFilename);
		if (!SelectedTranslator.IsValid())
		{
			return EReimportResult::Failed;
		}

		FScopedSlowTask Progress( (float) 1, LOCTEXT("ReimportHairAsset", "Reimporting hair asset..."), true );
		Progress.MakeDialog(true);

		FHairDescription HairDescription;
		if (!SelectedTranslator->Translate(CurrentFilename, HairDescription))
		{
			return EReimportResult::Failed;
		}

		UGroomAsset* ReimportedHair = FHairStrandsImporter::ImportHair(FHairImportContext(), HairDescription, HairAsset);
		if (!ReimportedHair)
		{
			return EReimportResult::Failed;
		}

		if (HairAsset->GetOuter())
		{
			HairAsset->GetOuter()->MarkPackageDirty();
		}
		else
		{
			HairAsset->MarkPackageDirty();
		}
	}

	return EReimportResult::Succeeded;
}
