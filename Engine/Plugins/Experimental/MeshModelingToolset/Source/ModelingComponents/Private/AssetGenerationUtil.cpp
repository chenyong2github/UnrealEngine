// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetGenerationUtil.h"
#include "InteractiveTool.h"
#include "InteractiveToolManager.h"
#include "Materials/Material.h"
#include "StaticMeshAttributes.h"
#include "DynamicMeshToMeshDescription.h"


// single material case
AActor* AssetGenerationUtil::GenerateStaticMeshActor(
	IToolsContextAssetAPI* AssetAPI,
	UWorld* TargetWorld,
	const FDynamicMesh3* Mesh,
	const FTransform3d& Transform,
	FString ObjectName,
	UMaterialInterface* Material
)
{
	return GenerateStaticMeshActor(AssetAPI, TargetWorld, Mesh, Transform, ObjectName, TArrayView<UMaterialInterface*>(&Material, Material != nullptr ? 1 : 0));
}

// N-material case
AActor* AssetGenerationUtil::GenerateStaticMeshActor(
	IToolsContextAssetAPI* AssetAPI,
	UWorld* TargetWorld,
	const FDynamicMesh3* Mesh,
	const FTransform3d& Transform,
	FString ObjectName,
	const TArrayView<UMaterialInterface*>& Materials
)
{
	FGeneratedStaticMeshAssetConfig AssetConfig;
	for (UMaterialInterface* Material : Materials)
	{
		AssetConfig.Materials.Add(Material);
	}

	AssetConfig.MeshDescription = MakeUnique<FMeshDescription>();
	FStaticMeshAttributes Attributes(*AssetConfig.MeshDescription);
	Attributes.Register();

	FDynamicMeshToMeshDescription Converter;
	Converter.Convert(Mesh, *AssetConfig.MeshDescription);

	return AssetAPI->GenerateStaticMeshActor(TargetWorld, (FTransform)Transform, ObjectName, MoveTemp(AssetConfig));
}

