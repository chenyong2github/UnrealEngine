// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonRoot.h"
#include "Builders/GLTFBuilder.h"

class FGLTFJsonBuilder : public FGLTFBuilder
{
public:

	FGLTFJsonSceneIndex& DefaultScene;

	FGLTFJsonBuilder();

	FGLTFJsonAccessorIndex AddAccessor(const FGLTFJsonAccessor& JsonAccessor);
	FGLTFJsonBufferIndex AddBuffer(const FGLTFJsonBuffer& JsonBuffer);
	FGLTFJsonBufferViewIndex AddBufferView(const FGLTFJsonBufferView& JsonBufferView);
	FGLTFJsonImageIndex AddImage(const FGLTFJsonImage& JsonImage);
	FGLTFJsonMaterialIndex AddMaterial(const FGLTFJsonMaterial& JsonMaterial);
	FGLTFJsonMeshIndex AddMesh(const FGLTFJsonMesh& JsonMesh);
	FGLTFJsonNodeIndex AddNode(const FGLTFJsonNode& JsonNode);
	FGLTFJsonSamplerIndex AddSampler(const FGLTFJsonSampler& JsonSampler);
	FGLTFJsonSceneIndex AddScene(const FGLTFJsonScene& JsonScene);
	FGLTFJsonTextureIndex AddTexture(const FGLTFJsonTexture& JsonTexture);

	virtual bool Serialize(FArchive& Archive, const FString& FilePath);

protected:

	FGLTFJsonRoot JsonRoot;
};
