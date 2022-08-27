// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFLevelConverters.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Converters/GLTFConverterUtility.h"

FGLTFJsonNodeIndex FGLTFSceneComponentConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const USceneComponent* SceneComponent)
{
	const AActor* Owner = SceneComponent->GetOwner();
	const USceneComponent* ParentComponent = SceneComponent->GetAttachParent();
	const UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(Owner != nullptr ? Owner->GetClass() : nullptr);

	const bool bSelectedOnly = Builder.bSelectedActorsOnly;
	const bool bIsRootComponent = Owner != nullptr && Owner->GetRootComponent() == SceneComponent;
	const bool bRootNode = bIsRootComponent && (ParentComponent == nullptr || (bSelectedOnly && !FGLTFConverterUtility::IsSelected(ParentComponent)));

	const FGLTFJsonNodeIndex NodeIndex = Builder.AddNode();
	FGLTFJsonNode& Node = Builder.GetNode(NodeIndex);
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

	const TArray<USceneComponent*>& ChildComponents = SceneComponent->GetAttachChildren();

	TArray<FGLTFJsonNodeIndex> Children;
	Children.Reserve(ChildComponents.Num());

	for (const USceneComponent* ChildComponent : ChildComponents)
	{
		if (ChildComponent != nullptr && (!bSelectedOnly || FGLTFConverterUtility::IsSelected(ChildComponent)))
		{
			const FGLTFJsonNodeIndex ChildNodeIndex = Builder.GetOrAddNode(ChildComponent);
			if (ChildNodeIndex != INDEX_NONE)
			{
				Children.Add(ChildNodeIndex);
			}
		}
	}

	// TODO: refactor this workaround for no longer valid reference to node (due to calling Builder.GetOrAddNode)
	Builder.GetNode(NodeIndex).Children = Children;

	return NodeIndex;
}

FGLTFJsonSceneIndex FGLTFLevelConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const ULevel* Level)
{
	const bool bSelectedOnly = Builder.bSelectedActorsOnly;

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
					FGLTFJsonNodeIndex NodeIndex = Builder.GetOrAddNode(RootComponent);
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
