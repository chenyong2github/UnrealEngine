// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFNodeConverters.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Converters/GLTFConverterUtility.h"
#include "Converters/GLTFActorUtility.h"
#include "Converters/GLTFNameUtility.h"
#include "Actors/GLTFInteractionHotspotActor.h"

FGLTFJsonNodeIndex FGLTFComponentSocketConverter::Convert(const USceneComponent* SceneComponent, FName SocketName)
{
	const FGLTFJsonNodeIndex NodeIndex = Builder.GetOrAddNode(SceneComponent);

	if (const USkinnedMeshComponent* SkinnedMeshComponent = Cast<USkinnedMeshComponent>(SceneComponent))
	{
		// TODO: add support for SocketOverrideLookup?
		const USkeletalMesh* SkeletalMesh = SkinnedMeshComponent->SkeletalMesh;
		return Builder.GetOrAddNode(NodeIndex, SkeletalMesh, SocketName);
	}

	// TODO: add support for non-skeletalmesh sockets

	return FGLTFJsonNodeIndex(INDEX_NONE);
}

FGLTFJsonNodeIndex FGLTFComponentConverter::Convert(const USceneComponent* SceneComponent)
{
	const AActor* Owner = SceneComponent->GetOwner();
	if (Owner == nullptr)
	{
		// TODO: report error (invalid scene component)
		return FGLTFJsonNodeIndex(INDEX_NONE);
	}

	if (Builder.bSelectedActorsOnly && !Owner->IsSelected())
	{
		return FGLTFJsonNodeIndex(INDEX_NONE);
	}

	const bool bIsRootComponent = Owner->GetRootComponent() == SceneComponent;
	const bool bIsRootNode = bIsRootComponent && FGLTFActorUtility::IsRootActor(Owner, Builder.bSelectedActorsOnly);

	// TODO: if node is root, then add it to the scene here to avoid any possible orphan nodes. this change requires node converter to support cyclic calls.

	const USceneComponent* ParentComponent = !bIsRootNode ? SceneComponent->GetAttachParent() : nullptr;
	const FGLTFJsonNodeIndex ParentNodeIndex = Builder.GetOrAddNode(ParentComponent);

	if (ParentComponent != nullptr && !SceneComponent->IsUsingAbsoluteScale())
	{
		const FVector ParentScale = ParentComponent->GetComponentScale();
		if (ParentScale.X != ParentScale.Y || ParentScale.Y != ParentScale.Z)
		{
			Builder.AddWarningMessage(
			FString::Printf(TEXT("Non-uniform parent scale (%s) for component %s (in actor %s) may be represented differently in glTF"),
				*ParentScale.ToString(),
				*SceneComponent->GetName(),
				*Owner->GetName()));
		}
	}

	const FTransform Transform = SceneComponent->GetComponentTransform();
	const FTransform RelativeTransform = bIsRootNode ? Transform : Transform.GetRelativeTransform(ParentComponent->GetComponentTransform());

	const FGLTFJsonNodeIndex NodeIndex = Builder.AddChildNode(ParentNodeIndex);
	FGLTFJsonNode& Node = Builder.GetNode(NodeIndex);
	Node.Name = FGLTFNameUtility::GetName(SceneComponent);
	Node.Translation = FGLTFConverterUtility::ConvertPosition(RelativeTransform.GetTranslation(), Builder.ExportOptions->ExportScale);
	Node.Rotation = FGLTFConverterUtility::ConvertRotation(RelativeTransform.GetRotation());
	Node.Scale = FGLTFConverterUtility::ConvertScale(RelativeTransform.GetScale3D());

	if (SceneComponent->bHiddenInGame) // TODO: make this configurable
	{
		// ignore any visible properties
	}
	else if (const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(SceneComponent))
	{
		Node.Mesh = Builder.GetOrAddMesh(StaticMeshComponent);

		if (Builder.ExportOptions->bExportLightmaps)
		{
			Node.LightMap = Builder.GetOrAddLightMap(StaticMeshComponent);
		}
	}
	else if (const USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(SceneComponent))
	{
		Node.Mesh = Builder.GetOrAddMesh(SkeletalMeshComponent);

		if (Builder.ExportOptions->bExportVertexSkinWeights)
		{
			// TODO: remove need for NodeIndex by adding support for cyclic calls in converter
			const FGLTFJsonSkinIndex SkinIndex = Builder.GetOrAddSkin(NodeIndex, SkeletalMeshComponent);
			if (SkinIndex != INDEX_NONE)
			{
				Builder.GetNode(NodeIndex).Skin = SkinIndex;

				if (Builder.ExportOptions->bExportAnimationSequences)
				{
					Builder.GetOrAddAnimation(NodeIndex, SkeletalMeshComponent);
				}
			}
		}
	}
	else if (const UCameraComponent* CameraComponent = Cast<UCameraComponent>(SceneComponent))
	{
		if (Builder.ExportOptions->bExportCameras)
		{
			// TODO: conversion of camera direction should be done in separate converter
			FGLTFJsonNode CameraNode;
			CameraNode.Name = FGLTFNameUtility::GetName(CameraComponent);
			CameraNode.Rotation = FGLTFConverterUtility::ConvertCameraDirection();
			CameraNode.Camera = Builder.GetOrAddCamera(CameraComponent);
			Builder.AddChildComponentNode(NodeIndex, CameraNode);
		}
	}
	else if (const ULightComponent* LightComponent = Cast<ULightComponent>(SceneComponent))
	{
		if (Builder.ExportOptions->ShouldExportLight(LightComponent->Mobility))
		{
			// TODO: conversion of light direction should be done in separate converter
			FGLTFJsonNode LightNode;
			LightNode.Name = FGLTFNameUtility::GetName(LightComponent);
			LightNode.Rotation = FGLTFConverterUtility::ConvertLightDirection();
			LightNode.Light = Builder.GetOrAddLight(LightComponent);
			Builder.AddChildComponentNode(NodeIndex, LightNode);
		}
	}

	// TODO: add support for SkyLight?

	return NodeIndex;
}

