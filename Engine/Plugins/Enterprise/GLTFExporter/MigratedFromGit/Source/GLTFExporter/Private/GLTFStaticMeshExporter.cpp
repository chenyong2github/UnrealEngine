// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFStaticMeshExporter.h"
#include "GLTFJsonRoot.h"
#include "GLTFBufferBuilder.h"
#include "GLTFMeshConverter.h"
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

	FGLTFJsonRoot Root;
	FGLTFBufferBuilder BufferBuilder(Root);
	FGLTFMeshConverter MeshConverter(Root, BufferBuilder);

	FGLTFJsonIndex MeshIndex = MeshConverter.AppendMesh(StaticMesh, 0);

	FGLTFJsonNode Node;
	Node.Mesh = MeshIndex;
	FGLTFJsonIndex NodeIndex = Root.Nodes.Add(Node);

	FGLTFJsonScene Scene;
	Scene.Nodes.Add(NodeIndex);
	FGLTFJsonIndex SceneIndex = Root.Scenes.Add(Scene);

	Root.DefaultScene = SceneIndex;

	BufferBuilder.Close();
	Root.Serialize(&Archive, true);
	return true;
}
