// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFJsonObject.h"
#include "GLTFJsonAccessor.h"
#include "GLTFJsonBufferView.h"
#include "GLTFJsonBuffer.h"
#include "GLTFJsonMesh.h"
#include "GLTFJsonScene.h"
#include "GLTFJsonNode.h"

#include "Runtime/Launch/Resources/Version.h"


struct GLTFEXPORTER_API FGLTFJsonAsset : FGLTFJsonObject
{
	FString Version;
	FString Generator;
	FString Copyright;

	FGLTFJsonAsset()
		: Version(TEXT("2.0"))
		, Generator(TEXT(EPIC_PRODUCT_NAME) TEXT(" ") ENGINE_VERSION_STRING)
	{
	}
};

struct GLTFEXPORTER_API FGLTFJsonRoot
{
	FGLTFJsonAsset Asset;
	FGLTFJsonIndex DefaultScene;

	TArray<FGLTFJsonAccessor>   Accessors;
	TArray<FGLTFJsonBuffer>     Buffers;
	TArray<FGLTFJsonBufferView> BufferViews;
	TArray<FGLTFJsonMesh>       Meshes;
	TArray<FGLTFJsonNode>       Nodes;
	TArray<FGLTFJsonScene>      Scenes;

	FGLTFJsonRoot()
		: DefaultScene(INDEX_NONE)
	{
	}
};
