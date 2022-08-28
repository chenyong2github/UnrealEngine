// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFLevelConverters.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Converters/GLTFConverterUtility.h"
#include "Converters/GLTFActorUtility.h"
#include "Converters/GLTFCameraUtility.h"

FGLTFJsonNodeIndex FGLTFSceneComponentConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const USceneComponent* SceneComponent)
{
	const AActor* Owner = SceneComponent->GetOwner();
	if (Owner == nullptr)
	{
		// TODO: report invalid scene component
		return FGLTFJsonNodeIndex(INDEX_NONE);
	}

	const bool bIsRootComponent = Owner->GetRootComponent() == SceneComponent;
	const bool bIsRootNode = bIsRootComponent && FGLTFActorUtility::IsRootActor(Owner, Builder.bSelectedActorsOnly);

	const USceneComponent* ParentComponent = SceneComponent->GetAttachParent();
	const FGLTFJsonNodeIndex ParentNodeIndex = Builder.GetOrAddNode(ParentComponent);

	// TODO: if node is root, then add it to the scene here to avoid any possible orphan nodes. this change requires level converter to support cyclic calls.

	const FGLTFJsonNodeIndex NodeIndex = Builder.AddChildNode(ParentNodeIndex);
	FGLTFJsonNode& Node = Builder.GetNode(NodeIndex);
	Node.Name = Name.IsEmpty() ? Owner->GetName() + TEXT("_") + SceneComponent->GetName() : Name;

	// TODO: add support for non-uniform scaling (Unreal doesn't treat combined transforms as simple matrix multiplication)
	const FTransform Transform = bIsRootNode ? SceneComponent->GetComponentTransform() : SceneComponent->GetRelativeTransform();
	Node.Translation = FGLTFConverterUtility::ConvertPosition(Transform.GetTranslation());
	Node.Rotation = FGLTFConverterUtility::ConvertRotation(Transform.GetRotation());
	Node.Scale = FGLTFConverterUtility::ConvertScale(Transform.GetScale3D());

	if (SceneComponent->bHiddenInGame)
	{
		// ignore any visible properties
	}
	else if (const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(SceneComponent))
	{
		Node.Mesh = Builder.GetOrAddMesh(StaticMeshComponent);

		if (true /* TODO: make export of light-maps optional (selectable via export options) */)
		{
			Node.LightMap = Builder.GetOrAddLightMap(StaticMeshComponent);
		}
	}
	else if (const USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(SceneComponent))
	{
		Node.Mesh = Builder.GetOrAddMesh(SkeletalMeshComponent);
	}
	else if (const UCameraComponent* CameraComponent = Cast<UCameraComponent>(SceneComponent))
	{
		// TODO: conversion of camera direction should be done in separate converter
		FGLTFJsonNode CameraNode;
		CameraNode.Name = Owner->GetName(); // TODO: choose a more unique name if owner is not ACameraActor
		CameraNode.Rotation = FGLTFConverterUtility::ConvertCameraDirection();
		CameraNode.Camera = Builder.GetOrAddCamera(CameraComponent, CameraNode.Name);
		Builder.AddChildNode(NodeIndex, CameraNode);
	}
	else if (const ULightComponent* LightComponent = Cast<ULightComponent>(SceneComponent))
	{
		// TODO: conversion of light direction should be done in separate converter
		FGLTFJsonNode LightNode;
		LightNode.Name = Owner->GetName(); // TODO: choose a more unique name if owner is not ALight
		LightNode.Rotation = FGLTFConverterUtility::ConvertLightDirection();
		LightNode.Light = Builder.GetOrAddLight(LightComponent, LightNode.Name);
		Builder.AddChildNode(NodeIndex, LightNode);
	}

	return NodeIndex;
}

FGLTFJsonNodeIndex FGLTFActorConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const AActor* Actor)
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
		FGLTFJsonNode& RootNode = Builder.GetNode(RootNodeIndex);
		RootNode.Backdrop = Builder.GetOrAddBackdrop(Actor);
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

FGLTFJsonSceneIndex FGLTFLevelConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const ULevel* Level)
{
	FGLTFJsonScene Scene;
	Scene.Name = Name;

	// TODO: export Level->Model ?

	for (const AActor* Actor : Level->Actors)
	{
		if (const ALevelVariantSetsActor *LevelVariantSetsActor = Cast<ALevelVariantSetsActor>(Actor))
		{
			const FGLTFJsonLevelVariantSetsIndex LevelVariantSetsIndex = Builder.GetOrAddLevelVariantSets(LevelVariantSetsActor);
			if (LevelVariantSetsIndex != INDEX_NONE)
			{
				Scene.LevelVariantSets.Add(LevelVariantSetsIndex);
			}
		}

		const FGLTFJsonNodeIndex NodeIndex = Builder.GetOrAddNode(Actor);
		if (NodeIndex != INDEX_NONE && FGLTFActorUtility::IsRootActor(Actor, Builder.bSelectedActorsOnly))
		{
			Scene.Nodes.Add(NodeIndex);
		}
	}

	return Builder.AddScene(Scene);
}

FGLTFJsonCameraIndex FGLTFCameraComponentConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const UCameraComponent* CameraComponent)
{
	FGLTFJsonCamera Camera;
	Camera.Name = Name;
	Camera.Type = FGLTFConverterUtility::ConvertCameraType(CameraComponent->ProjectionMode);

	FMinimalViewInfo DesiredView;
	const_cast<UCameraComponent*>(CameraComponent)->GetCameraView(0, DesiredView);

	switch (Camera.Type)
	{
		case EGLTFJsonCameraType::Orthographic:
			Camera.Orthographic = FGLTFCameraUtility::ConvertOrthographic(DesiredView);
			break;

		case EGLTFJsonCameraType::Perspective:
			Camera.Perspective = FGLTFCameraUtility::ConvertPerspective(DesiredView);
			break;

		default:
		    // TODO: report unsupported camera type
		    return FGLTFJsonCameraIndex(INDEX_NONE);
	}

	return Builder.AddCamera(Camera);
}

FGLTFJsonLightIndex FGLTFLightComponentConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const ULightComponent* LightComponent)
{
	FGLTFJsonLight Light;
	Light.Name = Name;
	Light.Type = FGLTFConverterUtility::ConvertLightType(LightComponent->GetLightType());

	if (Light.Type == EGLTFJsonLightType::None)
	{
		// TODO: report unsupported light component type
		return FGLTFJsonLightIndex(INDEX_NONE);
	}

	Light.Intensity = LightComponent->Intensity;

	const FLinearColor LightColor = LightComponent->bUseTemperature ? FLinearColor::MakeFromColorTemperature(LightComponent->Temperature) : LightComponent->GetLightColor();
	Light.Color = FGLTFConverterUtility::ConvertColor(LightColor);

	if (const UPointLightComponent* PointLightComponent = Cast<UPointLightComponent>(LightComponent))
	{
		Light.Range = FGLTFConverterUtility::ConvertLength(PointLightComponent->AttenuationRadius);
	}

	if (const USpotLightComponent* SpotLightComponent = Cast<USpotLightComponent>(LightComponent))
	{
		Light.InnerConeAngle = FGLTFConverterUtility::ConvertLightAngle(SpotLightComponent->InnerConeAngle);
		Light.OuterConeAngle = FGLTFConverterUtility::ConvertLightAngle(SpotLightComponent->OuterConeAngle);
	}

	return Builder.AddLight(Light);
}
