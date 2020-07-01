#include "Import3D.h"

#include "AssetImporters/ImportSurface.h"
#include "AssetImportData.h"
#include "Runtime/Engine/Classes/Engine/StaticMesh.h"
#include "Utilities/MeshOp.h"
#include "Utilities/MiscUtils.h"

#include "EditorAssetLibrary.h"
#include "EditorStaticMeshLibrary.h"
#include "UI/MSSettings.h"
#include "Runtime/Core/Public/Misc/MessageDialog.h"
#include "Runtime/Core/Public/Internationalization/Text.h"



TSharedPtr<FImport3d> FImport3d::Import3dInst;


TSharedPtr<FImport3d> FImport3d::Get()
{
	if (!Import3dInst.IsValid())
	{
		Import3dInst = MakeShareable(new FImport3d);
	}
	return Import3dInst;
}

void FImport3d::Import3d(TSharedPtr<F3DPreferences> Type3dPrefs, TSharedPtr<FAssetTypeData> AssetImportData, UMaterialInstanceConstant* MaterialInstance)
{

	//checkf(AssetImportData->MeshList.Num() > 0, TEXT("There are no mesh files to import."));
	if (AssetImportData->MeshList.Num() == 0) return;

	if (AssetImportData->AssetMetaInfo->ActiveLOD == TEXT("high"))
	{
		EAppReturnType::Type ContinueImport = FMessageDialog::Open(EAppMsgType::OkCancel, FText(FText::FromString("You are about to import a high poly mesh. This may cause Unreal to stop responding. Press Ok to continue.")));
		if (ContinueImport == EAppReturnType::Cancel) return;
	}

	const UMegascansSettings* MegascansSettings = GetDefault<UMegascansSettings>();
	FString RootDestination = GetRootDestination(AssetImportData->AssetMetaInfo->ExportPath);
	FString MeshDestination = FPaths::Combine(RootDestination, ResolvePath(Type3dPrefs->DestinationPrefs->MeshDestinationPath, AssetImportData));	
	FString AssetName = RemoveReservedKeywords(NormalizeString(GetUniqueAssetName(MeshDestination, AssetImportData->AssetMetaInfo->Name)));

	MeshDestination = FPaths::Combine(MeshDestination, AssetName);

	if (MaterialInstance == nullptr)
	{

		TSharedPtr<FSurfacePreferences> TypeSurfacePrefs = MakeShareable(new FSurfacePreferences);
		TypeSurfacePrefs->DestinationPrefs = Type3dPrefs->DestinationPrefs;
		TypeSurfacePrefs->MaterialPrefs = Type3dPrefs->MaterialPrefs;
		TypeSurfacePrefs->TexturePrefs = Type3dPrefs->TexturePrefs;
		TypeSurfacePrefs->RenamePrefs = Type3dPrefs->RenamePrefs;
		TSharedPtr<SurfaceImportParams> SImportParams = FImportSurface::Get()->GetSurfaceImportParams(TypeSurfacePrefs, AssetImportData);
		MaterialInstance = FImportSurface::Get()->ImportSurface(TypeSurfacePrefs, AssetImportData, SImportParams);
	}

	

	if (AssetImportData->AssetMetaInfo->Categories.Contains(TEXT("scatter")) || AssetImportData->AssetMetaInfo->Tags.Contains(TEXT("scatter")))
	{
		ImportScatter(AssetImportData, MaterialInstance, MeshDestination, SanitizeName( AssetImportData->MeshList[0]->NameOverride));
	}

	else {
		
		FString FileExtension;
		FString FilePath;
		AssetImportData->MeshList[0]->Path.Split(TEXT("."), &FilePath, &FileExtension);
		FileExtension = FPaths::GetExtension(AssetImportData->MeshList[0]->Path);

		FString OverridenName;
		FString OverridenExtension;
		AssetImportData->MeshList[0]->NameOverride.Split(TEXT("."), &OverridenName, &OverridenExtension);
		

		TSharedPtr<FMeshOps> MeshUtils = FMeshOps::Get();
		//UE_LOG(MSLiveLinkLog, Log, TEXT("Target static mesh name : %s"), *SanitizeName(OverridenName));
		
		FString AssetPath = MeshUtils->ImportMesh(AssetImportData->MeshList[0]->Path, MeshDestination, SanitizeName(OverridenName));
		

		if (!UEditorAssetLibrary::DoesAssetExist(AssetPath)) return;

		UStaticMesh * ImportedAsset = CastChecked<UStaticMesh>(UEditorAssetLibrary::LoadAsset(AssetPath));

		//checkf(ImportedAsset != nullptr, TEXT("Error importing mesh file."));
		if (ImportedAsset == nullptr) return;
		if(MaterialInstance != nullptr)
			ImportedAsset->SetMaterial(0, CastChecked<UMaterialInterface>(MaterialInstance));

		if (MegascansSettings->bEnableLods && AssetImportData->LodList.Num() > 0)
		{			
			TArray<FString> LodPathList = ParseLodList(AssetImportData);
			if (FileExtension == TEXT("abc"))
			{
				
				MeshUtils->ApplyAbcLods(ImportedAsset, LodPathList, MeshDestination);
			}
			else {
				
				MeshUtils->ApplyLods(LodPathList, ImportedAsset);
			}
			
			MeshUtils->RemoveExtraMaterialSlot(ImportedAsset);
		}
		ImportedAsset->MarkPackageDirty();
		ImportedAsset->PostEditChange();
		//AssetUtils::SavePackage(ImportedAsset);

		MeshUtils.Reset();
		
	}

	
	
}


