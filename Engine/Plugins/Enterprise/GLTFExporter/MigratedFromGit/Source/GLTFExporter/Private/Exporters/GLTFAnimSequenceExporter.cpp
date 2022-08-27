// Copyright Epic Games, Inc. All Rights Reserved.

#include "Exporters/GLTFAnimSequenceExporter.h"
#include "Exporters/GLTFExporterUtility.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Animation/AnimSequence.h"

UGLTFAnimSequenceExporter::UGLTFAnimSequenceExporter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UAnimSequence::StaticClass();
}

bool UGLTFAnimSequenceExporter::AddObject(FGLTFContainerBuilder& Builder, const UObject* Object)
{
	const UAnimSequence* AnimSequence = CastChecked<UAnimSequence>(Object);

	const USkeletalMesh* SkeletalMesh = FGLTFExporterUtility::GetPreviewMesh(AnimSequence);
	if (SkeletalMesh == nullptr)
	{
		// report error
		return false;
	}

	const FGLTFJsonMeshIndex MeshIndex = Builder.GetOrAddMesh(SkeletalMesh);

	FGLTFJsonNode Node;
	Node.Mesh = MeshIndex;
	const FGLTFJsonNodeIndex NodeIndex = Builder.AddNode(Node);
	Builder.GetNode(NodeIndex).Skin = Builder.GetOrAddSkin(NodeIndex, SkeletalMesh);

	Builder.GetOrAddAnimation(NodeIndex, SkeletalMesh, AnimSequence);

	FGLTFJsonScene Scene;
	Scene.Nodes.Add(NodeIndex);
	const FGLTFJsonSceneIndex SceneIndex = Builder.AddScene(Scene);

	Builder.DefaultScene = SceneIndex;
	return true;
}
