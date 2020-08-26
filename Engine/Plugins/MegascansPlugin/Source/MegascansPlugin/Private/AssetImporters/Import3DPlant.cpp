// Copyright Epic Games, Inc. All Rights Reserved.
#include "Import3DPlant.h"


#include "Utilities/AssetsDatabase.h"
#include "Utilities/MeshOp.h"
#include "AssetImportData.h"

#include "Runtime/Engine/Classes/Engine/StaticMesh.h"
#include "EditorAssetLibrary.h"
#include "EditorStaticMeshLibrary.h"


#include "Utilities/MiscUtils.h"
#include "UI/MSSettings.h"
#include "PerPlatformProperties.h"

TSharedPtr<FImportPlant> FImportPlant::ImportPlantInst;



TSharedPtr<FImportPlant> FImportPlant::Get()
{
	if (!ImportPlantInst.IsValid())
	{
		ImportPlantInst = MakeShareable(new FImportPlant);
	}
	return ImportPlantInst;
}

void FImportPlant::ImportAsset(TSharedPtr<FAssetTypeData> AssetImportData)
{
	if (AssetImportData->MeshList.Num() == 0) return;
	const UMegascansSettings* MegascansSettings = GetDefault<UMegascansSettings>();
	TSharedPtr<FAssetImportParams> AssetSetupParameters = FAssetImportParams::Get();
	TSharedPtr<ImportParams3DPlantAsset> AssetPlantParameters = AssetSetupParameters->Get3DPlantParams(AssetImportData);	
	PlantImportType ImportType = GetImportType(AssetImportData);
	TMap<FString, FString> ImportedPlants = ImportPlants(AssetImportData, AssetPlantParameters);
	FString LastLOD = AssetImportData->LodList[AssetImportData->LodList.Num() - 1]->Lod;
	FString MinLOD = (AssetImportData->AssetMetaInfo->MinLOD == TEXT("")) ? LastLOD : AssetImportData->AssetMetaInfo->MinLOD;
	int32 HighestLodIndex = FCString::Atoi(*MinLOD.Replace(TEXT("lod"), TEXT("")));
	HighestLodIndex = HighestLodIndex - FCString::Atoi(*AssetImportData->AssetMetaInfo->ActiveLOD.Replace(TEXT("lod"), TEXT("")));

	UMaterialInstanceConstant* BillboardMatInst = nullptr;
	UMaterialInstanceConstant* MaterialInstance = nullptr;

	if (ImportType == PlantImportType::BLLBOARD_NORMAL && !MegascansSettings->bEnableLods)
	{
		ImportType = PlantImportType::NORMAL_ONLY;
	}

	switch (ImportType)
	{
	case PlantImportType::BILLBOARD_ONLY:
		AssetImportData->TextureComponents.Empty();
		PopulateBillboardTextures(AssetImportData);	
		BillboardMatInst = FImportSurface::Get()->ImportSurface(AssetImportData, AssetPlantParameters->ParamsAssetType->BillboardMasterMaterial);
		
		ApplyMaterial(ImportType, BillboardMatInst, nullptr, ImportedPlants, AssetImportData->AssetMetaInfo->bSavePackages);
		break;

	case PlantImportType::BLLBOARD_NORMAL:
		
		MaterialInstance = FImportSurface::Get()->ImportSurface( AssetImportData, AssetPlantParameters->ParamsAssetType->PlantsMasterMaterial);
		AssetImportData->TextureComponents.Empty();
		PopulateBillboardTextures(AssetImportData);		
		BillboardMatInst = FImportSurface::Get()->ImportSurface(AssetImportData, AssetPlantParameters->ParamsAssetType->BillboardMasterMaterial);
		ApplyMaterial(ImportType, MaterialInstance, BillboardMatInst, ImportedPlants,  AssetImportData->AssetMetaInfo->bSavePackages);
		break;

	case PlantImportType::NORMAL_ONLY:
		MaterialInstance = FImportSurface::Get()->ImportSurface(AssetImportData, AssetPlantParameters->ParamsAssetType->PlantsMasterMaterial);
		ApplyMaterial(ImportType, MaterialInstance, nullptr, ImportedPlants, AssetImportData->AssetMetaInfo->bSavePackages);
		break;

	default:
		break;
	}

	AssetUtils::FocusOnSelected(AssetPlantParameters->BaseParams->AssetDestination);
	AssetRecord MSRecord;
	MSRecord.ID = AssetImportData->AssetMetaInfo->Id;
	MSRecord.Name = AssetPlantParameters->BaseParams->AssetName;
	MSRecord.Path = AssetPlantParameters->BaseParams->AssetDestination;
	MSRecord.Type = AssetImportData->AssetMetaInfo->Type;
	FAssetsDatabase::Get()->AddRecord(MSRecord);
}