FGLTFJsonNodeIndex FGLTFActorConverter::Convert(const AActor* Actor)
{
	if (Builder.bSelectedActorsOnly && !Actor->IsSelected())
	{
		return FGLTFJsonNodeIndex(INDEX_NONE);
	}

	const USceneComponent* RootComponent = Actor->GetRootComponent();
	const FGLTFJsonNodeIndex RootNodeIndex = Builder.GetOrAddNode(RootComponent);

	const UBlueprint* Blueprint = FGLTFActorUtility::GetBlueprintFromActor(Actor);
	if (FGLTFActorUtility::IsSkySphereBlueprint(Blueprint))
	{
		// Ignore mesh and light components that are part of BP_SkySphere
		// TODO: add support for BP_SkySphere later?
	}
	else if (FGLTFActorUtility::IsHDRIBackdropBlueprint(Blueprint))
	{
		if (Builder.ExportOptions->bExportHDRIBackdrops)
		{
			FGLTFJsonNode& RootNode = Builder.GetNode(RootNodeIndex);
			RootNode.Backdrop = Builder.GetOrAddBackdrop(Actor);
		}
	}
	else if (const AGLTFInteractionHotspotActor* HotspotActor = Cast<AGLTFInteractionHotspotActor>(Actor))
	{
		if (Builder.ExportOptions->bExportInteractionHotspots)
		{
			FGLTFJsonNode& RootNode = Builder.GetNode(RootNodeIndex);
			RootNode.Hotspot = Builder.GetOrAddHotspot(HotspotActor);
		}
	}
	else
	{
		// TODO: to reduce number of nodes, only export components that are of interest

		for (const UActorComponent* Component : Actor->GetComponents())
		{
			const USceneComponent* SceneComponent = Cast<USceneComponent>(Component);
			if (SceneComponent != nullptr)
			{
				Builder.GetOrAddNode(SceneComponent);
			}
		}
	}

	return RootNodeIndex;
}

FGLTFJsonNodeIndex FGLTFSkeletalSocketConverter::Convert(FGLTFJsonNodeIndex RootNode, const USkeletalMesh* SkeletalMesh, FName SocketName)
{
	const USkeletalMeshSocket* Socket = SkeletalMesh->FindSocket(SocketName);
	if (Socket != nullptr)
	{
		FGLTFJsonNode Node;
		Node.Name = SocketName.ToString();

		// TODO: add support for non-uniform scaling
		const FTransform Transform = Socket->GetSocketLocalTransform();
		Node.Translation = FGLTFConverterUtility::ConvertPosition(Transform.GetTranslation(), Builder.ExportOptions->ExportScale);
		Node.Rotation = FGLTFConverterUtility::ConvertRotation(Transform.GetRotation());
		Node.Scale = FGLTFConverterUtility::ConvertScale(Transform.GetScale3D());

		const int32 ParentBone = SkeletalMesh->RefSkeleton.FindBoneIndex(Socket->BoneName);
		const FGLTFJsonNodeIndex ParentNode = ParentBone != INDEX_NONE ? Builder.GetOrAddNode(RootNode, SkeletalMesh, ParentBone) : RootNode;
		return Builder.AddChildNode(ParentNode, Node);
	}

	const int32 BoneIndex = SkeletalMesh->RefSkeleton.FindBoneIndex(SocketName);
	if (BoneIndex != INDEX_NONE)
	{
		return Builder.GetOrAddNode(RootNode, SkeletalMesh, BoneIndex);
	}

	// TODO: report error
	return FGLTFJsonNodeIndex(INDEX_NONE);
}

FGLTFJsonNodeIndex FGLTFSkeletalBoneConverter::Convert(FGLTFJsonNodeIndex RootNode, const USkeletalMesh* SkeletalMesh, int32 BoneIndex)
{
	// TODO: add support for MasterPoseComponent?

	const TArray<FMeshBoneInfo>& BoneInfos = SkeletalMesh->RefSkeleton.GetRefBoneInfo();
	if (!BoneInfos.IsValidIndex(BoneIndex))
	{
		// TODO: report error
		return FGLTFJsonNodeIndex(INDEX_NONE);
	}

	const FMeshBoneInfo& BoneInfo = BoneInfos[BoneIndex];

	FGLTFJsonNode Node;
	Node.Name = BoneInfo.Name.ToString();

	const TArray<FTransform>& BonePoses = SkeletalMesh->RefSkeleton.GetRefBonePose();
	if (BonePoses.IsValidIndex(BoneIndex))
	{
		// TODO: add support for non-uniform scaling
		const FTransform& BonePose = BonePoses[BoneIndex];
		Node.Translation = FGLTFConverterUtility::ConvertPosition(BonePose.GetTranslation(), Builder.ExportOptions->ExportScale);
		Node.Rotation = FGLTFConverterUtility::ConvertRotation(BonePose.GetRotation());
		Node.Scale = FGLTFConverterUtility::ConvertScale(BonePose.GetScale3D());
	}
	else
	{
		// TODO: report error
	}

	const FGLTFJsonNodeIndex ParentNode = BoneInfo.ParentIndex != INDEX_NONE ? Builder.GetOrAddNode(RootNode, SkeletalMesh, BoneInfo.ParentIndex) : RootNode;
	return Builder.AddChildNode(ParentNode, Node);
}
