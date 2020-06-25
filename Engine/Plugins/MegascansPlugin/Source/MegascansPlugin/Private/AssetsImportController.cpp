#include "AssetsImportController.h"
#include "AssetImportDataHandler.h"
#include "AssetImporters/ImportSurface.h"
#include "AssetImporters/Import3D.h"
#include "AssetImporters/Import3DPlant.h"
#include "UI/MSSettings.h"

#include "Runtime/Core/Public/Misc/MessageDialog.h"
#include "Runtime/Core/Public/Internationalization/Text.h"


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
	if (IsGarbageCollecting() || GIsSavingPackage) return;	

	ImportAssets(DataFromBridge);

	
	
}


// Gets import preferences from a json file, parses the Bridge json and calls the appropriate import function based in asset type
void FAssetsImportController::ImportAssets(const FString& AssetsImportJson)
{
	TSharedPtr<FAssetPreferencesHandler> PrefHandler = FAssetPreferencesHandler::Get();
	TSharedPtr<F3DPreferences> Type3dPrefs = nullptr;
	TSharedPtr<F3dPlantPreferences> Type3dPlantPrefs = nullptr;
	TSharedPtr<FSurfacePreferences> TypeSurfacePrefs = nullptr;

	TSharedPtr<FAssetsData> AssetsImportData = FAssetDataHandler::Get()->GetAssetsData(AssetsImportJson);
	
	
	checkf(AssetsImportData->AllAssetsData.Num() > 0, TEXT("There was an error reading asset data."));
	UE_LOG(MSLiveLinkLog, Log, TEXT("Successfully read asset json data"));
	
	const UMegascansSettings* MegascansSettings = GetDefault<UMegascansSettings>();

	if (AssetsImportData->AllAssetsData.Num() > 10)
	{
		if (MegascansSettings->bBatchImportPrompt)
		{			
			EAppReturnType::Type ContinueImport = FMessageDialog::Open(EAppMsgType::OkCancel, FText(FText::FromString("You are about to download more than 10 assets. Press Ok to continue.")));
			if (ContinueImport == EAppReturnType::Cancel) return;
		}
	}

	

	for (TSharedPtr<FAssetTypeData> AssetImportData : AssetsImportData->AllAssetsData)
	{

		

		if (AssetImportData->AssetMetaInfo->Type == "3d")
		{
			if (!Type3dPrefs.IsValid())
			{
				Type3dPrefs = PrefHandler->Get3dPreferences();
			}

			FImport3d::Get()->Import3d(Type3dPrefs, AssetImportData);

			//TSharedPtr<FImport3d> Import3dInst = FImport3d::Get();
			//Import3dInst->Import3d(Type3dPrefs, AssetImportData);
			//Import3dInst.Reset();

		}

		else if (AssetImportData->AssetMetaInfo->Type == "3dplant")
		{
			if (!Type3dPlantPrefs.IsValid())
			{
				Type3dPlantPrefs = PrefHandler->Get3dPlantPreferences();
			}

			FImportPlant::Get()->ImportPlant(Type3dPlantPrefs, AssetImportData);

			//TSharedPtr<FImportPlant> Import3dPlantInst = FImportPlant::Get();
			//Import3dPlantInst->ImportPlant(Type3dPlantPrefs, AssetImportData);
			//Import3dPlantInst.Reset();
		}

		else if (AssetImportData->AssetMetaInfo->Type == "surface")
		{
			if (!TypeSurfacePrefs.IsValid())
			{
				TypeSurfacePrefs = PrefHandler->GetSurfacePreferences();
			}

			TSharedPtr<SurfaceImportParams> SImportParams = FImportSurface::Get()->GetSurfaceImportParams(TypeSurfacePrefs, AssetImportData);
			FImportSurface::Get()->ImportSurface(TypeSurfacePrefs, AssetImportData, SImportParams);

			//TSharedPtr<FImportSurface> ImportSurfaceInst = FImportSurface::Get();
			//ImportSurfaceInst->ImportSurface(TypeSurfacePrefs, AssetImportData, SImportParams);
			//ImportSurfaceInst.Reset();

		}

		else if (AssetImportData->AssetMetaInfo->Type == "atlas")
		{
			if (!TypeSurfacePrefs.IsValid())
			{
				TypeSurfacePrefs = PrefHandler->GetSurfacePreferences();
			}
			TSharedPtr<SurfaceImportParams> SImportParams = FImportSurface::Get()->GetSurfaceImportParams(TypeSurfacePrefs, AssetImportData);
			FImportSurface::Get()->ImportSurface(TypeSurfacePrefs, AssetImportData, SImportParams);
		}

		else if (AssetImportData->AssetMetaInfo->Type == "brush")
		{
			if (!TypeSurfacePrefs.IsValid())
			{
				TypeSurfacePrefs = PrefHandler->GetSurfacePreferences();
			}
			TSharedPtr<SurfaceImportParams> SImportParams = FImportSurface::Get()->GetSurfaceImportParams(TypeSurfacePrefs, AssetImportData);
			FImportSurface::Get()->ImportSurface(TypeSurfacePrefs, AssetImportData, SImportParams);
		}

	}
	PrefHandler.Reset();
	AssetsImportData.Reset();

	if(FImport3d::Get().IsValid())
		FImport3d::Get().Reset();

	if (FImportPlant::Get().IsValid())	
		FImportPlant::Get().Reset();

	if (FImportSurface::Get().IsValid())
		FImportSurface::Get().Reset();


}