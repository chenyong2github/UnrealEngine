// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFSceneBuilder.h"
#include "GLTFConverterUtility.h"
#include "GLTFExporterModule.h"

FGLTFNodeBuilder::FGLTFNodeBuilder(const USceneComponent* SceneComponent, const AActor* ComponentOwner, bool bSelectedOnly, bool bRootNode)
	: SceneComponent(SceneComponent)
	, ComponentOwner(ComponentOwner)
	, bRootNode(bRootNode)
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

	const FTransform Transform = bRootNode ? SceneComponent->GetComponentTransform() : SceneComponent->GetRelativeTransform();
	Node.Translation = FGLTFConverterUtility::ConvertPosition(Transform.GetTranslation());
	Node.Rotation = FGLTFConverterUtility::ConvertRotation(Transform.GetRotation());
	Node.Scale = FGLTFConverterUtility::ConvertScale(Transform.GetScale3D());

	const UClass* OwnerClass = ComponentOwner->GetClass();
	const UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(OwnerClass);
	const bool bIsRootComponent = ComponentOwner->GetRootComponent() == SceneComponent;

	if (FGLTFConverterUtility::IsSkySphereBlueprint(Blueprint))
	{
		// Avoid exporting any SkySphere actor components (like StaticMeshComponent)
	}
	else if (const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(SceneComponent))
	{
		Node.Mesh = Container.AddMesh(StaticMeshComponent);
	}
	else if (bIsRootComponent && FGLTFConverterUtility::IsHDRIBackdropBlueprint(Blueprint))
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
					RootNodes.Add(FGLTFNodeBuilder(RootComponent, Actor, bSelectedOnly, true));
				}
			}
		}
	}
}

FGLTFJsonSceneIndex FGLTFSceneBuilder::AddScene(FGLTFContainerBuilder& Container) const
{
	FGLTFJsonScene Scene;
	Scene.Name = Name;

	for (const FGLTFNodeBuilder& TopLevelComponent : RootNodes)
	{
		FGLTFJsonNodeIndex NodeIndex = TopLevelComponent.AddNode(Container);
		if (NodeIndex != INDEX_NONE) Scene.Nodes.Add(NodeIndex);
	}

	return Container.AddScene(Scene);
}
