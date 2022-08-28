// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFStaticMeshExporter.h"
#include "GLTFJsonRoot.h"
#include "GLTFBuilder.h"
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

	FGLTFBuilder Builder;

	FGLTFJsonMeshIndex MeshIndex = Builder.AppendMesh(StaticMesh, 0);

	FGLTFJsonNode Node;
	Node.Mesh = MeshIndex;
	FGLTFJsonNodeIndex NodeIndex = Builder.JsonRoot.Nodes.Add(Node);

	FGLTFJsonScene Scene;
	Scene.Nodes.Add(NodeIndex);
	FGLTFJsonSceneIndex SceneIndex = Builder.JsonRoot.Scenes.Add(Scene);

	Builder.JsonRoot.DefaultScene = SceneIndex;

	Builder.Serialize(Archive);
	return true;
}
