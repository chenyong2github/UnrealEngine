// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFLevelConverters.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Converters/GLTFConverterUtility.h"
#include "Converters/GLTFActorUtility.h"
#include "Engine/MapBuildDataRegistry.h"

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
	else if (const UCameraComponent* CameraComponent = Cast<UCameraComponent>(SceneComponent))
	{
		Node.Camera = Builder.GetOrAddCamera(CameraComponent);
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
	else if (const ACameraActor* CameraActor = Cast<ACameraActor>(Actor))
	{
		// Ignore any other components inside camera actor (like proxy static mesh component)
		// TODO: do we want to have to do this? Or can we rely on bHiddenInGame?
		FGLTFJsonNode& RootNode = Builder.GetNode(RootNodeIndex);
		RootNode.Camera = Builder.GetOrAddCamera(CameraActor->GetCameraComponent());
	}
	else
	{
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
		else
		{
			const FGLTFJsonNodeIndex NodeIndex = Builder.GetOrAddNode(Actor);
			if (NodeIndex != INDEX_NONE && FGLTFActorUtility::IsRootActor(Actor, Builder.bSelectedActorsOnly))
			{
				Scene.Nodes.Add(NodeIndex);
			}
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
			Camera.Orthographic = FGLTFConverterUtility::ConvertOrthographic(DesiredView);
			break;

		case EGLTFJsonCameraType::Perspective:
			Camera.Perspective = FGLTFConverterUtility::ConvertPerspective(DesiredView);
			break;

		default:
			checkNoEntry();
	}

	return Builder.AddCamera(Camera);
}
