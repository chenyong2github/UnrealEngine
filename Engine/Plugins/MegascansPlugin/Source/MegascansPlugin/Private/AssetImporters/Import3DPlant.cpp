#include "Import3DPlant.h"


#include "AssetImporters/ImportSurface.h"
#include "Utilities/MeshOp.h"
#include "AssetImportData.h"

#include "Runtime/Engine/Classes/Engine/StaticMesh.h"
#include "EditorAssetLibrary.h"
#include "EditorStaticMeshLibrary.h"


#include "Utilities/MiscUtils.h"
#include "UI/MSSettings.h"

TSharedPtr<FImportPlant> FImportPlant::ImportPlantInst;



TSharedPtr<FImportPlant> FImportPlant::Get()
{
	if (!ImportPlantInst.IsValid())
	{
		ImportPlantInst = MakeShareable(new FImportPlant);
	}
	return ImportPlantInst;
}

void FImportPlant::ImportPlant(TSharedPtr<F3dPlantPreferences> Type3dPlantPrefs, TSharedPtr<FAssetTypeData> AssetImportData, UMaterialInstanceConstant* MaterialInstance)
{

	if (AssetImportData->MeshList.Num() == 0) return;

	UMaterialInstanceConstant* BillboardMatInst = nullptr;
	TSharedPtr<FSurfacePreferences> TypeSurfacePrefs = GetSurfacePrefs(Type3dPlantPrefs);
	TSharedPtr<SurfaceImportParams> SImportParams = FImportSurface::Get()->GetSurfaceImportParams(TypeSurfacePrefs, AssetImportData);

	FString RootDestination = GetRootDestination(AssetImportData->AssetMetaInfo->ExportPath);
	FString MeshDestination = FPaths::Combine(RootDestination, ResolvePath(Type3dPlantPrefs->DestinationPrefs->MeshDestinationPath, AssetImportData));

	SImportParams->MeshDestination = FPaths::Combine(MeshDestination, SImportParams->AssetName);

	PlantImportType ImportType = GetImportType(AssetImportData);

	TArray<FString> ImportedPlants = ImportPlants(AssetImportData, SImportParams);

	FString LastLOD = AssetImportData->LodList[AssetImportData->LodList.Num() - 1]->Lod;
	FString MinLOD = (AssetImportData->AssetMetaInfo->MinLOD == TEXT("")) ? LastLOD : AssetImportData->AssetMetaInfo->MinLOD;
	int32 HighestLodIndex = FCString::Atoi(*MinLOD.Replace(TEXT("lod"), TEXT("")));
	
	switch (ImportType)
	{
	case PlantImportType::BILLBOARD_ONLY:
	
		AssetImportData->TextureComponents.Empty();		
		PopulateBillboardTextures(AssetImportData);	

		BillboardMatInst = FImportSurface::Get()->ImportSurface(TypeSurfacePrefs, AssetImportData, SImportParams);
		ApplyMaterial(ImportType, BillboardMatInst, nullptr, ImportedPlants);
		break;

	case PlantImportType::BLLBOARD_NORMAL:

		MaterialInstance = FImportSurface::Get()->ImportSurface(TypeSurfacePrefs, AssetImportData, SImportParams);
		AssetImportData->TextureComponents.Empty();
		PopulateBillboardTextures(AssetImportData);
		SetBillboardImportParams(SImportParams);
		BillboardMatInst = FImportSurface::Get()->ImportSurface(TypeSurfacePrefs, AssetImportData, SImportParams);

		
		ApplyMaterial(ImportType, MaterialInstance, BillboardMatInst, ImportedPlants, HighestLodIndex);

		break;

	case PlantImportType::NORMAL_ONLY:
	
		MaterialInstance = FImportSurface::Get()->ImportSurface(TypeSurfacePrefs, AssetImportData, SImportParams);
		ApplyMaterial(ImportType, MaterialInstance, nullptr, ImportedPlants);
		break;

	default:
	
		break;

	}
	
	
	AssetUtils::FocusOnSelected(SImportParams->MeshDestination);	
	

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


TSharedPtr<FSurfacePreferences> FImportPlant::GetSurfacePrefs(TSharedPtr<F3dPlantPreferences> Type3dPlantPrefs)
{
	TSharedPtr<FSurfacePreferences> TypeSurfacePrefs = MakeShareable(new FSurfacePreferences);
	TypeSurfacePrefs->DestinationPrefs = Type3dPlantPrefs->DestinationPrefs;
	TypeSurfacePrefs->MaterialPrefs = Type3dPlantPrefs->MaterialPrefs;
	TypeSurfacePrefs->TexturePrefs = Type3dPlantPrefs->TexturePrefs;
	TypeSurfacePrefs->RenamePrefs = Type3dPlantPrefs->RenamePrefs;


	return TypeSurfacePrefs;
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

TArray<FString> FImportPlant::ImportPlants(TSharedPtr<FAssetTypeData> AssetImportData, TSharedPtr<SurfaceImportParams> SImportParams)
{
	const UMegascansSettings* MegascansSettings = GetDefault<UMegascansSettings>();
	TArray<FString> AllLodList = ParseLodList(AssetImportData);

	FString FileExtension;
	FString FilePath;
	AssetImportData->MeshList[0]->Path.Split(TEXT("."), &FilePath, &FileExtension);
	FileExtension = FPaths::GetExtension(AssetImportData->MeshList[0]->Path);

	int32 VarCounter = 1;
	TArray<FString> ImportedPlants;
	for (auto PlantVar : AssetImportData->MeshList)
	{

		FString VariantPath = FMeshOps::Get()->ImportMesh(PlantVar->Path, SImportParams->MeshDestination);
		if (!UEditorAssetLibrary::DoesAssetExist(VariantPath)) continue;

		UStaticMesh* ImportedAsset = CastChecked<UStaticMesh>(LoadAsset(VariantPath));


		FString VariantName = FPaths::GetBaseFilename(VariantPath);

		FString Variant, LodNumber;
		VariantName.Split(TEXT("_"), &Variant, &LodNumber);

		TArray<FString> VarLodList;


		if (MegascansSettings->bEnableLods && AssetImportData->LodList.Num() > 0)
		{
			for (FString LodPath : AllLodList)
			{
				FString LodBaseFilename = FPaths::GetBaseFilename(LodPath);

				FString VariantTag;
				FString LodTag;

				LodBaseFilename.Split(TEXT("_"), &VariantTag, &LodTag);
				if (VariantTag == Variant)
				{
					VarLodList.Add(LodPath);
				}
			}


			if (FileExtension == "abc")
			{
				FMeshOps::Get()->ApplyAbcLods(ImportedAsset, VarLodList, SImportParams->MeshDestination);
			}
			else {

				FMeshOps::Get()->ApplyLods(VarLodList, ImportedAsset);
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

			FMeshOps::Get()->CreateFoliageAsset(SImportParams->MeshDestination, ImportedAsset, PlantAssetName);
		}

		ImportedAsset->MarkPackageDirty();

		FString RenamedAssetPath = FPaths::Combine(FPaths::GetPath(VariantPath), PlantAssetName);
		ImportedPlants.Add(RenamedAssetPath);

		UEditorAssetLibrary::RenameAsset(VariantPath, RenamedAssetPath);


	}
	return ImportedPlants;
}

void FImportPlant::ApplyMaterial(PlantImportType ImportType, UMaterialInstanceConstant* MaterialInstance, UMaterialInstanceConstant* BillboardInstance, TArray<FString> ImportedPlants, int32 BillboardLodIndex)
{	
	
	for (FString PlantPath : ImportedPlants)
	{
		UStaticMesh* ImportedPlantMesh = CastChecked<UStaticMesh>(UEditorAssetLibrary::LoadAsset(PlantPath));
		if(MaterialInstance !=nullptr)
			ImportedPlantMesh->SetMaterial(0, CastChecked<UMaterialInterface>(MaterialInstance));

		if (ImportType == PlantImportType::BLLBOARD_NORMAL && BillboardInstance != nullptr)
		{

			AssetUtils::AddStaticMaterial(ImportedPlantMesh, BillboardInstance);
			FMeshSectionInfo MeshSectionInfo = ImportedPlantMesh->SectionInfoMap.Get(BillboardLodIndex, 0);
			//FMeshSectionInfo MeshSectionInfo = ImportedPlantMesh->GetSectionInfoMap().Get(BillboardLodIndex, 0);
			
			MeshSectionInfo.MaterialIndex = ImportedPlantMesh->StaticMaterials.Num() - 1;
			ImportedPlantMesh->SectionInfoMap.Set(BillboardLodIndex, 0, MeshSectionInfo);
			//ImportedPlantMesh->GetSectionInfoMap().Set(BillboardLodIndex, 0, MeshSectionInfo);
			
			ImportedPlantMesh->Modify();
			ImportedPlantMesh->PostEditChange();
			ImportedPlantMesh->MarkPackageDirty();
		}

	}

}
