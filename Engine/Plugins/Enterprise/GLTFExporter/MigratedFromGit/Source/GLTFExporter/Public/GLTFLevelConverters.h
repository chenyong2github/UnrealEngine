// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFJsonIndex.h"
#include "Engine.h"

struct FGLTFContainerBuilder;

struct GLTFEXPORTER_API FGLTFSceneComponentConverter
{
	static FGLTFJsonNodeIndex Convert(FGLTFContainerBuilder& Container, const FString& Name, const USceneComponent* SceneComponent, bool bSelectedOnly, bool bRootNode = false);
};

struct GLTFEXPORTER_API FGLTFLevelConverter
{
	static FGLTFJsonSceneIndex Convert(FGLTFContainerBuilder& Container, const FString& Name, const ULevel* Level, bool bSelectedOnly);
};