void FImport3d::ImportScatter(TSharedPtr<FAssetTypeData> AssetImportData, UMaterialInstanceConstant * MaterialInstance, const FString & MeshDestination, const FString& AssetName)
{
	TSharedPtr<FMeshOps> MeshUtils = FMeshOps::Get();

	const UMegascansSettings* MegascansSettings = GetDefault<UMegascansSettings>();
	TArray<FString> ExistingAssets = GetAssetsList(MeshDestination);
	MeshUtils->ImportMesh(AssetImportData->MeshList[0]->Path, MeshDestination, "");
	TArray<FString> ImportedAssets;
	TArray<FString> AllAssets = GetAssetsList(MeshDestination);

	TMap<FString, TArray<UStaticMesh*>> ScatterLods;



	for (FString Asset : AllAssets)
	{
		if (!ExistingAssets.Contains(Asset))
		{
			ImportedAssets.Add(Asset);
			UStaticMesh* ImportedMesh = CastChecked<UStaticMesh>(UEditorAssetLibrary::LoadAsset(Asset));
			if(MaterialInstance!=nullptr)
				ImportedMesh->SetMaterial(0, CastChecked<UMaterialInterface>(MaterialInstance));

			ImportedMesh->PostEditChange();
		}
	}

	if (MegascansSettings->bEnableLods && AssetImportData->LodList.Num() > 0) {		

		TArray<FString> LodPathList = ParseLodList(AssetImportData);
		FString LodDestination = FPaths::Combine(MeshDestination, TEXT("Lods"));
		int32 LodCounter = 1;
		for (FString LodPath : LodPathList)
		{
			if (LodCounter > 7) continue;

			MeshUtils->ImportMesh(LodPath, LodDestination);
			TArray<FString> ImportedLods = UEditorAssetLibrary::ListAssets(LodDestination);

			for (FString MeshLod : ImportedLods)
			{
				FString MeshLodName = FPaths::GetBaseFilename(MeshLod);

				TArray<FString> NameTags;
				FString LodNameRefined = TEXT("");

				MeshLodName.ParseIntoArray(NameTags, TEXT("_"));
				for (FString Tag : NameTags)
				{
					if (!Tag.Contains(TEXT("LOD")))
					{
						LodNameRefined.Append(Tag);
					}
				}

				

				for (FString SourceMeshPath : ImportedAssets)
				{
					FString SourceMeshName = FPaths::GetBaseFilename(SourceMeshPath);
					TArray<FString> MeshNameTags;
					FString MeshNameRefined = TEXT("");
					UStaticMesh* ImportedScatter = CastChecked<UStaticMesh>(UEditorAssetLibrary::LoadAsset(SourceMeshPath));

					SourceMeshName.ParseIntoArray(MeshNameTags, TEXT("_"));

					for (FString Tag : MeshNameTags)
					{
						if (!Tag.Contains(TEXT("LOD")))
						{
							MeshNameRefined.Append(Tag);
						}
					}

					if (MeshNameRefined == LodNameRefined)
					{
						
						UEditorStaticMeshLibrary::SetLodFromStaticMesh(ImportedScatter, LodCounter, CastChecked<UStaticMesh>(UEditorAssetLibrary::LoadAsset(MeshLod)), 0, true);

					}
					ImportedScatter->PostEditChange();
					ImportedScatter->MarkPackageDirty();

				}
				

			}
			LodCounter++;
			UEditorAssetLibrary::DeleteDirectory(LodDestination);
		}
	}

	int32 VarCounter = 1;
	FString OverridenName;
	FString OverridenExtension;
	AssetImportData->MeshList[0]->NameOverride.Split(TEXT("."), &OverridenName, &OverridenExtension);

	OverridenName = SanitizeName(OverridenName);
	for (FString Asset : ImportedAssets)
	{
		FString BasePath = FPaths::GetPath(Asset);
		FString RenamedPath = FPaths::Combine(BasePath, (OverridenName + TEXT("_") + FString::FromInt(VarCounter)));

		if(MaterialInstance != nullptr)
			CastChecked<UStaticMesh>(UEditorAssetLibrary::LoadAsset(Asset))->SetMaterial(1, CastChecked<UMaterialInterface>(MaterialInstance));

		UEditorAssetLibrary::RenameAsset(Asset, RenamedPath);

		
		UStaticMesh* ImportedScatter = CastChecked<UStaticMesh>(UEditorAssetLibrary::LoadAsset(RenamedPath));
		MeshUtils->RemoveExtraMaterialSlot(ImportedScatter);

		if (MegascansSettings->bCreateFoliage)
		{
			
			MeshUtils->CreateFoliageAsset(MeshDestination, ImportedScatter, (OverridenName + TEXT("_") + FString::FromInt(VarCounter)));
		}
		ImportedScatter->MarkPackageDirty();
		//AssetUtils::SavePackage(ImportedScatter);
		VarCounter++;
	}

	MeshUtils.Reset();

}