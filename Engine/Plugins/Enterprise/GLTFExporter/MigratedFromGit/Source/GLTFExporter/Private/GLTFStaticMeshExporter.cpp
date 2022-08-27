// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFStaticMeshExporter.h"
#include "GLTFJsonRoot.h"
#include "GLTFContainer.h"
#include "Engine/StaticMesh.h"

UGLTFStaticMeshExporter::UGLTFStaticMeshExporter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UStaticMesh::StaticClass();
}

bool UGLTFStaticMeshExporter::ExportBinary(UObject* Object, const TCHAR* Type, FArchive& Archive, FFeedbackContext* Warn, int32 FileIndex, uint32 PortFlags)
{
	UStaticMesh* StaticMesh = CastChecked<UStaticMesh>(Object);

	if (!FillExportOptions())
	{
		// User cancelled the export
		return false;
	}

	FGLTFContainer Container;

	FGLTFJsonMeshIndex MeshIndex = Container.AppendMesh(StaticMesh, 0);

	FGLTFJsonNode Node;
	Node.Mesh = MeshIndex;
	FGLTFJsonNodeIndex NodeIndex = Container.JsonRoot.Nodes.Add(Node);

	FGLTFJsonScene Scene;
	Scene.Nodes.Add(NodeIndex);
	FGLTFJsonSceneIndex SceneIndex = Container.JsonRoot.Scenes.Add(Scene);

	Container.JsonRoot.DefaultScene = SceneIndex;

	Container.Serialize(Archive);
	return true;
}
