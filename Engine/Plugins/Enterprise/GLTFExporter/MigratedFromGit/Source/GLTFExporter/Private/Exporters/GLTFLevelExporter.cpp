// Copyright Epic Games, Inc. All Rights Reserved.

#include "Exporters/GLTFLevelExporter.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Engine/World.h"

UGLTFLevelExporter::UGLTFLevelExporter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UWorld::StaticClass();
}

bool UGLTFLevelExporter::Add(FGLTFContainerBuilder& Builder, const UObject* Object)
{
	const UWorld* World = CastChecked<UWorld>(Object);
	const FGLTFJsonSceneIndex SceneIndex = Builder.GetOrAddScene(World, bSelectedOnly);

	Builder.DefaultScene = SceneIndex;
	return true;
}
