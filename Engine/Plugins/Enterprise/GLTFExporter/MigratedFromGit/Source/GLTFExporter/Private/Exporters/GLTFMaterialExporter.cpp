// Copyright Epic Games, Inc. All Rights Reserved.

#include "Exporters/GLTFMaterialExporter.h"
#include "Exporters/GLTFExporterUtility.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

UGLTFMaterialExporter::UGLTFMaterialExporter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UMaterialInterface::StaticClass();

	static ConstructorHelpers::FObjectFinder<UStaticMesh> DefaultPreviewMeshFinder(TEXT("/Engine/EditorMeshes/EditorSphere.EditorSphere"));
	DefaultPreviewMesh = DefaultPreviewMeshFinder.Object;
}

bool UGLTFMaterialExporter::AddObject(FGLTFContainerBuilder& Builder, const UObject* Object)
{
	const UMaterialInterface* Material = CastChecked<UMaterialInterface>(Object);

	if (Builder.ExportOptions->bExportPreviewMesh)
	{
		const UStaticMesh* PreviewMesh = FGLTFExporterUtility::GetPreviewMesh(Material);
		if (PreviewMesh == nullptr)
		{
			PreviewMesh = DefaultPreviewMesh;
		}

		FGLTFJsonMesh* MeshIndex = Builder.GetOrAddMesh(PreviewMesh, { Material });
		if (MeshIndex == nullptr)
		{
			Builder.LogError(
				FString::Printf(TEXT("Failed to export preview mesh %s for material %s"),
				*Material->GetName(),
				*PreviewMesh->GetName()));
			return false;
		}

		FGLTFJsonNode Node;
		Node.Mesh = MeshIndex;
		FGLTFJsonNode* NodeIndex = Builder.AddNode(Node);

		FGLTFJsonScene Scene;
		Scene.Nodes.Add(NodeIndex);
		FGLTFJsonScene* SceneIndex = Builder.AddScene(Scene);

		Builder.DefaultScene = SceneIndex;
	}
	else
	{
		Builder.GetOrAddMaterial(Material);
	}

	return true;
}
