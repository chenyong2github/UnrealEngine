// Copyright Epic Games, Inc. All Rights Reserved.
#include "AssetImporters/ProgressiveImportSurfaces.h"
#include "Utilities/MiscUtils.h"
#include "Utilities/MaterialUtils.h"

#include "Misc/FileHelper.h"
#include "Misc/ScopedSlowTask.h"
#include "JsonObjectConverter.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/Paths.h"

#include "UObject/SoftObjectPath.h"
#include "Engine/StreamableManager.h"
#include "Engine/AssetManager.h"

#include "EditorViewportClient.h"
#include "UnrealClient.h"
#include "Engine/StaticMesh.h"

#include "MaterialEditingLibrary.h"

#include "Async/AsyncWork.h"
#include "Async/Async.h"

#include "Kismet/GameplayStatics.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"




TSharedPtr<FImportProgressiveSurfaces> FImportProgressiveSurfaces::ImportProgressiveSurfacesInst;



TSharedPtr<FImportProgressiveSurfaces> FImportProgressiveSurfaces::Get()
{
	if (!ImportProgressiveSurfacesInst.IsValid())
	{
		ImportProgressiveSurfacesInst = MakeShareable(new FImportProgressiveSurfaces);
	}
	return ImportProgressiveSurfacesInst;
}


void FImportProgressiveSurfaces::ImportAsset(TSharedPtr<FJsonObject> AssetImportJson)
{

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::GetModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FStreamableManager& Streamable = UAssetManager::GetStreamableManager();

	TSharedPtr<FUAssetData> ImportData = JsonUtils::ParseUassetJson(AssetImportJson);

	/*FString UassetMetaString;
	FFileHelper::LoadFileToString(UassetMetaString, *ImportData->ImportJsonPath);*/

	FUAssetMeta AssetMetaData = AssetUtils::GetAssetMetaData(ImportData->ImportJsonPath);
	//FJsonObjectConverter::JsonObjectStringToUStruct(UassetMetaString, &AssetMetaData);

	FString DestinationPath = AssetMetaData.assetRootPath;
	FString DestinationFolder = FPaths::Combine(FPaths::ProjectContentDir(), DestinationPath.Replace(TEXT("/Game/"), TEXT("")));

	CopyUassetFiles(ImportData->FilePaths, DestinationFolder);
	

	if (!PreviewDetails.Contains(ImportData->AssetId))
	{
		TSharedPtr< FProgressiveSurfaces> ProgressiveDetails = MakeShareable(new FProgressiveSurfaces);
		PreviewDetails.Add(ImportData->AssetId, ProgressiveDetails);
	}


	if (ImportData->ProgressiveStage == 1)
	{
		FString MInstancePath = AssetMetaData.materialInstances[0].instancePath;
		FAssetData MInstanceData = AssetRegistry.GetAssetByObjectPath(FName(*MInstancePath));

		FSoftObjectPath ItemToStream = MInstanceData.ToSoftObjectPath();
		Streamable.RequestAsyncLoad(ItemToStream, FStreamableDelegate::CreateRaw(this, &FImportProgressiveSurfaces::HandlePreviewInstanceLoad, MInstanceData, ImportData->AssetId));
		

	}
	else if (ImportData->ProgressiveStage == 2)
	{
		FString AlbedoPath = TEXT("");
		FString TextureType = TEXT("albedo");

		for (FTexturesList TextureMeta : AssetMetaData.textureSets)
		{
			if (TextureMeta.type == TEXT("albedo"))
			{
				AlbedoPath = TextureMeta.path;
			}
		}		

		FAssetData AlbedoData = AssetRegistry.GetAssetByObjectPath(FName(*AlbedoPath));
		FSoftObjectPath ItemToStream = AlbedoData.ToSoftObjectPath();
		Streamable.RequestAsyncLoad(ItemToStream, FStreamableDelegate::CreateRaw(this, &FImportProgressiveSurfaces::HandlePreviewTextureLoad, AlbedoData, ImportData->AssetId, TextureType));


	}

	else if (ImportData->ProgressiveStage == 3)
	{

		FString NormalPath = TEXT("");
		FString TextureType = TEXT("normal");

		for (FTexturesList TextureMeta : AssetMetaData.textureSets)
		{
			if (TextureMeta.type == TEXT("normal"))
			{
				NormalPath = TextureMeta.path;
			}
		}		

		FAssetData NormalData = AssetRegistry.GetAssetByObjectPath(FName(*NormalPath));
		FSoftObjectPath ItemToStream = NormalData.ToSoftObjectPath();
		Streamable.RequestAsyncLoad(ItemToStream, FStreamableDelegate::CreateRaw(this, &FImportProgressiveSurfaces::HandlePreviewTextureLoad, NormalData, ImportData->AssetId, TextureType));

	}

	else if (ImportData->ProgressiveStage == 4)
	{	
		FString MInstanceHighPath = AssetMetaData.materialInstances[0].instancePath;
		FAssetData MInstanceHighData = AssetRegistry.GetAssetByObjectPath(FName(*MInstanceHighPath));

		FSoftObjectPath ItemToStream = MInstanceHighData.ToSoftObjectPath();
		Streamable.RequestAsyncLoad(ItemToStream, FStreamableDelegate::CreateRaw(this, &FImportProgressiveSurfaces::HandleHighInstanceLoad, MInstanceHighData, ImportData->AssetId, AssetMetaData));

	}


}

