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
	if (MeshIndex == INDEX_NONE)
	{
		Builder.AddErrorMessage(FString::Printf(TEXT("Failed to export skeletal mesh %s"), *SkeletalMesh->GetName()));
		return false;
	}

	FGLTFJsonNode Node;
	Node.Mesh = MeshIndex;
	const FGLTFJsonNodeIndex NodeIndex = Builder.AddNode(Node);

	const FGLTFJsonSkinIndex SkinIndex = Builder.GetOrAddSkin(NodeIndex, SkeletalMesh);
	if (SkinIndex == INDEX_NONE)
	{
		Builder.AddErrorMessage(FString::Printf(TEXT("Failed to export bones in skeletal mesh %s"), *SkeletalMesh->GetName()));
		return false;
	}

	Builder.GetNode(NodeIndex).Skin = SkinIndex;

	if (Builder.ExportOptions->bExportAnimationSequences)
	{
		const FGLTFJsonAnimationIndex AnimationIndex =  Builder.GetOrAddAnimation(NodeIndex, SkeletalMesh, AnimSequence);
		if (AnimationIndex == INDEX_NONE)
		{
			Builder.AddErrorMessage(FString::Printf(TEXT("Failed to export animation sequence %s"), *AnimSequence->GetName()));
			return false;
		}
	}
	else
	{
		Builder.AddWarningMessage(FString::Printf(TEXT("Export of animation sequences disabled by export options")));
	}

	FGLTFJsonScene Scene;
	Scene.Nodes.Add(NodeIndex);
	const FGLTFJsonSceneIndex SceneIndex = Builder.AddScene(Scene);

	Builder.DefaultScene = SceneIndex;
	return true;
}
