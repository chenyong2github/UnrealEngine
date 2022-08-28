// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFNodeConverters.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Converters/GLTFConverterUtility.h"
#include "Converters/GLTFActorUtility.h"
#include "Converters/GLTFNameUtility.h"
#include "Actors/GLTFHotspotActor.h"
#include "LevelSequenceActor.h"
#include "Camera/CameraComponent.h"
#include "Components/LightComponent.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/SkeletalMeshSocket.h"

FGLTFJsonNodeIndex FGLTFActorConverter::Convert(const AActor* Actor)
{
	if (Actor->bIsEditorOnlyActor)
	{
		return FGLTFJsonNodeIndex(INDEX_NONE);
	}

	if (!Builder.IsSelectedActor(Actor))
	{
		return FGLTFJsonNodeIndex(INDEX_NONE);
	}

	const USceneComponent* RootComponent = Actor->GetRootComponent();
	const FGLTFJsonNodeIndex RootNodeIndex = Builder.GetOrAddNode(RootComponent);

	// TODO: process all components since any component can be attached to any other component in runtime

	const FString BlueprintPath = FGLTFActorUtility::GetBlueprintPath(Actor);
	if (FGLTFActorUtility::IsSkySphereBlueprint(BlueprintPath))
	{
		if (Builder.ExportOptions->bExportSkySpheres)
		{
			FGLTFJsonNode& RootNode = Builder.GetNode(RootNodeIndex);
			RootNode.SkySphere = Builder.GetOrAddSkySphere(Actor);
		}
	}
	else if (FGLTFActorUtility::IsHDRIBackdropBlueprint(BlueprintPath))
	{
		if (Builder.ExportOptions->bExportHDRIBackdrops)
		{
			FGLTFJsonNode& RootNode = Builder.GetNode(RootNodeIndex);
			RootNode.Backdrop = Builder.GetOrAddBackdrop(Actor);
		}
	}
	else if (const ALevelSequenceActor* LevelSequenceActor = Cast<ALevelSequenceActor>(Actor))
	{
		if (Builder.ExportOptions->bExportLevelSequences)
		{
			Builder.GetOrAddAnimation(LevelSequenceActor);
		}
	}
	else if (const AGLTFHotspotActor* HotspotActor = Cast<AGLTFHotspotActor>(Actor))
	{
		if (Builder.ExportOptions->bExportAnimationHotspots)
		{
			FGLTFJsonNode& RootNode = Builder.GetNode(RootNodeIndex);
			RootNode.Hotspot = Builder.GetOrAddHotspot(HotspotActor);
		}
	}
	else
	{
		// TODO: add support for exporting brush geometry?

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

FGLTFJsonNodeIndex FGLTFComponentConverter::Convert(const USceneComponent* SceneComponent)
{
	if (SceneComponent->IsEditorOnly())
	{
		return FGLTFJsonNodeIndex(INDEX_NONE);
	}

	const AActor* Owner = SceneComponent->GetOwner();
	if (Owner == nullptr)
	{
		// TODO: report error (invalid scene component)
		return FGLTFJsonNodeIndex(INDEX_NONE);
	}

	if (!Builder.IsSelectedActor(Owner))
	{
		return FGLTFJsonNodeIndex(INDEX_NONE);
	}

	const bool bIsRootComponent = Owner->GetRootComponent() == SceneComponent;
	const bool bIsRootNode = bIsRootComponent && Builder.IsRootActor(Owner);

	const USceneComponent* ParentComponent = !bIsRootNode ? SceneComponent->GetAttachParent() : nullptr;
	const FName SocketName = SceneComponent->GetAttachSocketName();
	const FGLTFJsonNodeIndex ParentNodeIndex = Builder.GetOrAddNode(ParentComponent, SocketName);

	if (ParentComponent != nullptr && !SceneComponent->IsUsingAbsoluteScale())
	{
		const FVector ParentScale = ParentComponent->GetComponentScale();
		if (!ParentScale.IsUniform())
		{
			Builder.LogWarning(
				FString::Printf(TEXT("Non-uniform parent scale (%s) for component %s (in actor %s) may be represented differently in glTF"),
				*ParentScale.ToString(),
				*SceneComponent->GetName(),
				*Owner->GetName()));
		}
	}

	const FTransform Transform = SceneComponent->GetComponentTransform();
	const FTransform ParentTransform = ParentComponent != nullptr ? ParentComponent->GetSocketTransform(SocketName) : FTransform::Identity;
	const FTransform RelativeTransform = bIsRootNode ? Transform : Transform.GetRelativeTransform(ParentTransform);

	FGLTFJsonNode* Node = Builder.AddChildNode(ParentNodeIndex);
	Node->Name = FGLTFNameUtility::GetName(SceneComponent);
	Node->Translation = FGLTFConverterUtility::ConvertPosition(RelativeTransform.GetTranslation(), Builder.ExportOptions->ExportUniformScale);
	Node->Rotation = FGLTFConverterUtility::ConvertRotation(RelativeTransform.GetRotation());
	Node->Scale = FGLTFConverterUtility::ConvertScale(RelativeTransform.GetScale3D());

	// TODO: don't export invisible components unless visibility is variable due to variant sets

	// TODO: should hidden in game be configurable like this?
	if ((Builder.ExportOptions->bExportHiddenInGame || (!SceneComponent->bHiddenInGame && !Owner->IsHidden())) && FGLTFActorUtility::IsGenericActor(Owner))
	{
		if (const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(SceneComponent))
		{
			Node->Mesh = Builder.GetOrAddMesh(StaticMeshComponent);

			if (Builder.ExportOptions->bExportLightmaps)
			{
				Node->LightMap = Builder.GetOrAddLightMap(StaticMeshComponent);
			}
		}
		else if (const USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(SceneComponent))
		{
			Node->Mesh = Builder.GetOrAddMesh(SkeletalMeshComponent);

			if (Builder.ExportOptions->bExportVertexSkinWeights)
			{
				// TODO: remove need for NodeIndex by adding support for cyclic calls in converter
				const FGLTFJsonSkinIndex SkinIndex = Builder.GetOrAddSkin(Node->Index, SkeletalMeshComponent);
				if (SkinIndex != INDEX_NONE)
				{
					Node->Skin = SkinIndex;

					if (Builder.ExportOptions->bExportAnimationSequences)
					{
						Builder.GetOrAddAnimation(Node->Index, SkeletalMeshComponent);
					}
				}
			}
		}
		else if (const UCameraComponent* CameraComponent = Cast<UCameraComponent>(SceneComponent))
		{
			if (Builder.ExportOptions->bExportCameras)
			{
				// TODO: conversion of camera direction should be done in separate converter
				FGLTFJsonNode* CameraNode = Builder.AddChildComponentNode(Node->Index);
				CameraNode->Name = FGLTFNameUtility::GetName(CameraComponent);
				CameraNode->Rotation = FGLTFConverterUtility::ConvertCameraDirection();
				CameraNode->Camera = Builder.GetOrAddCamera(CameraComponent);
			}
		}
		else if (const ULightComponent* LightComponent = Cast<ULightComponent>(SceneComponent))
		{
			if (Builder.ShouldExportLight(LightComponent->Mobility))
			{
				// TODO: conversion of light direction should be done in separate converter
				FGLTFJsonNode* LightNode = Builder.AddChildComponentNode(Node->Index);
				LightNode->Name = FGLTFNameUtility::GetName(LightComponent);
				LightNode->Rotation = FGLTFConverterUtility::ConvertLightDirection();
				LightNode->Light = Builder.GetOrAddLight(LightComponent);
			}
		}
	}

	return Node->Index;
}

FGLTFJsonNodeIndex FGLTFComponentSocketConverter::Convert(const USceneComponent* SceneComponent, FName SocketName)
{
	const FGLTFJsonNodeIndex NodeIndex = Builder.GetOrAddNode(SceneComponent);

	if (SocketName != NAME_None)
	{
		if (const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(SceneComponent))
		{
			const UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
			return Builder.GetOrAddNode(NodeIndex, StaticMesh, SocketName);
		}

		if (const USkinnedMeshComponent* SkinnedMeshComponent = Cast<USkinnedMeshComponent>(SceneComponent))
		{
			// TODO: add support for SocketOverrideLookup?
			const USkeletalMesh* SkeletalMesh = SkinnedMeshComponent->SkeletalMesh;
			return Builder.GetOrAddNode(NodeIndex, SkeletalMesh, SocketName);
		}

		// TODO: add support for more socket types

		Builder.LogWarning(
			FString::Printf(TEXT("Can't export socket %s because it belongs to an unsupported mesh component %s"),
			*SocketName.ToString(),
			*SceneComponent->GetName()));
	}

	return NodeIndex;
}

FGLTFJsonNodeIndex FGLTFStaticSocketConverter::Convert(FGLTFJsonNodeIndex RootNode, const UStaticMesh* StaticMesh, FName SocketName)
{
	const UStaticMeshSocket* Socket = StaticMesh->FindSocket(SocketName);
	if (Socket == nullptr)
	{
		// TODO: report error
		return FGLTFJsonNodeIndex(INDEX_NONE);
	}

	FGLTFJsonNode Node;
	Node.Name = SocketName.ToString();

	// TODO: add warning check for non-uniform scaling
	Node.Translation = FGLTFConverterUtility::ConvertPosition(Socket->RelativeLocation, Builder.ExportOptions->ExportUniformScale);
	Node.Rotation = FGLTFConverterUtility::ConvertRotation(Socket->RelativeRotation.Quaternion());
	Node.Scale = FGLTFConverterUtility::ConvertScale(Socket->RelativeScale);

	return Builder.AddChildNode(RootNode, Node);
}

FGLTFJsonNodeIndex FGLTFSkeletalSocketConverter::Convert(FGLTFJsonNodeIndex RootNode, const USkeletalMesh* SkeletalMesh, FName SocketName)
{
#if (ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION >= 27)
	const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
#else
	const FReferenceSkeleton& RefSkeleton = SkeletalMesh->RefSkeleton;
#endif

	const USkeletalMeshSocket* Socket = SkeletalMesh->FindSocket(SocketName);
	if (Socket != nullptr)
	{
		FGLTFJsonNode Node;
		Node.Name = SocketName.ToString();

		// TODO: add warning check for non-uniform scaling
		Node.Translation = FGLTFConverterUtility::ConvertPosition(Socket->RelativeLocation, Builder.ExportOptions->ExportUniformScale);
		Node.Rotation = FGLTFConverterUtility::ConvertRotation(Socket->RelativeRotation.Quaternion());
		Node.Scale = FGLTFConverterUtility::ConvertScale(Socket->RelativeScale);

		const int32 ParentBone = RefSkeleton.FindBoneIndex(Socket->BoneName);
		const FGLTFJsonNodeIndex ParentNode = ParentBone != INDEX_NONE ? Builder.GetOrAddNode(RootNode, SkeletalMesh, ParentBone) : RootNode;
		return Builder.AddChildNode(ParentNode, Node);
	}

	const int32 BoneIndex = RefSkeleton.FindBoneIndex(SocketName);
	if (BoneIndex != INDEX_NONE)
	{
		return Builder.GetOrAddNode(RootNode, SkeletalMesh, BoneIndex);
	}

	// TODO: report error
	return FGLTFJsonNodeIndex(INDEX_NONE);
}

FGLTFJsonNodeIndex FGLTFSkeletalBoneConverter::Convert(FGLTFJsonNodeIndex RootNode, const USkeletalMesh* SkeletalMesh, int32 BoneIndex)
{
#if (ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION >= 27)
	const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
#else
	const FReferenceSkeleton& RefSkeleton = SkeletalMesh->RefSkeleton;
#endif

	// TODO: add support for MasterPoseComponent?

	const TArray<FMeshBoneInfo>& BoneInfos = RefSkeleton.GetRefBoneInfo();
	if (!BoneInfos.IsValidIndex(BoneIndex))
	{
		// TODO: report error
		return FGLTFJsonNodeIndex(INDEX_NONE);
	}

	const FMeshBoneInfo& BoneInfo = BoneInfos[BoneIndex];

	FGLTFJsonNode Node;
	Node.Name = BoneInfo.Name.ToString();

	const TArray<FTransform>& BonePoses = RefSkeleton.GetRefBonePose();
	if (BonePoses.IsValidIndex(BoneIndex))
	{
		// TODO: add warning check for non-uniform scaling
		const FTransform& BonePose = BonePoses[BoneIndex];
		Node.Translation = FGLTFConverterUtility::ConvertPosition(BonePose.GetTranslation(), Builder.ExportOptions->ExportUniformScale);
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
