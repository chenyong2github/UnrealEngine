// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFSceneBuilder.h"
#include "GLTFConversionUtilities.h"
#include "GLTFExporterModule.h"

FGLTFNodeBuilder::FGLTFNodeBuilder(const USceneComponent* SceneComponent, bool bSelectedOnly, bool bTopLevel)
	: SceneComponent(SceneComponent)
	, bTopLevel(bTopLevel)
{
	const AActor* Owner = SceneComponent->GetOwner();
	Name = Owner->GetName() + TEXT("_") + SceneComponent->GetName();

	const TArray<USceneComponent*>& Children = SceneComponent->GetAttachChildren();
	for (USceneComponent* ChildComponent : Children)
	{
		if (ChildComponent != nullptr && (!bSelectedOnly || ChildComponent->GetOwner()->IsSelected()))
		{
			AttachedComponents.Add(FGLTFNodeBuilder(ChildComponent, bSelectedOnly));
		}
	}
}

FGLTFJsonNodeIndex FGLTFNodeBuilder::AddNode(FGLTFContainerBuilder& Container)
{
	FGLTFJsonNode Node;

	// TODO:

	return Container.AddNode(Node);
}

void FGLTFNodeBuilder::_DebugLog()
{
	FString Path;
	auto Component = SceneComponent;

	do
	{
		auto Actor = Component->GetOwner();
		auto ActorName = Actor ? Actor->GetName() : TEXT("null");
		Path = TEXT(" / ") + ActorName + TEXT("_") + Component->GetName() + Path;
		Component = Component->GetAttachParent();
	} while (Component != nullptr);

	UE_LOG(LogGLTFExporter, Log, TEXT("%s [%d] %s"), *Path, AttachedComponents.Num(), bTopLevel ? TEXT("*") : TEXT(""));

	for (auto& AttachedComponent : AttachedComponents)
	{
		AttachedComponent._DebugLog();
	}
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
				const USceneComponent* ParentComponent = RootComponent->GetAttachParent();
				if (ParentComponent == nullptr || (bSelectedOnly && !ParentComponent->GetOwner()->IsSelected()))
				{
					TopLevelComponents.Add(FGLTFNodeBuilder(RootComponent, bSelectedOnly, true));
				}
			}
		}
	}
}

FGLTFJsonSceneIndex FGLTFSceneBuilder::AddScene(FGLTFContainerBuilder& Container)
{
	FGLTFJsonScene Scene;

	// TODO:

	return Container.AddScene(Scene);
}

void FGLTFSceneBuilder::_DebugLog()
{
	UE_LOG(LogGLTFExporter, Log, TEXT("Level %s:"), *Name);

	for (auto& TopLevelComponent : TopLevelComponents)
	{
		TopLevelComponent._DebugLog();
	}
}
