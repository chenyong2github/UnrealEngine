// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builders/GLTFJsonBuilder.h"

FGLTFJsonBuilder::FGLTFJsonBuilder()
	: DefaultScene(JsonRoot.DefaultScene)
{
}

FGLTFJsonBuilder::~FGLTFJsonBuilder()
{
}

FGLTFJsonAccessorIndex FGLTFJsonBuilder::AddAccessor(const FGLTFJsonAccessor& JsonAccessor)
{
	return FGLTFJsonAccessorIndex(JsonRoot.Accessors.Add(JsonAccessor));
}

FGLTFJsonBufferIndex FGLTFJsonBuilder::AddBuffer(const FGLTFJsonBuffer& JsonBuffer)
{
	return FGLTFJsonBufferIndex(JsonRoot.Buffers.Add(JsonBuffer));
}

FGLTFJsonBufferViewIndex FGLTFJsonBuilder::AddBufferView(const FGLTFJsonBufferView& JsonBufferView)
{
	return FGLTFJsonBufferViewIndex(JsonRoot.BufferViews.Add(JsonBufferView));
}

FGLTFJsonImageIndex FGLTFJsonBuilder::AddImage(const FGLTFJsonImage& JsonImage)
{
	return FGLTFJsonImageIndex(JsonRoot.Images.Add(JsonImage));
}

FGLTFJsonMaterialIndex FGLTFJsonBuilder::AddMaterial(const FGLTFJsonMaterial& JsonMaterial)
{
	return FGLTFJsonMaterialIndex(JsonRoot.Materials.Add(JsonMaterial));
}

FGLTFJsonMeshIndex FGLTFJsonBuilder::AddMesh(const FGLTFJsonMesh& JsonMesh)
{
	return FGLTFJsonMeshIndex(JsonRoot.Meshes.Add(JsonMesh));
}

FGLTFJsonNodeIndex FGLTFJsonBuilder::AddNode(const FGLTFJsonNode& JsonNode)
{
	return FGLTFJsonNodeIndex(JsonRoot.Nodes.Add(JsonNode));
}

FGLTFJsonSamplerIndex FGLTFJsonBuilder::AddSampler(const FGLTFJsonSampler& JsonSampler)
{
	return FGLTFJsonSamplerIndex(JsonRoot.Samplers.Add(JsonSampler));
}

FGLTFJsonSceneIndex FGLTFJsonBuilder::AddScene(const FGLTFJsonScene& JsonScene)
{
	return FGLTFJsonSceneIndex(JsonRoot.Scenes.Add(JsonScene));
}

FGLTFJsonTextureIndex FGLTFJsonBuilder::AddTexture(const FGLTFJsonTexture& JsonTexture)
{
	return FGLTFJsonTextureIndex(JsonRoot.Textures.Add(JsonTexture));
}

bool FGLTFJsonBuilder::Serialize(FArchive& Archive, const FString& FilePath)
{
	JsonRoot.Serialize(&Archive, true);
	return true;
}
