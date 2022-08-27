// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFStaticMeshExporter.h"
#include "GLTFJsonRoot.h"
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

	FGLTFJsonBuffer Buffer;
	FGLTFJsonIndex BufferIndex = Root.Buffers.Add(Buffer);

	FGLTFJsonBufferView BufferView;
	BufferView.Buffer = BufferIndex;
	FGLTFJsonIndex BufferViewIndex = Root.BufferViews.Add(BufferView);

	FGLTFJsonAccessor Accessor;
	Accessor.BufferView = BufferViewIndex;
	FGLTFJsonIndex AccessorIndex = Root.Accessors.Add(Accessor);

	FGLTFJsonMesh Mesh;
	FGLTFJsonPrimitive Primitive;
	Primitive.Attributes.Position = AccessorIndex;
	Mesh.Primitives.Add(Primitive);
	FGLTFJsonIndex MeshIndex = Root.Meshes.Add(Mesh);

	FGLTFJsonNode Node;
	Node.Mesh = MeshIndex;
	FGLTFJsonIndex NodeIndex = Root.Nodes.Add(Node);

	FGLTFJsonScene Scene;
	Scene.Nodes.Add(NodeIndex);
	FGLTFJsonIndex SceneIndex = Root.Scenes.Add(Scene);

	Root.DefaultScene = SceneIndex;

	Root.Serialize(&Archive, true);
	return true;
}
