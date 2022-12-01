// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions_ClothAsset.h"
#include "ThumbnailRendering/SceneThumbnailInfo.h"
#include "ToolMenus.h"
#include "ChaosClothAsset/ClothEditor.h"
#include "ChaosClothAsset/ClothAsset.h"
#include "Dataflow/DataflowObject.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "FileHelpers.h"

#define LOCTEXT_NAMESPACE "FAssetTypeActions_ClothAsset"

namespace UE::Chaos::ClothAsset
{
	namespace ClothAssetActionsHelpers
	{
		// Create a new UDataflow if one doesn't already exist for the Cloth Asset
		UObject* CreateNewDataflowAsset(const UChaosClothAsset* ClothAsset)
		{
			if (FMessageDialog::Open(EAppMsgType::YesNo, LOCTEXT("MissingDataflow", "This Cloth asset currently has no Dataflow graph. Would you like to create a new one?")) == EAppReturnType::Yes)
			{
				const UClass* const DataflowClass = UDataflow::StaticClass();

				FSaveAssetDialogConfig NewDataflowAssetDialogConfig;
				{
					const FString PackageName = ClothAsset->GetOutermost()->GetName();
					NewDataflowAssetDialogConfig.DefaultPath = FPackageName::GetLongPackagePath(PackageName);
					NewDataflowAssetDialogConfig.AssetClassNames.Add(DataflowClass->GetClassPathName());
					NewDataflowAssetDialogConfig.ExistingAssetPolicy = ESaveAssetDialogExistingAssetPolicy::AllowButWarn;
					NewDataflowAssetDialogConfig.DialogTitleOverride = LOCTEXT("NewDataflowAssetDialogTitle", "Save Dataflow Asset As");
				}

				FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

				FString NewPackageName;
				FText OutError;
				for (bool bFilenameValid = false; !bFilenameValid; bFilenameValid = FFileHelper::IsFilenameValidForSaving(NewPackageName, OutError))
				{
					const FString AssetSavePath = ContentBrowserModule.Get().CreateModalSaveAssetDialog(NewDataflowAssetDialogConfig);
					if (AssetSavePath.IsEmpty())
					{
						return nullptr;
					}
					NewPackageName = FPackageName::ObjectPathToPackageName(AssetSavePath);
				}

				const FName NewAssetName(FPackageName::GetLongPackageAssetName(NewPackageName));
				UPackage* const NewPackage = CreatePackage(*NewPackageName);
				UObject* const NewAsset = NewObject<UObject>(NewPackage, DataflowClass, NewAssetName, RF_Public | RF_Standalone | RF_Transactional);

				NewAsset->MarkPackageDirty();

				// Notify the asset registry
				FAssetRegistryModule::AssetCreated(NewAsset);

				// Save the package
				TArray<UPackage*> PackagesToSave;
				PackagesToSave.Add(NewAsset->GetOutermost());
				constexpr bool bCheckDirty = false;
				constexpr bool bPromptToSave = false;
				FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, bCheckDirty, bPromptToSave);

				return NewAsset;
			}

			return nullptr;
		}
	}


	FText FAssetTypeActions_ClothAsset::GetName() const
	{
		return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_ClothAsset", "Cloth Asset");
	}

	void FAssetTypeActions_ClothAsset::GetActions(const TArray<UObject*>& Objects, FToolMenuSection& Section)
	{
		FAssetTypeActions_Base::GetActions(Objects, Section);
	}

	FColor FAssetTypeActions_ClothAsset::GetTypeColor() const
	{
		return FColor(180, 120, 110);
	}

	UClass* FAssetTypeActions_ClothAsset::GetSupportedClass() const
	{
		return UChaosClothAsset::StaticClass();
	}

	void FAssetTypeActions_ClothAsset::OpenAssetEditor(const TArray<UObject*>& Objects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
	{
		TArray<TObjectPtr<UObject>> ClothObjects;

		for (UObject* const Object : Objects)
		{
			if (Cast<UChaosClothAsset>(Object))
			{
				ClothObjects.Add(Object);
				break;
			}
		}

		// For now the cloth editor only works on one asset at a time
		ensure(ClothObjects.Num() == 0 || ClothObjects.Num() == 1);

		if (ClothObjects.Num() > 0)
		{
			UAssetEditorSubsystem* const AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			UChaosClothAssetEditor* const AssetEditor = NewObject<UChaosClothAssetEditor>(AssetEditorSubsystem, NAME_None, RF_Transient);

			// Validate the asset
			UChaosClothAsset* const ClothAsset = CastChecked<UChaosClothAsset>(ClothObjects[0]);

			if (!ClothAsset->DataflowAsset)
			{
				if (UObject* const NewAsset = ClothAssetActionsHelpers::CreateNewDataflowAsset(ClothAsset))
				{
					ClothAsset->DataflowAsset = CastChecked<UDataflow>(NewAsset);
				}
			}

			AssetEditor->Initialize(ClothObjects);
		}
	}

	uint32 FAssetTypeActions_ClothAsset::GetCategories()
	{
		return EAssetTypeCategories::Physics;
	}

	UThumbnailInfo* FAssetTypeActions_ClothAsset::GetThumbnailInfo(UObject* Asset) const
	{
		check(Cast<UChaosClothAsset>(Asset));
		return NewObject<USceneThumbnailInfo>(Asset, NAME_None, RF_Transactional);
	}
} // End namespace UE::Chaos::ClothAsset


#undef LOCTEXT_NAMESPACE
