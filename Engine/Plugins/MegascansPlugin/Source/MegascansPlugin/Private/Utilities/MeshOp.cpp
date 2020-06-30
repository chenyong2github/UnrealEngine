#include "Utilities/MeshOp.h"
#include "UnrealEd/Classes/Factories/FbxImportUI.h"
#include "UnrealEd/Classes/Factories/FbxStaticMeshImportData.h"
#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "FbxMeshUtils.h"
#include "AbcImportSettings.h"
#include "Utilities/MiscUtils.h"
#include "Runtime/Core/Public/Misc/Paths.h"

#include "EditorAssetLibrary.h"
#include "EditorStaticMeshLibrary.h"
#include "Editor/FoliageEdit/Private/FoliageTypeFactory.h"
#include "Runtime/Foliage/Public/InstancedFoliageActor.h"

#include "Runtime/Foliage/Public/FoliageType.h"
#include "Runtime/Foliage/Public/FoliageType_InstancedStaticMesh.h"
#include "Runtime/AssetRegistry/Public/AssetRegistryModule.h"

#include "Editor.h"



TSharedPtr<FMeshOps> FMeshOps::MeshOpsInst;



TSharedPtr<FMeshOps> FMeshOps::Get()
{
	if (!MeshOpsInst.IsValid())
	{
		MeshOpsInst = MakeShareable(new FMeshOps);
	}
	return MeshOpsInst;
}

FString FMeshOps::ImportMesh(const FString& MeshPath, const FString& Destination, const FString& AssetName)
{
	
	FString FileExtension;
	FString FilePath;
	FString AssetPath;

	UFbxImportUI* ImportOptions;
	MeshPath.Split(TEXT("."), &FilePath, &FileExtension);

	FileExtension = FPaths::GetExtension(MeshPath);
	if (FileExtension == TEXT("obj") || FileExtension == TEXT("fbx"))
	{
		ImportOptions = GetFbxOptions();
		if (FileExtension == TEXT("obj"))
		{
			ImportOptions->StaticMeshImportData->ImportRotation = FRotator(0, 0, 90);
		}
		

		AssetPath = ImportFile(ImportOptions, Destination, AssetName, MeshPath);
	}
	else if (FileExtension == TEXT("abc"))
	{
		UAbcImportSettings* AbcImportOptions = GetAbcSettings();
		AssetPath = ImportFile(AbcImportOptions, Destination, AssetName, MeshPath);

	}
	return AssetPath;
}

void FMeshOps::ApplyLods(const TArray<FString>& LodList, UStaticMesh* SourceMesh)
{
	int32 LodCounter = 1;
	for (FString LodPath : LodList)
	{
		if (LodCounter > 7) continue;
		FbxMeshUtils::ImportStaticMeshLOD(SourceMesh, LodPath, LodCounter);
		
		LodCounter++;
	}

	
}

void FMeshOps::ApplyAbcLods(UStaticMesh* SourceMesh, const TArray<FString>& LodPathList, const FString& AssetDestination)
{
	FString LodDestination = FPaths::Combine(AssetDestination, TEXT("Lods"));
	int32 LodCounter = 1;

	for (FString LodPath : LodPathList)
	{
		if (LodCounter > 7) continue;
		FString ImportedLodPath = ImportMesh(LodPath, LodDestination, "");
		UStaticMesh* ImportedLod = CastChecked<UStaticMesh>(LoadAsset(ImportedLodPath));
		UEditorStaticMeshLibrary::SetLodFromStaticMesh(SourceMesh, LodCounter, ImportedLod, 0, true);
		LodCounter++;
	}
	UEditorAssetLibrary::DeleteDirectory(LodDestination);
}

void FMeshOps::CreateFoliageAsset(const FString& FoliagePath, UStaticMesh* SourceAsset, const FString& FoliageAssetName)
{
	
	if (SourceAsset == nullptr) return;
	FString FoliageTypePath = FPaths::Combine(FoliagePath, TEXT("Foliage/"));	

	//IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();	

	FString PackageName = FoliageTypePath;
	PackageName += FoliageAssetName;
	UPackage* Package = CreatePackage(NULL, *PackageName);
	UFoliageType_InstancedStaticMesh* FoliageAsset = NewObject<UFoliageType_InstancedStaticMesh>(Package, *FoliageAssetName, RF_Public | RF_Standalone | RF_MarkAsRootSet);
	FoliageAsset->SetStaticMesh(SourceAsset);
	FAssetRegistryModule::AssetCreated(FoliageAsset);
	FoliageAsset->PostEditChange();
	Package->MarkPackageDirty();

	//AssetUtils::SavePackage(FoliageAsset);

	auto* CurrentWorld = GEditor->GetEditorWorldContext().World();
	AInstancedFoliageActor* IFA = AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel(CurrentWorld, true);
	IFA->AddFoliageType(FoliageAsset);

}



