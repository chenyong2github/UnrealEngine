// Copyright Epic Games, Inc. All Rights Reserved.

#include "FilterLoader.h"

#include "DisjunctiveNormalFormFilter.h"
#include "EditorDirectories.h"

#include "FileHelpers.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/Paths.h"

void UFilterLoader::OverwriteExisting()
{
	const TOptional<FAssetData> AssetData = GetAssetLastSavedOrLoaded();
	if (!ensure(AssetData.IsSet()))
	{
		return;
	}

	// duplicate asset at destination
	UObject* AssetDuplicatedAtCorrectPath = [this, &AssetData]()
	{
		const FString NewPackageName = *AssetData->PackageName.ToString();
		UPackage* DuplicatedPackage = CreatePackage(*NewPackageName);
		UObject* DuplicatedAsset = StaticDuplicateObject(AssetBeingEdited, DuplicatedPackage, *AssetData->AssetName.ToString());

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

	UDisjunctiveNormalFormFilter* Filter = Cast<UDisjunctiveNormalFormFilter>(AssetDuplicatedAtCorrectPath);
	if (ensure(Filter))
	{
		OnSaveOrLoadAssetOnDisk(Filter);
	}
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
		OnSaveOrLoadAssetOnDisk(Cast<UDisjunctiveNormalFormFilter>(SavedAssetOnDisk));
	}
}

void UFilterLoader::LoadAsset(const FAssetData& PickedAsset)
{
	UObject* LoadedAsset = PickedAsset.GetAsset();
	if (!ensure(LoadedAsset) || !ensure(Cast<UDisjunctiveNormalFormFilter>(LoadedAsset)))
	{
		return;
	}
	
	UDisjunctiveNormalFormFilter* Filter = Cast<UDisjunctiveNormalFormFilter>(LoadedAsset);
	if (ensure(Filter))
	{
		FScopedTransaction Transaction(FText::FromString("Load filter preset"));
		Modify();
		OnSaveOrLoadAssetOnDisk(Filter);
	}
}

TOptional<FAssetData> UFilterLoader::GetAssetLastSavedOrLoaded() const
{
	UObject* Result = AssetLastSavedOrLoaded.TryLoad();
	return Result ? FAssetData(Result) : TOptional<FAssetData>();
}

void UFilterLoader::PostTransacted(const FTransactionObjectEvent& TransactionEvent)
{
	Super::PostTransacted(TransactionEvent);

	if (TransactionEvent.GetEventType() == ETransactionObjectEventType::UndoRedo)
	{
		OnFilterChanged.Broadcast(AssetBeingEdited);
	}
}

void UFilterLoader::SetAssetBeingEdited(UDisjunctiveNormalFormFilter* NewAssetBeingEdited)
{
	AssetBeingEdited = NewAssetBeingEdited;
}

void UFilterLoader::OnSaveOrLoadAssetOnDisk(UDisjunctiveNormalFormFilter* AssetOnDisk)
{
	SetAssetLastSavedOrLoaded(AssetOnDisk);
	
	// Duplicate to avoid referencing asset on disk: if user deletes the asset, this will leave editor with nulled references
	UDisjunctiveNormalFormFilter* DuplicatedFilter = DuplicateObject<UDisjunctiveNormalFormFilter>(AssetOnDisk, GetOutermost());
	// If user does Save as again later, this prevents FEditorFileUtils::SaveAssetsAs from suggesting an invalid file path to a transient package
	DuplicatedFilter->SetFlags(RF_Transient);

	SetAssetBeingEdited(DuplicatedFilter);
	OnFilterChanged.Broadcast(DuplicatedFilter);
}

void UFilterLoader::SetAssetLastSavedOrLoaded(UDisjunctiveNormalFormFilter* NewSavedAsset)
{
	AssetLastSavedOrLoaded = NewSavedAsset;
	OnFilterWasSavedOrLoaded.Broadcast();
}