PlantImportType FImportPlant::GetImportType(TSharedPtr<FAssetTypeData> AssetImportData)
{	
	FString LastLOD = AssetImportData->LodList[AssetImportData->LodList.Num() - 1]->Lod;
	FString MinLOD = (AssetImportData->AssetMetaInfo->MinLOD == TEXT("")) ? LastLOD : AssetImportData->AssetMetaInfo->MinLOD;
	FString CurrentLOD = (AssetImportData->AssetMetaInfo->ActiveLOD == TEXT("high")) ? TEXT("lod-1") : AssetImportData->AssetMetaInfo->ActiveLOD;	

	if (MinLOD == CurrentLOD && AssetImportData->BillboardTextures.Num() != 0)
	{		
		return PlantImportType::BILLBOARD_ONLY;
	}
	else if (LastLOD != MinLOD)
	{
		return PlantImportType::NORMAL_ONLY;
	}
	else {
		if(AssetImportData->BillboardTextures.Num() != 0) return PlantImportType::BLLBOARD_NORMAL;
		else return PlantImportType::NORMAL_ONLY;
	}
}




void FImportPlant::PopulateBillboardTextures(TSharedPtr<FAssetTypeData> AssetImportData)
{
	for (auto BillboardComponent : AssetImportData->BillboardTextures)
	{
		TSharedPtr<FAssetTextureData> TextureComponent = MakeShareable(new FAssetTextureData);
		TextureComponent->Name = FPaths::GetCleanFilename(BillboardComponent->Path);
		TextureComponent->NameOverride = TextureComponent->Name;
		TextureComponent->Path = BillboardComponent->Path;
		TextureComponent->Format = FPaths::GetExtension(BillboardComponent->Path);
		TextureComponent->Type = BillboardComponent->Type;
		AssetImportData->TextureComponents.Add(TextureComponent);
	}

}

void FImportPlant::SetBillboardImportParams(TSharedPtr<SurfaceImportParams> SImportParams)
{
	FString BillboardsFolder = TEXT("Billboards");
	SImportParams->MInstanceDestination = FPaths::Combine(SImportParams->MInstanceDestination, BillboardsFolder);
	SImportParams->TexturesDestination = FPaths::Combine(SImportParams->TexturesDestination, BillboardsFolder);
	SImportParams->MInstanceName = SImportParams->AssetName + TEXT("_billboard_inst");
}

TMap<FString, FString> FImportPlant::ImportPlants(TSharedPtr<FAssetTypeData> AssetImportData, TSharedPtr<ImportParams3DPlantAsset> AssetPlantParameters)
{
	const UMegascansSettings* MegascansSettings = GetDefault<UMegascansSettings>();
	TArray<FString> AllLodList = ParseLodList(AssetImportData);

	TArray<TSharedPtr<FAssetLodData>> SelectedLods = ParsePlantsLodList(AssetImportData);

	TMap<FString, FString> ImportedPlantVariations;

	FString FileExtension;
	FString FilePath;
	AssetImportData->MeshList[0]->Path.Split(TEXT("."), &FilePath, &FileExtension);
	FileExtension = FPaths::GetExtension(AssetImportData->MeshList[0]->Path);

	int32 VarCounter = 1;
	
	for (auto PlantVar : AssetImportData->MeshList)
	{	

		FString VariantPath = FMeshOps::Get()->ImportMesh(PlantVar->Path, AssetPlantParameters->ParamsAssetType->MeshDestination);
		if (!UEditorAssetLibrary::DoesAssetExist(VariantPath)) continue;
		UStaticMesh* ImportedAsset = CastChecked<UStaticMesh>(LoadAsset(VariantPath));
		FString VariantName = FPaths::GetBaseFilename(VariantPath);
		FString Variant, LodNumber;
		VariantName.Split(TEXT("_"), &Variant, &LodNumber);
		TArray<FString> VarLodList;
		if (MegascansSettings->bEnableLods && AssetImportData->LodList.Num() > 0)
		{
			// Alternate implementation for TArray based variable, gives more control
			//TMap<FString, TSharedPtr<FAssetLodData>> VariationLodsImported;			
			TArray<FString> VariationLodsImported;
			VariationLodsImported.Add(AssetImportData->AssetMetaInfo->ActiveLOD);
			
			for(TSharedPtr<FAssetLodData> LodData : SelectedLods)
			{
				FString LodBaseFilename = FPaths::GetBaseFilename(LodData->Path);

				FString VariantTag;
				FString LodTag;

				LodBaseFilename.Split(TEXT("_"), &VariantTag, &LodTag);
				if (VariantTag == Variant)
				{
					VariationLodsImported.Add(LodData->Lod);
					//VariationLodsImported.Add(LodData->Lod, LodData);					
					VarLodList.Add(LodData->Path);
				}
			}


			if (FileExtension == "abc")
			{
				FMeshOps::Get()->ApplyAbcLods(ImportedAsset, VarLodList, AssetPlantParameters->ParamsAssetType->MeshDestination);
			}
			else {

				FMeshOps::Get()->ApplyLods(VarLodList, ImportedAsset);
			}			
			//Apply the custom lod screen sizes.
			if (AssetImportData->PlantsLodScreenSizes.Contains(Variant))
			{
				SetLodScreenSizes(ImportedAsset, AssetImportData->PlantsLodScreenSizes[Variant], VariationLodsImported);
			}
		}
		
		FString OverridenName;
		FString OverridenExtension;
		PlantVar->NameOverride.Split(TEXT("."), &OverridenName, &OverridenExtension);
		OverridenName = SanitizeName(OverridenName);
		FString PlantAssetName = OverridenName + TEXT("_") + FString::FromInt(VarCounter);
		VarCounter++;
		if (MegascansSettings->bCreateFoliage)
		{
			FMeshOps::Get()->CreateFoliageAsset(AssetPlantParameters->ParamsAssetType->MeshDestination, ImportedAsset, PlantAssetName);
		}
		ImportedAsset->MarkPackageDirty();
		
		FString RenamedAssetPath = FPaths::Combine(FPaths::GetPath(VariantPath), PlantAssetName);
		//ImportedPlants.Add(RenamedAssetPath);
		FString Variation;
		FString Name;
		PlantVar->Name.Split(TEXT("_"), &Variation, &Name);
		ImportedPlantVariations.Add(Variation, RenamedAssetPath);
		UEditorAssetLibrary::RenameAsset(VariantPath, RenamedAssetPath);

		if (AssetImportData->AssetMetaInfo->bSavePackages)
		{
			AssetUtils::SavePackage(ImportedAsset);
		}
	}
	return ImportedPlantVariations;
}