TArray<FString> FMeshOps::ImportLodsAsStaticMesh(const TArray<FString> & LodList, const FString & AssetDestination)
{

	return TArray<FString>();
}


UFbxImportUI* FMeshOps::GetFbxOptions()
{
	UFbxImportUI* ImportOptions = NewObject<UFbxImportUI>();
	ImportOptions->bImportMesh = true;
	ImportOptions->bImportAnimations = false;
	ImportOptions->bImportMaterials = false;
	ImportOptions->bImportAsSkeletal = false;
	ImportOptions->StaticMeshImportData->bCombineMeshes = false;
	ImportOptions->StaticMeshImportData->bGenerateLightmapUVs = false;
	ImportOptions->StaticMeshImportData->bAutoGenerateCollision = false;
	ImportOptions->StaticMeshImportData->VertexColorImportOption = EVertexColorImportOption::Replace;
	

	return ImportOptions;
}

void FMeshOps::RemoveExtraMaterialSlot(UStaticMesh* SourceMesh)
{

	for (int i = 1; i < SourceMesh->GetNumLODs(); i++)
	{
		FMeshSectionInfo MeshSectionInfo = SourceMesh->SectionInfoMap.Get(i, 0);
		//FMeshSectionInfo MeshSectionInfo = SourceMesh->GetSectionInfoMap().Get(i, 0);
		MeshSectionInfo.MaterialIndex = 0;
		SourceMesh->SectionInfoMap.Set(i, 0, MeshSectionInfo);
		//SourceMesh->GetSectionInfoMap().Set(i, 0, MeshSectionInfo);
		
	}

	int NumOfMaterials = SourceMesh->StaticMaterials.Num();
	for (int i = NumOfMaterials; i > 1; i--)
	{
		SourceMesh->StaticMaterials.RemoveAt(i - 1);
	}

	SourceMesh->Modify();
	SourceMesh->PostEditChange();
	SourceMesh->MarkPackageDirty();

}


template<class T>
FString FMeshOps::ImportFile(T * ImportOptions, const FString & Destination, const FString & AssetName, const FString & Source)
{

	

	FString ImportedMeshPath = TEXT("");
	TArray< UAssetImportTask*> ImportTasks;
	UAssetImportTask* MeshImportTask = NewObject<UAssetImportTask>();
	MeshImportTask->bAutomated = true;
	MeshImportTask->bSave = false;
	MeshImportTask->Filename = Source;

	
	

	MeshImportTask->DestinationName = AssetName;
	MeshImportTask->DestinationPath = Destination;
	MeshImportTask->bReplaceExisting = true;
	MeshImportTask->Options = ImportOptions;

	ImportTasks.Add(MeshImportTask);

	IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();

	AssetTools.ImportAssetTasks(ImportTasks);

	for (UAssetImportTask* ImpTask : ImportTasks)
	{
		if(ImpTask->ImportedObjectPaths.Num()>0)
			ImportedMeshPath = ImpTask->ImportedObjectPaths[0];
	}
	return ImportedMeshPath;
}

UAbcImportSettings* FMeshOps::GetAbcSettings()
{
	UAbcImportSettings* AlembicOptions = NewObject<UAbcImportSettings>();
	AlembicOptions->ImportType = EAlembicImportType::StaticMesh;
	AlembicOptions->StaticMeshSettings.bMergeMeshes = false;
	AlembicOptions->MaterialSettings.bCreateMaterials = false;
	AlembicOptions->SamplingSettings.FrameStart = 0;
	AlembicOptions->SamplingSettings.FrameEnd = 1;
	AlembicOptions->ConversionSettings.Rotation = FVector(00, 00, 00);
	return AlembicOptions;
}





