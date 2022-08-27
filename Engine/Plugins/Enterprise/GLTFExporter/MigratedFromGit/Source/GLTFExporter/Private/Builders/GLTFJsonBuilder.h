// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonRoot.h"
#include "Builders/GLTFTaskBuilder.h"

class FGLTFJsonBuilder : public FGLTFTaskBuilder
{
public:

	FGLTFJsonSceneIndex& DefaultScene;

	FGLTFJsonBuilder(const FString& FilePath, const UGLTFExportOptions* ExportOptions);

	void WriteJson(FArchive& Archive);

	TSet<EGLTFJsonExtension> GetCustomExtensionsUsed() const;

	void AddExtension(EGLTFJsonExtension Extension, bool bIsRequired = false);

	FGLTFJsonAccessorIndex AddAccessor(const FGLTFJsonAccessor& JsonAccessor = {});
	FGLTFJsonAnimationIndex AddAnimation(const FGLTFJsonAnimation& JsonAnimation = {});
	FGLTFJsonBufferIndex AddBuffer(const FGLTFJsonBuffer& JsonBuffer = {});
	FGLTFJsonBufferViewIndex AddBufferView(const FGLTFJsonBufferView& JsonBufferView = {});
	FGLTFJsonCameraIndex AddCamera(const FGLTFJsonCamera& JsonCamera = {});
	FGLTFJsonImageIndex AddImage(const FGLTFJsonImage& JsonImage = {});
	FGLTFJsonMaterialIndex AddMaterial(const FGLTFJsonMaterial& JsonMaterial = {});
	FGLTFJsonMeshIndex AddMesh(const FGLTFJsonMesh& JsonMesh = {});
	FGLTFJsonNodeIndex AddNode(const FGLTFJsonNode& JsonNode = {});
	FGLTFJsonSamplerIndex AddSampler(const FGLTFJsonSampler& JsonSampler = {});
	FGLTFJsonSceneIndex AddScene(const FGLTFJsonScene& JsonScene = {});
	FGLTFJsonSkinIndex AddSkin(const FGLTFJsonSkin& JsonSkin = {});
	FGLTFJsonTextureIndex AddTexture(const FGLTFJsonTexture& JsonTexture = {});
	FGLTFJsonBackdropIndex AddBackdrop(const FGLTFJsonBackdrop& JsonBackdrop = {});
	FGLTFJsonHotspotIndex AddHotspot(const FGLTFJsonHotspot& JsonHotspot = {});
	FGLTFJsonLightIndex AddLight(const FGLTFJsonLight& JsonLight = {});
	FGLTFJsonLightMapIndex AddLightMap(const FGLTFJsonLightMap& JsonLightMap = {});
	FGLTFJsonSkySphereIndex AddSkySphere(const FGLTFJsonSkySphere& JsonSkySphere = {});
	FGLTFJsonEpicLevelVariantSetsIndex AddEpicLevelVariantSets(const FGLTFJsonEpicLevelVariantSets& JsonEpicLevelVariantSets = {});
	FGLTFJsonKhrMaterialVariantIndex AddKhrMaterialVariant(const FGLTFJsonKhrMaterialVariant& JsonKhrMaterialVariant = {});

	FGLTFJsonNodeIndex AddChildNode(FGLTFJsonNodeIndex ParentNodeIndex, const FGLTFJsonNode& JsonNode = {});
	FGLTFJsonNodeIndex AddChildComponentNode(FGLTFJsonNodeIndex ParentNodeIndex, const FGLTFJsonNode& JsonNode = {});

	const FGLTFJsonRoot& GetRoot() const;
	FGLTFJsonAccessor& GetAccessor(FGLTFJsonAccessorIndex AccessorIndex) const;
	FGLTFJsonAnimation& GetAnimation(FGLTFJsonAnimationIndex AnimationIndex) const;
	FGLTFJsonBuffer& GetBuffer(FGLTFJsonBufferIndex BufferIndex) const;
	FGLTFJsonBufferView& GetBufferView(FGLTFJsonBufferViewIndex BufferViewIndex) const;
	FGLTFJsonCamera& GetCamera(FGLTFJsonCameraIndex CameraIndex) const;
	FGLTFJsonImage& GetImage(FGLTFJsonImageIndex ImageIndex) const;
	FGLTFJsonMaterial& GetMaterial(FGLTFJsonMaterialIndex MaterialIndex) const;
	FGLTFJsonMesh& GetMesh(FGLTFJsonMeshIndex MeshIndex) const;
	FGLTFJsonNode& GetNode(FGLTFJsonNodeIndex NodeIndex) const;
	FGLTFJsonSampler& GetSampler(FGLTFJsonSamplerIndex SamplerIndex) const;
	FGLTFJsonScene& GetScene(FGLTFJsonSceneIndex SceneIndex) const;
	FGLTFJsonSkin& GetSkin(FGLTFJsonSkinIndex SkinIndex) const;
	FGLTFJsonTexture& GetTexture(FGLTFJsonTextureIndex TextureIndex) const;
	FGLTFJsonBackdrop& GetBackdrop(FGLTFJsonBackdropIndex BackdropIndex) const;
	FGLTFJsonHotspot& GetHotspot(FGLTFJsonHotspotIndex HotspotIndex) const;
	FGLTFJsonLight& GetLight(FGLTFJsonLightIndex LightIndex) const;
	FGLTFJsonLightMap& GetLightMap(FGLTFJsonLightMapIndex LightMapIndex) const;
	FGLTFJsonSkySphere& GetSkySphere(FGLTFJsonSkySphereIndex SkySphereIndex) const;
	FGLTFJsonEpicLevelVariantSets& GetEpicLevelVariantSets(FGLTFJsonEpicLevelVariantSetsIndex EpicLevelVariantSetsIndex) const;
	FGLTFJsonKhrMaterialVariant& GetKhrMaterialVariant(FGLTFJsonKhrMaterialVariantIndex KhrMaterialVariantIndex) const;

	FGLTFJsonNodeIndex GetComponentNodeIndex(FGLTFJsonNodeIndex NodeIndex) const;

private:

	FString GetGeneratorString() const;

	static bool IsCustomExtension(EGLTFJsonExtension Extension);

	FGLTFJsonRoot JsonRoot;
};
