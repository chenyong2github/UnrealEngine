// Copyright Epic Games, Inc. All Rights Reserved.

#include "Exporters/GLTFSkeletalMeshExporter.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Engine/SkeletalMesh.h"

UGLTFSkeletalMeshExporter::UGLTFSkeletalMeshExporter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = USkeletalMesh::StaticClass();
}

bool UGLTFSkeletalMeshExporter::AddObject(FGLTFContainerBuilder& Builder, const UObject* Object)
{
	const USkeletalMesh* SkeletalMesh = CastChecked<USkeletalMesh>(Object);

	FGLTFJsonMesh* MeshIndex = Builder.GetOrAddMesh(SkeletalMesh);
	if (MeshIndex == nullptr)
	{
		Builder.LogError(FString::Printf(TEXT("Failed to export skeletal mesh %s"), *SkeletalMesh->GetName()));
		return false;
	}

	FGLTFJsonNode Node;
	Node.Mesh = MeshIndex;
	FGLTFJsonNode* NodeIndex = Builder.AddNode(Node);

	if (Builder.ExportOptions->bExportVertexSkinWeights)
	{
		FGLTFJsonSkin* SkinIndex = Builder.GetOrAddSkin(NodeIndex, SkeletalMesh);
		if (SkinIndex == nullptr)
		{
			Builder.LogError(FString::Printf(TEXT("Failed to export bones in skeletal mesh %s"), *SkeletalMesh->GetName()));
			return false;
		}

		Builder.GetNode(NodeIndex).Skin = SkinIndex;
	}

	FGLTFJsonScene Scene;
	Scene.Nodes.Add(NodeIndex);
	FGLTFJsonScene* SceneIndex = Builder.AddScene(Scene);

	Builder.DefaultScene = SceneIndex;
	return true;
}
