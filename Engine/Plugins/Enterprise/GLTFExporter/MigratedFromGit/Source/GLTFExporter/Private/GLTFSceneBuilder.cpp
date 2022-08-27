// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFSceneBuilder.h"
#include "GLTFConversionUtilities.h"
#include "GLTFExporterModule.h"

FGLTFNodeBuilder::FGLTFNodeBuilder(const USceneComponent* SceneComponent, const AActor* ComponentOwner, bool bSelectedOnly, bool bTopLevel)
	: SceneComponent(SceneComponent)
	, ComponentOwner(ComponentOwner)
	, bTopLevel(bTopLevel)
{
	Name = ComponentOwner->GetName() + TEXT("_") + SceneComponent->GetName();

	const TArray<USceneComponent*>& Children = SceneComponent->GetAttachChildren();
	for (const USceneComponent* ChildComponent : Children)
	{
		if (ChildComponent != nullptr)
		{
			const AActor* ChildOwner = ChildComponent->GetOwner();
			if (!bSelectedOnly || ChildOwner->IsSelected())
			{
				AttachedComponents.Add(FGLTFNodeBuilder(ChildComponent, ChildOwner, bSelectedOnly));
			}
		}
	}
}

FGLTFJsonNodeIndex FGLTFNodeBuilder::AddNode(FGLTFContainerBuilder& Container) const
{
	FGLTFJsonNode Node;
	Node.Name = Name;

	FTransform Transform = bTopLevel ? SceneComponent->GetComponentTransform() : SceneComponent->GetRelativeTransform();
	Node.Translation = ConvertPosition(Transform.GetTranslation());
	Node.Rotation = ConvertRotation(Transform.GetRotation());
	Node.Scale = ConvertScale(Transform.GetScale3D());

	const UClass* OwnerClass = ComponentOwner->GetClass();
	const UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(OwnerClass);
	
	if (IsSkySphereBlueprint(Blueprint))
	{
		// Ignore components
	}
	else if (const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(SceneComponent))
	{
		Node.Mesh = Container.AddMesh(StaticMeshComponent);
	}
	else if (IsHDRIBackdropBlueprint(Blueprint) && ComponentOwner->GetRootComponent() == SceneComponent)
	{
		// TODO: add support for backdrop
	}

	for (const FGLTFNodeBuilder& AttachedComponent : AttachedComponents)
	{
		FGLTFJsonNodeIndex ChildIndex = AttachedComponent.AddNode(Container);
		if (ChildIndex != INDEX_NONE) Node.Children.Add(ChildIndex);
	}

	return Container.AddNode(Node);
}

FGLTFSceneBuilder::FGLTFSceneBuilder(const UWorld* World, bool bSelectedOnly)
{
	World->GetName(Name);

	const ULevel* Level = World->PersistentLevel;

	// TODO: export Level->Model

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
					TopLevelComponents.Add(FGLTFNodeBuilder(RootComponent, Actor, bSelectedOnly, true));
				}
			}
		}
	}
}

FGLTFJsonSceneIndex FGLTFSceneBuilder::AddScene(FGLTFContainerBuilder& Container) const
{
	FGLTFJsonScene Scene;
	Scene.Name = Name;

	for (const FGLTFNodeBuilder& TopLevelComponent : TopLevelComponents)
	{
		FGLTFJsonNodeIndex NodeIndex = TopLevelComponent.AddNode(Container);
		if (NodeIndex != INDEX_NONE) Scene.Nodes.Add(NodeIndex);
	}

	return Container.AddScene(Scene);
}
