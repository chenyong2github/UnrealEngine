// Copyright Epic Games, Inc. All Rights Reserved.

#include "Exporters/GLTFStaticMeshExporter.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Engine/StaticMesh.h"

UGLTFStaticMeshExporter::UGLTFStaticMeshExporter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UStaticMesh::StaticClass();
}

bool UGLTFStaticMeshExporter::Add(FGLTFContainerBuilder& Builder, const UObject* Object)
{
	const UStaticMesh* StaticMesh = CastChecked<UStaticMesh>(Object);
	FGLTFJsonMeshIndex MeshIndex = Builder.GetOrAddMesh(StaticMesh);

	FGLTFJsonNode Node;
	Node.Mesh = MeshIndex;
	FGLTFJsonNodeIndex NodeIndex = Builder.AddNode(Node);

	FGLTFJsonScene Scene;
	Scene.Nodes.Add(NodeIndex);
	FGLTFJsonSceneIndex SceneIndex = Builder.AddScene(Scene);

	Builder.DefaultScene = SceneIndex;
	return true;
}
