// Copyright Epic Games, Inc. All Rights Reserved.

#include "Exporters/GLTFStaticMeshExporter.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Engine/StaticMesh.h"

UGLTFStaticMeshExporter::UGLTFStaticMeshExporter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UStaticMesh::StaticClass();
}

bool UGLTFStaticMeshExporter::AddObject(FGLTFContainerBuilder& Builder, const UObject* Object)
{
	const UStaticMesh* StaticMesh = CastChecked<UStaticMesh>(Object);

	FGLTFJsonMesh* MeshIndex = Builder.GetOrAddMesh(StaticMesh);
	if (MeshIndex == nullptr)
	{
		Builder.LogError(FString::Printf(TEXT("Failed to export static mesh %s"), *StaticMesh->GetName()));
		return false;
	}

	FGLTFJsonNode* Node = Builder.AddNode();
	Node->Mesh = MeshIndex;

	FGLTFJsonScene* Scene = Builder.AddScene();
	Scene->Nodes.Add(Node);

	Builder.DefaultScene = Scene;
	return true;
}
