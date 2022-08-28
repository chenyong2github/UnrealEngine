// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonRoot.h"
#include "Builders/GLTFTaskBuilder.h"

class FGLTFJsonBuilder : public FGLTFTaskBuilder
{
public:

	FGLTFJsonScene*& DefaultScene;

	FGLTFJsonBuilder(const FString& FilePath, const UGLTFExportOptions* ExportOptions);

	void WriteJson(FArchive& Archive);

	TSet<EGLTFJsonExtension> GetCustomExtensionsUsed() const;

	void AddExtension(EGLTFJsonExtension Extension, bool bIsRequired = false);

	FGLTFJsonAccessor* AddAccessor();
	FGLTFJsonAnimation* AddAnimation();
	FGLTFJsonBuffer* AddBuffer();
	FGLTFJsonBufferView* AddBufferView();
	FGLTFJsonCamera* AddCamera();
	FGLTFJsonImage* AddImage();
	FGLTFJsonMaterial* AddMaterial();
	FGLTFJsonMesh* AddMesh();
	FGLTFJsonNode* AddNode();
	FGLTFJsonSampler* AddSampler();
	FGLTFJsonScene* AddScene();
	FGLTFJsonSkin* AddSkin();
	FGLTFJsonTexture* AddTexture();
	FGLTFJsonBackdrop* AddBackdrop();
	FGLTFJsonHotspot* AddHotspot();
	FGLTFJsonLight* AddLight();
	FGLTFJsonLightMap* AddLightMap();
	FGLTFJsonSkySphere* AddSkySphere();
	FGLTFJsonEpicLevelVariantSets* AddEpicLevelVariantSets();
	FGLTFJsonKhrMaterialVariant* AddKhrMaterialVariant();

	FGLTFJsonNode* AddChildNode(FGLTFJsonNode* ParentNode, FGLTFJsonNode* ChildNode = nullptr);
	FGLTFJsonNode* AddChildComponentNode(FGLTFJsonNode* ParentNodeParentNode, FGLTFJsonNode* ChildNode = nullptr);

	const FGLTFJsonRoot& GetRoot() const;
	FGLTFJsonAccessor& GetAccessor(FGLTFJsonAccessor* AccessorIndex);
	FGLTFJsonAnimation& GetAnimation(FGLTFJsonAnimation* AnimationIndex);
	FGLTFJsonBuffer& GetBuffer(FGLTFJsonBuffer* BufferIndex);
	FGLTFJsonBufferView& GetBufferView(FGLTFJsonBufferView* BufferViewIndex);
	FGLTFJsonCamera& GetCamera(FGLTFJsonCamera* CameraIndex);
	FGLTFJsonImage& GetImage(FGLTFJsonImage* ImageIndex);
	FGLTFJsonMaterial& GetMaterial(FGLTFJsonMaterial* MaterialIndex);
	FGLTFJsonMesh& GetMesh(FGLTFJsonMesh* MeshIndex);
	FGLTFJsonNode& GetNode(FGLTFJsonNode* NodeIndex);
	FGLTFJsonSampler& GetSampler(FGLTFJsonSampler* SamplerIndex);
	FGLTFJsonScene& GetScene(FGLTFJsonScene* SceneIndex);
	FGLTFJsonSkin& GetSkin(FGLTFJsonSkin* SkinIndex);
	FGLTFJsonTexture& GetTexture(FGLTFJsonTexture* TextureIndex);
	FGLTFJsonBackdrop& GetBackdrop(FGLTFJsonBackdrop* BackdropIndex);
	FGLTFJsonHotspot& GetHotspot(FGLTFJsonHotspot* HotspotIndex);
	FGLTFJsonLight& GetLight(FGLTFJsonLight* LightIndex);
	FGLTFJsonLightMap& GetLightMap(FGLTFJsonLightMap* LightMapIndex);
	FGLTFJsonSkySphere& GetSkySphere(FGLTFJsonSkySphere* SkySphereIndex);
	FGLTFJsonEpicLevelVariantSets& GetEpicLevelVariantSets(FGLTFJsonEpicLevelVariantSets* EpicLevelVariantSetsIndex);
	FGLTFJsonKhrMaterialVariant& GetKhrMaterialVariant(FGLTFJsonKhrMaterialVariant* KhrMaterialVariantIndex);

	FGLTFJsonNode* GetComponentNode(FGLTFJsonNode* Node);

private:

	FString GetGeneratorString() const;

	static bool IsCustomExtension(EGLTFJsonExtension Extension);

	FGLTFJsonRoot JsonRoot;
};