void FImportProgressiveSurfaces::HandlePreviewTextureLoad(FAssetData TextureData, FString AssetID, FString Type)
{
	UTexture* PreviewTexture = Cast<UTexture>(TextureData.GetAsset());
	UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(PreviewDetails[AssetID]->PreviewInstance, FName(*Type), PreviewTexture);
	AssetUtils::SavePackage(PreviewDetails[AssetID]->PreviewInstance);
}



void FImportProgressiveSurfaces::HandlePreviewInstanceLoad(FAssetData PreviewInstanceData, FString AssetID)
{

	PreviewDetails[AssetID]->PreviewInstance = Cast<UMaterialInstanceConstant>(PreviewInstanceData.GetAsset());
	SpawnMaterialPreviewActor(AssetID);


}

void FImportProgressiveSurfaces::SpawnMaterialPreviewActor(FString AssetID)
{
	IAssetRegistry& AssetRegistry = FModuleManager::GetModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	FString SphereMeshPath = TEXT("/Engine/BasicShapes/Sphere.Sphere");

	FAssetData PreviewerMeshData = AssetRegistry.GetAssetByObjectPath(FName(*SphereMeshPath));

	FViewport* ActiveViewport = GEditor->GetActiveViewport();
	FEditorViewportClient* EditorViewClient = (FEditorViewportClient*)ActiveViewport->GetClient();

	FVector ViewPosition = EditorViewClient->GetViewLocation();
	FVector ViewDirection = EditorViewClient->GetViewRotation().Vector();
	FRotator InitialRotation(0.0f, 0.0f, 0.0f);

	FVector SpawnLocation = ViewPosition + (ViewDirection * 300.0f);

	FVector Location(0.0f, 0.0f, 0.0f);
	FRotator Rotation(0.0f, 0.0f, 0.0f);
	UWorld* CurrentWorld = GEngine->GetWorldContexts()[0].World();
	UStaticMesh* SourceMesh = Cast<UStaticMesh>(PreviewerMeshData.GetAsset());
	FTransform InitialTransform(SpawnLocation);

	AStaticMeshActor* SMActor = Cast<AStaticMeshActor>(CurrentWorld->SpawnActor(AStaticMeshActor::StaticClass(), &InitialTransform));
	SMActor->GetStaticMeshComponent()->SetStaticMesh(SourceMesh);
	SMActor->GetStaticMeshComponent()->SetMaterial(0, CastChecked<UMaterialInterface>(PreviewDetails[AssetID]->PreviewInstance));

	//SMActor->Rename(TEXT("MyStaticMeshInTheWorld"));
	//SMActor->SetActorLabel("StaticMeshActor");

	GEditor->EditorUpdateComponents();
	CurrentWorld->UpdateWorldComponents(true, false);
	SMActor->RerunConstructionScripts();
	if (PreviewDetails.Contains(AssetID))
	{
		PreviewDetails[AssetID]->ActorInLevel = SMActor;
	}
}

void FImportProgressiveSurfaces::HandleHighInstanceLoad(FAssetData HighInstanceData, FString AssetID, FUAssetMeta AssetMetaData)
{
	if (!PreviewDetails.Contains(AssetID)) return;
	if (PreviewDetails[AssetID]->ActorInLevel == nullptr) return;

	if (FMaterialUtils::ShouldOverrideMaterial(AssetMetaData.assetType))
	{
		AssetUtils::DeleteAsset(AssetMetaData.materialInstances[0].instancePath);
		UMaterialInstanceConstant* OverridenInstance = FMaterialUtils::CreateMaterialOverride(AssetMetaData);
		FMaterialUtils::ApplyMaterialInstance(AssetMetaData, OverridenInstance);

	}

	AssetUtils::ManageImportSettings(AssetMetaData);

	//UMaterialInstanceConstant* HighInstance = Cast<UMaterialInstanceConstant>(HighInstanceData.GetAsset());
	PreviewDetails[AssetID]->ActorInLevel->GetStaticMeshComponent()->SetMaterial(0, CastChecked<UMaterialInterface>(HighInstanceData.GetAsset()));
	PreviewDetails.Remove(AssetID);
}