void FImportPlant::ApplyMaterial(PlantImportType ImportType, UMaterialInstanceConstant* MaterialInstance, UMaterialInstanceConstant* BillboardInstance, TMap<FString, FString> ImportedPlants,  bool bSavePackage)
{	
	
	for (const TPair<FString, FString>& PlantVar : ImportedPlants  )
	{
		UStaticMesh* ImportedPlantMesh = CastChecked<UStaticMesh>(UEditorAssetLibrary::LoadAsset(PlantVar.Value));
		if(MaterialInstance !=nullptr)
			ImportedPlantMesh->SetMaterial(0, CastChecked<UMaterialInterface>(MaterialInstance));

		if (ImportType == PlantImportType::BLLBOARD_NORMAL && BillboardInstance != nullptr)
		{
			
			AssetUtils::AddStaticMaterial(ImportedPlantMesh, BillboardInstance);			
			FMeshSectionInfo MeshSectionInfo = ImportedPlantMesh->GetSectionInfoMap().Get(ImportedPlantMesh->GetNumLODs()-1, 0);
			MeshSectionInfo.MaterialIndex = ImportedPlantMesh->StaticMaterials.Num() - 1;			
			ImportedPlantMesh->GetSectionInfoMap().Set(ImportedPlantMesh->GetNumLODs() - 1, 0, MeshSectionInfo);
			ImportedPlantMesh->Modify();
			ImportedPlantMesh->PostEditChange();
			ImportedPlantMesh->MarkPackageDirty();


		}

		if (bSavePackage)
		{
			AssetUtils::SavePackage(ImportedPlantMesh);
		}

	}

}

void FImportPlant::SetLodScreenSizes(TSharedPtr<FAssetTypeData> AssetImportData, TMap<FString, FString> ImportedPlants)
{
	

}

void FImportPlant::SetLodScreenSizes(UStaticMesh* SourceMesh, TMap<FString, float>& LodScreenSizes, const TArray<FString>& SelectedLods)
{
	//Check if lod screen size information exist in the json.
	if (LodScreenSizes.Contains(TEXT("lod0")) && LodScreenSizes[TEXT("lod0")] == 0) return;

	//Check if any lods are skipped.
	TArray<int8> ImportedLods;
	int8 ActiveLod = FCString::Atoi(*SelectedLods[0].Replace(TEXT("lod"), TEXT("")));
	bool bSequence = true;

	for (int8 i = 1 ; i < SelectedLods.Num()-1; i++)
	{
		int8 CurrentLod = FCString::Atoi(*SelectedLods[i].Replace(TEXT("lod"), TEXT("")));
		if (CurrentLod != ActiveLod + 1) {
			bSequence = false;
			break;
		}
		ActiveLod = CurrentLod;
	}


	
	if (bSequence)
	{
		if (SelectedLods[0] != TEXT("lod0") && LodScreenSizes.Contains(SelectedLods[0]))
		{
			LodScreenSizes.Remove(SelectedLods[0]);
			LodScreenSizes.Add(SelectedLods[0], 1.0);			
		}
		int8 count = 0;
		SourceMesh->bAutoComputeLODScreenSize = false;
		for (FString Lod : SelectedLods)
		{
			FStaticMeshSourceModel& SourceModel = SourceMesh->GetSourceModel(count);
			if (LodScreenSizes.Contains(Lod)) {
				SourceModel.ScreenSize = FPerPlatformFloat(LodScreenSizes[Lod]);
			}
			else {
				SourceMesh->bAutoComputeLODScreenSize = true;
				return;
			}

			count++;
		}
	}


	
}
