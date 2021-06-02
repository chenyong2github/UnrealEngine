// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorModelingObjectsCreationAPI.h"
#include "InteractiveToolsContext.h"
#include "InteractiveToolManager.h"
#include "ContextObjectStore.h"

#include "AssetUtils/CreateStaticMeshUtil.h"
#include "AssetUtils/CreateTexture2DUtil.h"

#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"

using namespace UE::Geometry;


UEditorModelingObjectsCreationAPI* UEditorModelingObjectsCreationAPI::Register(UInteractiveToolsContext* ToolsContext)
{
	if (ensure(ToolsContext))
	{
		UEditorModelingObjectsCreationAPI* CreationAPI = ToolsContext->ContextObjectStore->FindContext<UEditorModelingObjectsCreationAPI>();
		if (CreationAPI)
		{
			return CreationAPI;
		}
		CreationAPI = NewObject<UEditorModelingObjectsCreationAPI>(ToolsContext);
		ToolsContext->ContextObjectStore->AddContextObject(CreationAPI);
		if (ensure(CreationAPI))
		{
			return CreationAPI;
		}
	}
	return nullptr;
}

UEditorModelingObjectsCreationAPI* UEditorModelingObjectsCreationAPI::Find(UInteractiveToolsContext* ToolsContext)
{
	if (ensure(ToolsContext))
	{
		UEditorModelingObjectsCreationAPI* CreationAPI = ToolsContext->ContextObjectStore->FindContext<UEditorModelingObjectsCreationAPI>();
		if (CreationAPI != nullptr)
		{
			return CreationAPI;
		}
	}
	return nullptr;
}

bool UEditorModelingObjectsCreationAPI::Deregister(UInteractiveToolsContext* ToolsContext)
{
	if (ensure(ToolsContext))
	{
		UEditorModelingObjectsCreationAPI* CreationAPI = ToolsContext->ContextObjectStore->FindContext<UEditorModelingObjectsCreationAPI>();
		if (CreationAPI != nullptr)
		{
			ToolsContext->ContextObjectStore->RemoveContextObject(CreationAPI);
		}
		return true;
	}
	return false;
}


FCreateMeshObjectResult UEditorModelingObjectsCreationAPI::CreateMeshObject(const FCreateMeshObjectParams& CreateMeshParams)
{
	// TODO: implement this path
	check(false);
	return FCreateMeshObjectResult{ ECreateModelingObjectResult::Failed_InvalidMesh };
}


FCreateTextureObjectResult UEditorModelingObjectsCreationAPI::CreateTextureObject(const FCreateTextureObjectParams& CreateTexParams)
{
	FCreateTextureObjectParams LocalParams = CreateTexParams;
	return CreateTextureObject(MoveTemp(LocalParams));

}


