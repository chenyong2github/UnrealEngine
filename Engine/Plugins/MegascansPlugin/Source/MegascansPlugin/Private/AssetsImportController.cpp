// Copyright Epic Games, Inc. All Rights Reserved.
#include "AssetsImportController.h"
#include "AssetImportDataHandler.h"
#include "AssetImporters/ImportSurface.h"
#include "AssetImporters/Import3D.h"
#include "AssetImporters/Import3DPlant.h"
#include "UI/MSSettings.h"
#include "Utilities/AssetsDatabase.h"
#include "Utilities/Analytics.h"
#include "Utilities/AssetData.h"
#include "AssetImporters/ImportFactory.h"

#include "Misc/MessageDialog.h"
#include "Runtime/Core/Public/Internationalization/Text.h"
#include "Misc/Paths.h"


TSharedPtr<FAssetsImportController> FAssetsImportController::AssetsImportController;


// Get instance of the class
TSharedPtr<FAssetsImportController> FAssetsImportController::Get()
{
	if (!AssetsImportController.IsValid())
	{
		AssetsImportController = MakeShareable(new FAssetsImportController);	
	}
	return AssetsImportController;
}



void FAssetsImportController::DataReceived(const FString DataFromBridge)
{
	
	//UE_LOG(LogTemp, Error, TEXT("Data from Bridge :%s"), *DataFromBridge);
	if (IsGarbageCollecting() || GIsSavingPackage) return;
	TArray<FDHIData> DHIAssetsData;
	if (DHI::GetDHIJsonData(DataFromBridge, DHIAssetsData)) {
		for (FDHIData CharacterData : DHIAssetsData) {
			DHI::CopyCharacter(CharacterData);	
		}
	}
	else ImportAssets(DataFromBridge);	
}

// Gets import preferences from a json file, parses the Bridge json and calls the appropriate import function based in asset type
void FAssetsImportController::ImportAssets(const FString& AssetsImportJson)
{
	bool bSavePackages = false;
	bool bSkipImportAll = false;
	bool bImportAll = false;
	bool bAllSkipOrImport = false;

	TSharedPtr<FAssetsData> AssetsImportData = FAssetDataHandler::Get()->GetAssetsData(AssetsImportJson);
	

	checkf(AssetsImportData->AllAssetsData.Num() > 0, TEXT("There was an error reading asset data."));	
	const UMegascansSettings* MegascansSettings = GetDefault<UMegascansSettings>();
	if (AssetsImportData->AllAssetsData.Num() > 10)
	{
		bSavePackages = true;
		if (MegascansSettings->bBatchImportPrompt)
		{			
			EAppReturnType::Type ContinueImport = FMessageDialog::Open(EAppMsgType::OkCancel, FText(FText::FromString("You are about to download more than 10 assets. Press Ok to continue.")));
			if (ContinueImport == EAppReturnType::Cancel) return;
			
		}
	}

	TSharedPtr<FJsonObject> UIAnalytics = FAnalytics::Get()->GenerateAnalyticsJson();
	FAnalytics::Get()->SendAnalytics(UIAnalytics);
	

	for (TSharedPtr<FAssetTypeData> AssetImportData : AssetsImportData->AllAssetsData)
	{	
		//AssetImportData->AssetMetaInfo->bSavePackages = bSavePackages;
		AssetImportData->AssetMetaInfo->bSavePackages = false;
		TSharedPtr<FAssetImportParams> AssetSetupParameters = FAssetImportParams::Get();
		AssetRecord Record;
		if (FAssetsDatabase::Get()->RecordExists(AssetImportData->AssetMetaInfo->Id, Record) && FPaths::DirectoryExists(FPaths::Combine(FPaths::ProjectContentDir(), Record.Path.Replace(TEXT("/Game"), TEXT("")))))
		{
			if (bSkipImportAll)
			{
				continue;
			}
			if (!bAllSkipOrImport)
			{
				EAppReturnType::Type ReimportAssetDlg = FMessageDialog::Open(EAppMsgType::YesNoYesAllNoAll, FText(FText::FromString(FString::Printf(TEXT("The asset %s already exists at %s. Do you want to import this asset.?"), *AssetImportData->AssetMetaInfo->Name, *Record.Path))));
				if (ReimportAssetDlg == EAppReturnType::No) {
					continue;
				}
				if (ReimportAssetDlg == EAppReturnType::NoAll)
				{
					bSkipImportAll = true;
					bAllSkipOrImport = true;
					continue;
				}
				if (ReimportAssetDlg == EAppReturnType::YesAll)
				{
					bAllSkipOrImport = true;
					bImportAll = true;
				}
			}


		}
		if (AssetImportData->AssetMetaInfo->Type == "3d")
		{
			
			FImport3d::Get()->ImportAsset(AssetImportData);			
		}
		else if (AssetImportData->AssetMetaInfo->Type == "3dplant")
		{
			FImportPlant::Get()->ImportAsset(AssetImportData);
		}

		else if (AssetImportData->AssetMetaInfo->Type == "surface" || AssetImportData->AssetMetaInfo->Type == "atlas" || AssetImportData->AssetMetaInfo->Type == "brush")
		{			
			FImportSurface::Get()->ImportAsset(AssetImportData);
		}

	}	
	AssetsImportData.Reset();	
	FImport3d::Get().Reset();	
	FImportPlant::Get().Reset();
	FImportSurface::Get().Reset();
}