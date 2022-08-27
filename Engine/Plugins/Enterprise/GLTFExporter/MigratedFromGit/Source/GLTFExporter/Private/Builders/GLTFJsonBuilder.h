// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonRoot.h"
#include "Builders/GLTFBuilder.h"

class FGLTFJsonBuilder : public FGLTFBuilder
{
public:

	FGLTFJsonSceneIndex& DefaultScene;

	FGLTFJsonBuilder(const UGLTFExportOptions* ExportOptions);

	FGLTFJsonAccessorIndex AddAccessor(const FGLTFJsonAccessor& JsonAccessor = {});
	FGLTFJsonBufferIndex AddBuffer(const FGLTFJsonBuffer& JsonBuffer = {});
	FGLTFJsonBufferViewIndex AddBufferView(const FGLTFJsonBufferView& JsonBufferView = {});
	FGLTFJsonImageIndex AddImage(const FGLTFJsonImage& JsonImage = {});
	FGLTFJsonMaterialIndex AddMaterial(const FGLTFJsonMaterial& JsonMaterial = {});
	FGLTFJsonMeshIndex AddMesh(const FGLTFJsonMesh& JsonMesh = {});
	FGLTFJsonNodeIndex AddNode(const FGLTFJsonNode& JsonNode = {});
	FGLTFJsonSamplerIndex AddSampler(const FGLTFJsonSampler& JsonSampler = {});
	FGLTFJsonSceneIndex AddScene(const FGLTFJsonScene& JsonScene = {});
	FGLTFJsonTextureIndex AddTexture(const FGLTFJsonTexture& JsonTexture = {});
	FGLTFJsonLightMapIndex AddLightMap(const FGLTFJsonLightMap& JsonLightMap = {});
	FGLTFJsonLevelVariantSetsIndex AddLevelVariantSets(const FGLTFJsonLevelVariantSets& LevelVariantSets = {});

	FGLTFJsonNodeIndex AddChildNode(FGLTFJsonNodeIndex ParentNodeIndex, const FGLTFJsonNode& JsonNode = {});

	FGLTFJsonAccessor& GetAccessor(FGLTFJsonAccessorIndex AccessorIndex);
	FGLTFJsonBuffer& GetBuffer(FGLTFJsonBufferIndex BufferIndex);
	FGLTFJsonBufferView& GetBufferView(FGLTFJsonBufferViewIndex BufferViewIndex);
	FGLTFJsonImage& GetImage(FGLTFJsonImageIndex ImageIndex);
	FGLTFJsonMaterial& GetMaterial(FGLTFJsonMaterialIndex MaterialIndex);
	FGLTFJsonMesh& GetMesh(FGLTFJsonMeshIndex MeshIndex);
	FGLTFJsonNode& GetNode(FGLTFJsonNodeIndex NodeIndex);
	FGLTFJsonSampler& GetSampler(FGLTFJsonSamplerIndex SamplerIndex);
	FGLTFJsonScene& GetScene(FGLTFJsonSceneIndex SceneIndex);
	FGLTFJsonTexture& GetTexture(FGLTFJsonTextureIndex TextureIndex);
	FGLTFJsonLightMap& GetLightMap(FGLTFJsonLightMapIndex LightMapIndex);
	FGLTFJsonLevelVariantSets& GetLevelVariantSets(FGLTFJsonLevelVariantSetsIndex LevelVariantSetsIndex);

	virtual bool Serialize(FArchive& Archive, const FString& FilePath);

private:

	FGLTFJsonRoot JsonRoot;
};
