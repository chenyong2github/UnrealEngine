// Copyright Epic Games, Inc. All Rights Reserved.

//#include "GLTFLevelConverter.h"

#include "GLTFLevelConverters.h"
#include "GLTFContainerBuilder.h"
#include "GLTFConversionUtilities.h"

FGLTFJsonNodeIndex FGLTFSceneComponentConverter::Convert(FGLTFContainerBuilder& Container, const FString& Name, const USceneComponent* SceneComponent, bool bSelectedOnly, bool bRootNode)
{
	const AActor* Owner = SceneComponent->GetOwner();
	const bool bIsRootComponent = Owner != nullptr && Owner->GetRootComponent() == SceneComponent;
	const UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(Owner != nullptr ? Owner->GetClass() : nullptr);

	FGLTFJsonNode Node;
	Node.Name = Name.IsEmpty() ? Owner->GetName() + TEXT("_") + SceneComponent->GetName() : Name;

	const FTransform Transform = bRootNode ? SceneComponent->GetComponentTransform() : SceneComponent->GetRelativeTransform();
	Node.Translation = ConvertPosition(Transform.GetTranslation());
	Node.Rotation = ConvertRotation(Transform.GetRotation());
	Node.Scale = ConvertScale(Transform.GetScale3D());
	
	if (IsSkySphereBlueprint(Blueprint))
	{
		// Ignore components
	}
	else if (const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(SceneComponent))
	{
		Node.Mesh = Container.ConvertMesh(StaticMeshComponent);
	}
	else if (IsHDRIBackdropBlueprint(Blueprint) && bIsRootComponent)
	{
		// TODO: add support for backdrop
	}

	const TArray<USceneComponent*>& Children = SceneComponent->GetAttachChildren();
	for (const USceneComponent* ChildComponent : Children)
	{
		if (ChildComponent != nullptr)
		{
			const AActor* ChildOwner = ChildComponent->GetOwner();
			if (!bSelectedOnly || ChildOwner->IsSelected())
			{
				FGLTFJsonNodeIndex NodeIndex = Container.ConvertNode(ChildComponent, bSelectedOnly, false);
				if (NodeIndex != INDEX_NONE) Node.Children.Add(NodeIndex);
			}
		}
	}

	return Container.AddNode(Node);
}

FGLTFJsonSceneIndex FGLTFLevelConverter::Convert(FGLTFContainerBuilder& Container, const FString& Name, const ULevel* Level, bool bSelectedOnly)
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
				const AActor* ParentActor = Actor->GetParentActor();
				if (ParentActor == nullptr || (bSelectedOnly && !ParentActor->IsSelected()))
				{
					FGLTFJsonNodeIndex NodeIndex = Container.ConvertNode(RootComponent, bSelectedOnly, true);
					if (NodeIndex != INDEX_NONE) Scene.Nodes.Add(NodeIndex);
				}
			}
		}
	}

	return Container.AddScene(Scene);
}
