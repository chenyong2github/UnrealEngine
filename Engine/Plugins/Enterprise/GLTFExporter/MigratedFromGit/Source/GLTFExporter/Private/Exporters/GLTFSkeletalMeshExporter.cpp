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

	FGLTFJsonNode* Node = Builder.AddNode();
	Node->Mesh = MeshIndex;

	if (Builder.ExportOptions->bExportVertexSkinWeights)
	{
		FGLTFJsonSkin* SkinIndex = Builder.GetOrAddSkin(Node, SkeletalMesh);
		if (SkinIndex == nullptr)
		{
			Builder.LogError(FString::Printf(TEXT("Failed to export bones in skeletal mesh %s"), *SkeletalMesh->GetName()));
			return false;
		}

		Node->Skin = SkinIndex;
	}

	FGLTFJsonScene* Scene = Builder.AddScene();
	Scene->Nodes.Add(Node);

	Builder.DefaultScene = Scene;
	return true;
}
