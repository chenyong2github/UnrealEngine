// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonObject.h"
#include "Json/GLTFJsonIndex.h"
#include "Json/GLTFJsonAsset.h"
#include "Json/GLTFJsonAccessor.h"
#include "Json/GLTFJsonAnimation.h"
#include "Json/GLTFJsonBuffer.h"
#include "Json/GLTFJsonBufferView.h"
#include "Json/GLTFJsonCamera.h"
#include "Json/GLTFJsonImage.h"
#include "Json/GLTFJsonMaterial.h"
#include "Json/GLTFJsonMesh.h"
#include "Json/GLTFJsonNode.h"
#include "Json/GLTFJsonSampler.h"
#include "Json/GLTFJsonScene.h"
#include "Json/GLTFJsonSkin.h"
#include "Json/GLTFJsonTexture.h"
#include "Json/GLTFJsonBackdrop.h"
#include "Json/GLTFJsonHotspot.h"
#include "Json/GLTFJsonLight.h"
#include "Json/GLTFJsonLightMap.h"
#include "Json/GLTFJsonSkySphere.h"
#include "Json/GLTFJsonEpicLevelVariantSets.h"
#include "Json/GLTFJsonKhrMaterialVariant.h"
#include "Json/GLTFJsonWriter.h"

struct GLTFEXPORTER_API FGLTFJsonRoot : IGLTFJsonObject
{
	FGLTFJsonAsset Asset;

	FGLTFJsonExtensions Extensions;

	FGLTFJsonSceneIndex DefaultScene;

	TArray<TUniquePtr<FGLTFJsonAccessor>>   Accessors;
	TArray<TUniquePtr<FGLTFJsonAnimation>>  Animations;
	TArray<TUniquePtr<FGLTFJsonBuffer>>     Buffers;
	TArray<TUniquePtr<FGLTFJsonBufferView>> BufferViews;
	TArray<TUniquePtr<FGLTFJsonCamera>>     Cameras;
	TArray<TUniquePtr<FGLTFJsonMaterial>>   Materials;
	TArray<TUniquePtr<FGLTFJsonMesh>>       Meshes;
	TArray<TUniquePtr<FGLTFJsonNode>>       Nodes;
	TArray<TUniquePtr<FGLTFJsonImage>>      Images;
	TArray<TUniquePtr<FGLTFJsonSampler>>    Samplers;
	TArray<TUniquePtr<FGLTFJsonScene>>      Scenes;
	TArray<TUniquePtr<FGLTFJsonSkin>>       Skins;
	TArray<TUniquePtr<FGLTFJsonTexture>>    Textures;
	TArray<TUniquePtr<FGLTFJsonBackdrop>>   Backdrops;
	TArray<TUniquePtr<FGLTFJsonHotspot>>    Hotspots;
	TArray<TUniquePtr<FGLTFJsonLight>>      Lights;
	TArray<TUniquePtr<FGLTFJsonLightMap>>   LightMaps;
	TArray<TUniquePtr<FGLTFJsonSkySphere>>  SkySpheres;
	TArray<TUniquePtr<FGLTFJsonEpicLevelVariantSets>>  EpicLevelVariantSets;
	TArray<TUniquePtr<FGLTFJsonKhrMaterialVariant>>    KhrMaterialVariants;

	FGLTFJsonRoot() = default;
	FGLTFJsonRoot(FGLTFJsonRoot&&) = default;
	FGLTFJsonRoot &operator=(FGLTFJsonRoot&&) = default;

	FGLTFJsonRoot(const FGLTFJsonRoot&) = delete;
	FGLTFJsonRoot &operator=(const FGLTFJsonRoot&) = delete;

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;

	void WriteJson(FArchive& Archive, bool bPrettyJson, float DefaultTolerance);
};
