// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFJsonRoot.h"

struct GLTFEXPORTER_API FGLTFJsonBuilder
{
	FGLTFJsonRoot JsonRoot;

	FGLTFJsonBuilder();
	virtual ~FGLTFJsonBuilder();

	FGLTFJsonAccessorIndex AddAccessor(const FGLTFJsonAccessor& JsonAccessor);
	FGLTFJsonBufferIndex AddBuffer(const FGLTFJsonBuffer& JsonBuffer);
	FGLTFJsonBufferViewIndex AddBufferView(const FGLTFJsonBufferView& JsonBufferView);
	FGLTFJsonMeshIndex AddMesh(const FGLTFJsonMesh& JsonMesh);
	FGLTFJsonNodeIndex AddNode(const FGLTFJsonNode& JsonNode);
	FGLTFJsonSceneIndex AddScene(const FGLTFJsonScene& JsonScene);

	virtual void Serialize(FArchive& Archive);
};
