// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFLevelConverters.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Converters/GLTFConverterUtility.h"

FGLTFJsonNodeIndex FGLTFSceneComponentConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const USceneComponent* SceneComponent, bool bSelectedOnly, bool bRootNode)
{
	const AActor* Owner = SceneComponent->GetOwner();
	const bool bIsRootComponent = Owner != nullptr && Owner->GetRootComponent() == SceneComponent;
	const UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(Owner != nullptr ? Owner->GetClass() : nullptr);

	FGLTFJsonNode Node;
	Node.Name = Name.IsEmpty() ? Owner->GetName() + TEXT("_") + SceneComponent->GetName() : Name;

	// TODO: add support for non-uniform scaling (Unreal doesn't treat combined transforms as simple matrix multiplication)
	const FTransform Transform = bRootNode ? SceneComponent->GetComponentTransform() : SceneComponent->GetRelativeTransform();
	Node.Translation = FGLTFConverterUtility::ConvertPosition(Transform.GetTranslation());
	Node.Rotation = FGLTFConverterUtility::ConvertRotation(Transform.GetRotation());
	Node.Scale = FGLTFConverterUtility::ConvertScale(Transform.GetScale3D());

	if (FGLTFConverterUtility::IsSkySphereBlueprint(Blueprint))
	{
		// Ignore mesh and light components that are part of BP_SkySphere
	}
	else if (FGLTFConverterUtility::IsHDRIBackdropBlueprint(Blueprint))
	{
		if (bIsRootComponent)
		{
			// TODO: add support for backdrop
		}
	}
	else if (const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(SceneComponent))
	{
		Node.Mesh = Builder.GetOrAddMesh(StaticMeshComponent);
	}

	const TArray<USceneComponent*>& Children = SceneComponent->GetAttachChildren();
	for (const USceneComponent* ChildComponent : Children)
	{
		if (ChildComponent != nullptr && (!bSelectedOnly || FGLTFConverterUtility::IsSelected(ChildComponent)))
		{
			FGLTFJsonNodeIndex NodeIndex = Builder.GetOrAddNode(ChildComponent, bSelectedOnly, false);
			if (NodeIndex != INDEX_NONE)
			{
				Node.Children.Add(NodeIndex);
			}
		}
	}

	return Builder.AddNode(Node);
}

FGLTFJsonSceneIndex FGLTFLevelConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const ULevel* Level, bool bSelectedOnly)
{
	FGLTFJsonScene Scene;
	Scene.Name = Name;

	// TODO: export Level->Model ?

	const TArray<AActor*>& Actors = Level->Actors;
	for (const AActor* Actor : Actors)
	{
		if (Actor != nullptr && (!bSelectedOnly || Actor->IsSelected()))
		{
			const USceneComponent* RootComponent = Actor->GetRootComponent();
			if (RootComponent != nullptr)
			{
				const USceneComponent* ParentComponent = RootComponent->GetAttachParent();
				if (ParentComponent == nullptr || (bSelectedOnly && !FGLTFConverterUtility::IsSelected(ParentComponent)))
				{
					FGLTFJsonNodeIndex NodeIndex = Builder.GetOrAddNode(RootComponent, bSelectedOnly, true);
					if (NodeIndex != INDEX_NONE)
					{
						Scene.Nodes.Add(NodeIndex);
					}
				}
			}
		}
	}

	return Builder.AddScene(Scene);
}
