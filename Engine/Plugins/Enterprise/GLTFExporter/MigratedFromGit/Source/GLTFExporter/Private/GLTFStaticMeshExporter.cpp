// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFStaticMeshExporter.h"
#include "GLTFJsonRoot.h"
#include "GLTFContainerBuilder.h"
#include "Engine/StaticMesh.h"

UGLTFStaticMeshExporter::UGLTFStaticMeshExporter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UStaticMesh::StaticClass();
}

bool UGLTFStaticMeshExporter::ExportBinary(UObject* Object, const TCHAR* Type, FArchive& Archive, FFeedbackContext* Warn, int32 FileIndex, uint32 PortFlags)
{
	const UStaticMesh* StaticMesh = CastChecked<UStaticMesh>(Object);

	if (!FillExportOptions())
	{
		// User cancelled the export
		return false;
	}

	FGLTFContainerBuilder Builder;

	FGLTFJsonMeshIndex MeshIndex = Builder.GetOrAddMesh(StaticMesh);

	FGLTFJsonNode Node;
	Node.Mesh = MeshIndex;
	FGLTFJsonNodeIndex NodeIndex = Builder.AddNode(Node);

	FGLTFJsonScene Scene;
	Scene.Nodes.Add(NodeIndex);
	FGLTFJsonSceneIndex SceneIndex = Builder.AddScene(Scene);

	Builder.JsonRoot.DefaultScene = SceneIndex;

	Builder.Serialize(Archive);
	return true;
}
