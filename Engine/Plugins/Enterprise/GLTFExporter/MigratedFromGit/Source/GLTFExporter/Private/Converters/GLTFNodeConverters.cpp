// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFNodeConverters.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Converters/GLTFConverterUtility.h"
#include "Converters/GLTFActorUtility.h"
#include "Converters/GLTFNameUtility.h"
#include "Components/GLTFInteractionHotspotComponent.h"

FGLTFJsonNodeIndex FGLTFSceneComponentConverter::Convert(const USceneComponent* SceneComponent)
{
	const AActor* Owner = SceneComponent->GetOwner();
	if (Owner == nullptr)
	{
		// TODO: report error (invalid scene component)
		return FGLTFJsonNodeIndex(INDEX_NONE);
	}

	const bool bIsRootComponent = Owner->GetRootComponent() == SceneComponent;
	const bool bIsRootNode = bIsRootComponent && FGLTFActorUtility::IsRootActor(Owner, Builder.bSelectedActorsOnly);
	const bool bExportNonUniformScale = Builder.ExportOptions->bExportNonUniformScale;

	const USceneComponent* ParentComponent = SceneComponent->GetAttachParent();
	const FGLTFJsonNodeIndex ParentNodeIndex = Builder.GetOrAddNode(ParentComponent);

	// TODO: if node is root, then add it to the scene here to avoid any possible orphan nodes. this change requires level converter to support cyclic calls.

	const FTransform Transform = SceneComponent->GetComponentTransform();
	const FTransform RelativeTransform = bIsRootNode ? Transform : Transform.GetRelativeTransform(ParentComponent->GetComponentTransform());

	const FVector ParentScale = ParentComponent != nullptr ? ParentComponent->GetComponentScale() : FVector::OneVector;
	const FVector Translation = bExportNonUniformScale ? RelativeTransform.GetTranslation() * ParentScale : RelativeTransform.GetTranslation();
	const FVector Scale = bExportNonUniformScale ? FVector::OneVector : RelativeTransform.GetScale3D();

	const FGLTFJsonNodeIndex NodeIndex = Builder.AddChildNode(ParentNodeIndex);
	FGLTFJsonNode& Node = Builder.GetNode(NodeIndex);
	Node.Name = FGLTFNameUtility::GetName(SceneComponent);
	Node.Translation = FGLTFConverterUtility::ConvertPosition(Translation, Builder.ExportOptions->ExportScale);
	Node.Rotation = FGLTFConverterUtility::ConvertRotation(RelativeTransform.GetRotation());
	Node.Scale = FGLTFConverterUtility::ConvertScale(Scale);

	const FGLTFJsonVector3 ComponentNodeScale = FGLTFConverterUtility::ConvertScale(bExportNonUniformScale ? Transform.GetScale3D() : FVector::OneVector);

	if (SceneComponent->bHiddenInGame) // TODO: make this configurable
	{
		// ignore any visible properties
	}
	else if (const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(SceneComponent))
	{
		if (bExportNonUniformScale)
		{
			FGLTFJsonNode MeshNode;
			MeshNode.Name = FGLTFNameUtility::GetName(StaticMeshComponent);
			MeshNode.Scale = ComponentNodeScale;
			MeshNode.Mesh = Builder.GetOrAddMesh(StaticMeshComponent);
			MeshNode.LightMap = Builder.GetOrAddLightMap(StaticMeshComponent);
			Builder.AddChildComponentNode(NodeIndex, MeshNode);
		}
		else
		{
			Node.Mesh = Builder.GetOrAddMesh(StaticMeshComponent);
			Node.LightMap = Builder.GetOrAddLightMap(StaticMeshComponent);
		}
	}
	else if (const USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(SceneComponent))
	{
		if (bExportNonUniformScale)
		{
			FGLTFJsonNode MeshNode;
			MeshNode.Name = FGLTFNameUtility::GetName(SkeletalMeshComponent);
			MeshNode.Scale = ComponentNodeScale;
			MeshNode.Mesh = Builder.GetOrAddMesh(SkeletalMeshComponent);
			Builder.AddChildComponentNode(NodeIndex, MeshNode);
		}
		else
		{
			Node.Mesh = Builder.GetOrAddMesh(SkeletalMeshComponent);
		}
	}
	else if (const UGLTFInteractionHotspotComponent* HotspotComponent = Cast<UGLTFInteractionHotspotComponent>(SceneComponent))
	{
		if (Builder.ExportOptions->bExportInteractionHotspots)
		{
			if (bExportNonUniformScale)
			{
				FGLTFJsonNode HotspotNode;
				HotspotNode.Name = FGLTFNameUtility::GetName(HotspotComponent);
				HotspotNode.Scale = ComponentNodeScale;

				HotspotNode.Hotspot = Builder.GetOrAddHotspot(HotspotComponent);
				Builder.AddChildComponentNode(NodeIndex, HotspotNode);
			}
			else
			{
				Node.Hotspot = Builder.GetOrAddHotspot(HotspotComponent);
			}
		}
		else
		{
			Builder.AddWarningMessage(FString::Printf(TEXT("Interaction-hotspot %s disabled by export options"), *Owner->GetName()));
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
			CameraNode.Scale = ComponentNodeScale;
			CameraNode.Camera = Builder.GetOrAddCamera(CameraComponent);
			Builder.AddChildComponentNode(NodeIndex, CameraNode);
		}
		else
		{
			Builder.AddWarningMessage(FString::Printf(TEXT("Camera %s disabled by export options"), *Owner->GetName()));
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
			LightNode.Scale = ComponentNodeScale;
			LightNode.Light = Builder.GetOrAddLight(LightComponent);
			Builder.AddChildComponentNode(NodeIndex, LightNode);
		}
		else
		{
			Builder.AddWarningMessage(FString::Printf(TEXT("Light %s disabled by export options"), *Owner->GetName()));
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
		else
		{
			Builder.AddWarningMessage(FString::Printf(
				TEXT("HDRIBackdrop %s disabled by export options"),
				*Actor->GetName()));
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
