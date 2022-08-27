// Copyright Epic Games, Inc. All Rights Reserved.

#include "Exporters/GLTFMaterialExporter.h"
#include "Exporters/GLTFExporterUtility.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Materials/MaterialInterface.h"

UGLTFMaterialExporter::UGLTFMaterialExporter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UMaterialInterface::StaticClass();
}

bool UGLTFMaterialExporter::AddObject(FGLTFContainerBuilder& Builder, const UObject* Object)
{
	const UMaterialInterface* Material = CastChecked<UMaterialInterface>(Object);
	const FGLTFJsonMaterialIndex MaterialIndex = Builder.GetOrAddMaterial(Material);

	if (MaterialIndex == INDEX_NONE)
	{
		return false;
	}

	const UStaticMesh* PreviewMesh = FGLTFExporterUtility::GetPreviewMesh(Material);
	if (PreviewMesh != nullptr)
	{
		const FGLTFJsonMeshIndex MeshIndex = Builder.GetOrAddMesh(PreviewMesh, 0, nullptr, { Material }, "PreviewMesh");

		FGLTFJsonNode Node;
		Node.Mesh = MeshIndex;
		const FGLTFJsonNodeIndex NodeIndex = Builder.AddNode(Node);

		FGLTFJsonScene Scene;
		Scene.Nodes.Add(NodeIndex);
		const FGLTFJsonSceneIndex SceneIndex = Builder.AddScene(Scene);

		Builder.DefaultScene = SceneIndex;
	}
	else
	{
		// TODO: should we report an error if no preview mesh was found?
	}

	return true;
}
