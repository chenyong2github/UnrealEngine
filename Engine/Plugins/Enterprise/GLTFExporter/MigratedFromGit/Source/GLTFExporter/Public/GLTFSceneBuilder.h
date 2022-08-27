// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFJsonIndex.h"
#include "GLTFContainerBuilder.h"
#include "Engine.h"

struct GLTFEXPORTER_API FGLTFNodeBuilder
{
	FString Name;

	const USceneComponent* SceneComponent;
	const AActor* ComponentOwner;

	bool bRootNode;

	TArray<FGLTFNodeBuilder> AttachedComponents;

	FGLTFNodeBuilder(const USceneComponent* SceneComponent, const AActor* ComponentOwner, bool bSelectedOnly, bool bRootNode = false);

	FGLTFJsonNodeIndex AddNode(FGLTFContainerBuilder& Container) const;
};

struct GLTFEXPORTER_API FGLTFSceneBuilder
{
	FString Name;

	TArray<FGLTFNodeBuilder> RootNodes;

	FGLTFSceneBuilder(const UWorld* World, bool bSelectedOnly);

	FGLTFJsonSceneIndex AddScene(FGLTFContainerBuilder& Container) const;
};
