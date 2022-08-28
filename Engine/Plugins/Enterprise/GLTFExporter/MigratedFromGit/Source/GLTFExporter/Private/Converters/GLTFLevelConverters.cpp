// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFLevelConverters.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Converters/GLTFConverterUtility.h"

FGLTFJsonNodeIndex FGLTFSceneComponentConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, TTuple<const USceneComponent*, bool, bool> Params)
{
	const USceneComponent* SceneComponent = Params.Get<0>();
	const bool bSelectedOnly = Params.Get<1>();
	const bool bRootNode = Params.Get<2>();

	const AActor* Owner = SceneComponent->GetOwner();
	const bool bIsRootComponent = Owner != nullptr && Owner->GetRootComponent() == SceneComponent;
	const UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(Owner != nullptr ? Owner->GetClass() : nullptr);

	FGLTFJsonNode Node;
	Node.Name = Name.IsEmpty() ? Owner->GetName() + TEXT("_") + SceneComponent->GetName() : Name;

	const FTransform Transform = bRootNode ? SceneComponent->GetComponentTransform() : SceneComponent->GetRelativeTransform();
	Node.Translation = FGLTFConverterUtility::ConvertPosition(Transform.GetTranslation());
	Node.Rotation = FGLTFConverterUtility::ConvertRotation(Transform.GetRotation());
	Node.Scale = FGLTFConverterUtility::ConvertScale(Transform.GetScale3D());

	if (FGLTFConverterUtility::IsSkySphereBlueprint(Blueprint))
	{
		// Ignore components
	}
	else if (const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(SceneComponent))
	{
		Node.Mesh = Builder.GetOrAddMesh(StaticMeshComponent);
	}
	else if (FGLTFConverterUtility::IsHDRIBackdropBlueprint(Blueprint) && bIsRootComponent)
	{
		// TODO: add support for backdrop
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

FGLTFJsonSceneIndex FGLTFLevelConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, TTuple<const ULevel*, bool> Params)
{
	const ULevel* Level = Params.Get<0>();
	const bool bSelectedOnly = Params.Get<1>();

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
