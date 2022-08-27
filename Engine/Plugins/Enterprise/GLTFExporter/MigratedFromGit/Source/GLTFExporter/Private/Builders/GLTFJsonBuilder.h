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

	const FGLTFJsonRoot& GetRoot() const;

protected:

	TSet<EGLTFJsonExtension> GetCustomExtensionsUsed() const;

private:

	FString GetGeneratorString() const;

	static bool IsCustomExtension(EGLTFJsonExtension Extension);

	FGLTFJsonRoot JsonRoot;
};