FCreateMeshObjectResult UEditorModelingObjectsCreationAPI::CreateMeshObject(FCreateMeshObjectParams&& CreateMeshParams)
{
	if (!ensure(CreateMeshParams.TargetWorld)) { return FCreateMeshObjectResult{ ECreateModelingObjectResult::Failed_InvalidWorld }; }

	UE::AssetUtils::FStaticMeshAssetOptions AssetOptions;

	if (GetNewAssetPathNameCallback.IsBound())
	{
		AssetOptions.NewAssetPath = GetNewAssetPathNameCallback.Execute(CreateMeshParams.BaseName, CreateMeshParams.TargetWorld, FString());
		if (AssetOptions.NewAssetPath.Len() == 0)
		{
			return FCreateMeshObjectResult{ ECreateModelingObjectResult::Cancelled };
		}
	}
	else
	{
		AssetOptions.NewAssetPath = "/Game/" + CreateMeshParams.BaseName;
	}

	AssetOptions.NumSourceModels = 1;
	AssetOptions.NumMaterialSlots = CreateMeshParams.Materials.Num();
	AssetOptions.AssetMaterials = (CreateMeshParams.AssetMaterials.Num() == AssetOptions.NumMaterialSlots) ?
		CreateMeshParams.AssetMaterials : CreateMeshParams.Materials;

	AssetOptions.bEnableRecomputeNormals = CreateMeshParams.bEnableRecomputeNormals;
	AssetOptions.bEnableRecomputeTangents = CreateMeshParams.bEnableRecomputeTangents;
	AssetOptions.bGenerateNaniteEnabledMesh = CreateMeshParams.bEnableNanite;
	AssetOptions.NaniteProxyTrianglePercent = CreateMeshParams.NaniteProxyTrianglePercent;

	AssetOptions.bCreatePhysicsBody = CreateMeshParams.bEnableCollision;
	AssetOptions.CollisionType = CreateMeshParams.CollisionMode;

	if (CreateMeshParams.MeshType == ECreateMeshObjectSourceMeshType::DynamicMesh)
	{
		FDynamicMesh3* DynamicMesh = &CreateMeshParams.DynamicMesh.GetValue();
		AssetOptions.SourceMeshes.DynamicMeshes.Add(DynamicMesh);
	}
	else if (CreateMeshParams.MeshType == ECreateMeshObjectSourceMeshType::MeshDescription)
	{
		FMeshDescription* MeshDescription = &CreateMeshParams.MeshDescription.GetValue();
		AssetOptions.SourceMeshes.MoveMeshDescriptions.Add(MeshDescription);
	}
	else
	{
		return FCreateMeshObjectResult{ ECreateModelingObjectResult::Failed_InvalidMesh };
	}

	UE::AssetUtils::FStaticMeshResults ResultData;
	UE::AssetUtils::ECreateStaticMeshResult AssetResult = UE::AssetUtils::CreateStaticMeshAsset(AssetOptions, ResultData);

	if (AssetResult != UE::AssetUtils::ECreateStaticMeshResult::Ok)
	{
		return FCreateMeshObjectResult{ ECreateModelingObjectResult::Failed_AssetCreationFailed };
	}

	UStaticMesh* NewStaticMesh = ResultData.StaticMesh;

	// create new StaticMeshActor
	FRotator Rotation(0.0f, 0.0f, 0.0f);
	FActorSpawnParameters SpawnInfo;
	// @todo nothing here is specific to AStaticMeshActor...could we pass in a CDO and clone it instead of using SpawnActor?
	AStaticMeshActor* StaticMeshActor = CreateMeshParams.TargetWorld->SpawnActor<AStaticMeshActor>(FVector::ZeroVector, Rotation, SpawnInfo);
	StaticMeshActor->SetActorLabel(*CreateMeshParams.BaseName);
	UStaticMeshComponent* StaticMeshComponent = StaticMeshActor->GetStaticMeshComponent();

	// set the mesh

	// this disconnects the component from various events
	StaticMeshComponent->UnregisterComponent();
	// Configure flags of the component. Is this necessary?
	StaticMeshComponent->SetMobility(EComponentMobility::Movable);
	StaticMeshComponent->bSelectable = true;
	// replace the UStaticMesh in the component
	StaticMeshComponent->SetStaticMesh(NewStaticMesh);

	// set materials
	for (int32 k = 0; k < CreateMeshParams.Materials.Num(); ++k)
	{
		StaticMeshComponent->SetMaterial(k, CreateMeshParams.Materials[k]);
	}

	// re-connect the component (?)
	StaticMeshComponent->RegisterComponent();

	NewStaticMesh->PostEditChange();

	StaticMeshComponent->RecreatePhysicsState();

	// update transform
	StaticMeshActor->SetActorTransform(CreateMeshParams.Transform);

	// emit result
	FCreateMeshObjectResult ResultOut;
	ResultOut.ResultCode = ECreateModelingObjectResult::Ok;
	ResultOut.NewActor = StaticMeshActor;
	ResultOut.NewComponent = StaticMeshComponent;
	ResultOut.NewAsset = NewStaticMesh;

	OnModelingMeshCreated.Broadcast(ResultOut);

	return ResultOut;
}




FCreateTextureObjectResult UEditorModelingObjectsCreationAPI::CreateTextureObject(FCreateTextureObjectParams&& CreateTexParams)
{
	FString RelativeToObjectFolder;
	if (CreateTexParams.StoreRelativeToObject != nullptr)
	{
		// find path to asset
		UPackage* AssetOuterPackage = CastChecked<UPackage>(CreateTexParams.StoreRelativeToObject->GetOuter());
		if (ensure(AssetOuterPackage))
		{
			FString AssetPackageName = AssetOuterPackage->GetName();
			RelativeToObjectFolder = FPackageName::GetLongPackagePath(AssetPackageName);
		}
	}
	else
	{
		if (!ensure(CreateTexParams.TargetWorld)) { return FCreateTextureObjectResult{ ECreateModelingObjectResult::Failed_InvalidWorld }; }
	}

	UE::AssetUtils::FTexture2DAssetOptions AssetOptions;

	if (GetNewAssetPathNameCallback.IsBound())
	{
		AssetOptions.NewAssetPath = GetNewAssetPathNameCallback.Execute(CreateTexParams.BaseName, CreateTexParams.TargetWorld, RelativeToObjectFolder);
		if (AssetOptions.NewAssetPath.Len() == 0)
		{
			return FCreateTextureObjectResult{ ECreateModelingObjectResult::Cancelled };
		}
	}
	else
	{
		FString UseBaseFolder = (RelativeToObjectFolder.Len() > 0) ? RelativeToObjectFolder : TEXT("/Game");
		AssetOptions.NewAssetPath = FPaths::Combine(UseBaseFolder, CreateTexParams.BaseName);
	}

	// currently we cannot create a new texture without an existing generated texture to store
	if (!ensure(CreateTexParams.GeneratedTransientTexture))
	{
		return FCreateTextureObjectResult{ ECreateModelingObjectResult::Failed_InvalidTexture };
	}

	UE::AssetUtils::FTexture2DAssetResults ResultData;
	UE::AssetUtils::ECreateTexture2DResult AssetResult = UE::AssetUtils::SaveGeneratedTexture2DAsset(
		CreateTexParams.GeneratedTransientTexture, AssetOptions, ResultData);

	if (AssetResult != UE::AssetUtils::ECreateTexture2DResult::Ok)
	{
		return FCreateTextureObjectResult{ ECreateModelingObjectResult::Failed_AssetCreationFailed };
	}

	// emit result
	FCreateTextureObjectResult ResultOut;
	ResultOut.ResultCode = ECreateModelingObjectResult::Ok;
	ResultOut.NewAsset = ResultData.Texture;

	OnModelingTextureCreated.Broadcast(ResultOut);

	return ResultOut;

}