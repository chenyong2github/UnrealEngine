// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeImportTestStepReimport.h"
#include "InterchangeImportTestData.h"
#include "InterchangeTestFunction.h"
#include "UObject/SavePackage.h"


UE::Interchange::FAssetImportResultPtr UInterchangeImportTestStepReimport::StartStep(const FInterchangeImportTestData& Data)
{
	// Find the object we wish to reimport
	TArray<UObject*> PotentialObjectsToReimport;
	PotentialObjectsToReimport.Reserve(Data.ResultObjects.Num());

	for (UObject* ResultObject : Data.ResultObjects)
	{
		if (ResultObject->GetClass() == AssetTypeToReimport.Get())
		{
			PotentialObjectsToReimport.Add(ResultObject);
		}
	}

	UObject* AssetToReimport = nullptr;

	if (PotentialObjectsToReimport.Num() == 1)
	{
		AssetToReimport = PotentialObjectsToReimport[0];
	}
	else if (PotentialObjectsToReimport.Num() > 1 && !AssetNameToReimport.IsEmpty())
	{
		for (UObject* Object : PotentialObjectsToReimport)
		{
			if (Object->GetName() == AssetNameToReimport)
			{
				AssetToReimport = Object;
				break;
			}
		}
	}

	if (AssetToReimport == nullptr)
	{
		return UE::Interchange::FAssetImportResultPtr();
	}

	// Start the Interchange import
	UE::Interchange::FScopedSourceData ScopedSourceData(SourceFileToReimport.FilePath);

	FImportAssetParameters Params;
	Params.bIsAutomated = true;
	Params.ReimportAsset = AssetToReimport;

	UInterchangeManager& InterchangeManager = UInterchangeManager::GetInterchangeManager();
	return InterchangeManager.ImportAssetAsync(Data.DestAssetPackagePath, ScopedSourceData.GetSourceData(), Params);
}


FTestStepResults UInterchangeImportTestStepReimport::FinishStep(FInterchangeImportTestData& Data, FAutomationTestExecutionInfo& ExecutionInfo)
{
	FTestStepResults Results;

	// If we need to perform a save and fresh reload of everything we imported, do it here
	if (bSaveThenReloadImportedAssets)
	{
		// First save
		for (const FAssetData& AssetData : Data.ImportedAssets)
		{
			const bool bLoadAsset = false;
			UObject* AssetObject = AssetData.FastGetAsset(bLoadAsset);
			UPackage* PackageObject = AssetObject->GetPackage();
			check(PackageObject);

			AssetObject->MarkPackageDirty();

			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Standalone;
			SaveArgs.SaveFlags = SAVE_NoError;
			UPackage::SavePackage(PackageObject, AssetObject,
				*FPackageName::LongPackageNameToFilename(AssetData.PackageName.ToString(), FPackageName::GetAssetPackageExtension()),
				SaveArgs);
		}

		// Then rename original objects and their packages, and mark as garbage
		for (const FAssetData& AssetData : Data.ImportedAssets)
		{
			const bool bLoadAsset = false;
			UObject* AssetObject = AssetData.FastGetAsset(bLoadAsset);
			UPackage* PackageObject = AssetObject->GetPackage();
			check(PackageObject);
			check(PackageObject == AssetData.GetPackage());

			// Renaming the original objects avoids having to do a GC sweep here.
			// Any existing references to them will be retained but irrelevant.
			// Then the new object can be loaded in their place, as if it were being loaded for the first time.
			const ERenameFlags RenameFlags = REN_ForceNoResetLoaders | REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty;
			AssetObject->Rename(*(AssetObject->GetName() + TEXT("_TRASH")), PackageObject, RenameFlags);
			PackageObject->Rename(*(PackageObject->GetName() + TEXT("_TRASH")), nullptr, RenameFlags);
			AssetObject->ClearFlags(RF_Standalone | RF_Public);
			AssetObject->RemoveFromRoot();
			AssetObject->MarkAsGarbage();
			PackageObject->MarkAsGarbage();
		}

		// Now reload
		Data.ResultObjects.Reset();
		for (const FAssetData& AssetData : Data.ImportedAssets)
		{
			check(!AssetData.IsAssetLoaded());
			UObject* AssetObject = AssetData.GetAsset();
			check(AssetObject);
			Data.ResultObjects.Add(AssetObject);
		}
	}

	// Run all the tests
	bool bSuccess = PerformTests(Data, ExecutionInfo);

	Results.bTestStepSuccess = bSuccess;
	return Results;
}


FString UInterchangeImportTestStepReimport::GetContextString() const
{
	return FString(TEXT("Reimporting ")) + FPaths::GetCleanFilename(SourceFileToReimport.FilePath);
}
