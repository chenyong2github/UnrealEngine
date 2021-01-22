// Copyright Epic Games, Inc. All Rights Reserved.

#include "FilterLoader.h"

#include "DisjunctiveNormalFormFilter.h"
#include "EditorDirectories.h"

#include "FileHelpers.h"
#include "AssetRegistry/AssetRegistryModule.h"

void UFilterLoader::OverwriteExisting()
{
	if (!ensure(AssetLastSavedOrLoaded.IsSet()))
	{
		return;
	}

	// duplicate asset at destination
	const UObject* AssetDuplicatedAtCorrectPath = [this]()
	{
		const FString NewPackageName = *AssetLastSavedOrLoaded->PackageName.ToString();
		UPackage* DuplicatedPackage = CreatePackage(*NewPackageName);
		UObject* DuplicatedAsset = StaticDuplicateObject(AssetBeingEdited, DuplicatedPackage, *AssetLastSavedOrLoaded->AssetName.ToString());

		if (DuplicatedAsset != nullptr)
		{
			// update duplicated asset & notify asset registry
			if (AssetBeingEdited->HasAnyFlags(RF_Transient))
			{
				DuplicatedAsset->ClearFlags(RF_Transient);
				DuplicatedAsset->SetFlags(RF_Public | RF_Standalone);
			}

			if (AssetBeingEdited->GetOutermost()->HasAnyPackageFlags(PKG_DisallowExport))
			{
				DuplicatedPackage->SetPackageFlags(PKG_DisallowExport);
			}

			DuplicatedAsset->MarkPackageDirty();
			FAssetRegistryModule::AssetCreated(DuplicatedAsset);

			// update last save directory
			const FString PackageFilename = FPackageName::LongPackageNameToFilename(NewPackageName);
			const FString PackagePath = FPaths::GetPath(PackageFilename);

			FEditorDirectories::Get().SetLastDirectory(ELastDirectory::NEW_ASSET, PackagePath);
		}
		return DuplicatedAsset;
	}();
	if (!ensure(AssetDuplicatedAtCorrectPath))
	{
		return;
	}
	
	TArray<UObject*> SavedAssets;
	FEditorFileUtils::PromptForCheckoutAndSave({ AssetDuplicatedAtCorrectPath->GetOutermost() }, true, false);
	
	OnSaveOrLoadAssetOnDisk(AssetDuplicatedAtCorrectPath);
}

void UFilterLoader::SaveAs()
{
	TArray<UObject*> SavedAssets;
	FEditorFileUtils::SaveAssetsAs({ AssetBeingEdited }, SavedAssets);

	// Remember: user can cancel saving by clicking "cancel"
	const bool bSavedSuccessfully = SavedAssets.Num() == 1;
	if (bSavedSuccessfully)
	{
		UObject* SavedAssetOnDisk = SavedAssets[0];
		OnSaveOrLoadAssetOnDisk(SavedAssetOnDisk);
	}
}

void UFilterLoader::LoadAsset(const FAssetData& PickedAsset)
{
	UObject* LoadedAsset = PickedAsset.GetAsset();
	if (!ensure(LoadedAsset) || !ensure(Cast<UDisjunctiveNormalFormFilter>(LoadedAsset)))
	{
		return;
	}
	
	OnSaveOrLoadAssetOnDisk(PickedAsset);
}

TOptional<FAssetData> UFilterLoader::GetAssetLastSavedOrLoaded() const
{
	return AssetLastSavedOrLoaded;
}

void UFilterLoader::SetAssetBeingEdited(UDisjunctiveNormalFormFilter* NewAssetBeingEdited)
{
	AssetBeingEdited = NewAssetBeingEdited;
}

void UFilterLoader::OnSaveOrLoadAssetOnDisk(const FAssetData& AssetOnDisk)
{
	SetAssetLastSavedOrLoaded(AssetOnDisk);

	UDisjunctiveNormalFormFilter* FilterToUse = Cast<UDisjunctiveNormalFormFilter>(AssetOnDisk.GetAsset());
	// Duplicate to avoid referencing asset on disk: if user deletes the asset, this will leave editor with nulled references
	UDisjunctiveNormalFormFilter* DuplicatedFilter = DuplicateObject<UDisjunctiveNormalFormFilter>(FilterToUse, GetOutermost());
	// If user does Save as again later, this prevents FEditorFileUtils::SaveAssetsAs from suggesting an invalid file path to a transient package
	DuplicatedFilter->SetFlags(RF_Transient);
	OnUserSelectedLoadedFilters.Broadcast(DuplicatedFilter);
}

void UFilterLoader::SetAssetLastSavedOrLoaded(const FAssetData& NewSavedAsset)
{
	AssetLastSavedOrLoaded = NewSavedAsset;
	OnFilterWasSavedOrLoaded.Broadcast();
}