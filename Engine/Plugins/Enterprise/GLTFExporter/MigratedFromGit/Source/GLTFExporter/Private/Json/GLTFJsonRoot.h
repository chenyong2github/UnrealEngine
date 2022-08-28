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
#include "Json/GLTFJsonLevelVariantSets.h"
#include "Json/GLTFJsonWriter.h"

struct FGLTFJsonRoot : IGLTFJsonObject
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
	TArray<TUniquePtr<FGLTFJsonLevelVariantSets>>  LevelVariantSets;

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		Writer.Write(TEXT("asset"), Asset);

		if (DefaultScene != INDEX_NONE)
		{
			Writer.Write(TEXT("scene"), DefaultScene);
		}

		if (Accessors.Num() > 0) Writer.Write(TEXT("accessors"), Accessors);
		if (Animations.Num() > 0) Writer.Write(TEXT("animations"), Animations);
		if (Buffers.Num() > 0) Writer.Write(TEXT("buffers"), Buffers);
		if (BufferViews.Num() > 0) Writer.Write(TEXT("bufferViews"), BufferViews);
		if (Cameras.Num() > 0) Writer.Write(TEXT("cameras"), Cameras);
		if (Images.Num() > 0) Writer.Write(TEXT("images"), Images);
		if (Materials.Num() > 0) Writer.Write(TEXT("materials"), Materials);
		if (Meshes.Num() > 0) Writer.Write(TEXT("meshes"), Meshes);
		if (Nodes.Num() > 0) Writer.Write(TEXT("nodes"), Nodes);
		if (Samplers.Num() > 0) Writer.Write(TEXT("samplers"), Samplers);
		if (Scenes.Num() > 0) Writer.Write(TEXT("scenes"), Scenes);
		if (Skins.Num() > 0) Writer.Write(TEXT("skins"), Skins);
		if (Textures.Num() > 0) Writer.Write(TEXT("textures"), Textures);

		if (Backdrops.Num() > 0 || Hotspots.Num() > 0 || Lights.Num() > 0 || LightMaps.Num() > 0 || SkySpheres.Num() > 0 || LevelVariantSets.Num() > 0)
		{
			Writer.StartExtensions();

			if (Backdrops.Num() > 0)
			{
				Writer.StartExtension(EGLTFJsonExtension::EPIC_HDRIBackdrops);
				Writer.Write(TEXT("backdrops"), Backdrops);
				Writer.EndExtension();
			}

			if (Hotspots.Num() > 0)
			{
				Writer.StartExtension(EGLTFJsonExtension::EPIC_AnimationHotspots);
				Writer.Write(TEXT("hotspots"), Hotspots);
				Writer.EndExtension();
			}

			if (LevelVariantSets.Num() > 0)
			{
				Writer.StartExtension(EGLTFJsonExtension::EPIC_LevelVariantSets);
				Writer.Write(TEXT("levelVariantSets"), LevelVariantSets);
				Writer.EndExtension();
			}

			if (Lights.Num() > 0)
			{
				Writer.StartExtension(EGLTFJsonExtension::KHR_LightsPunctual);
				Writer.Write(TEXT("lights"), Lights);
				Writer.EndExtension();
			}

			if (LightMaps.Num() > 0)
			{
				Writer.StartExtension(EGLTFJsonExtension::EPIC_LightmapTextures);
				Writer.Write(TEXT("lightmaps"), LightMaps);
				Writer.EndExtension();
			}

			if (SkySpheres.Num() > 0)
			{
				Writer.StartExtension(EGLTFJsonExtension::EPIC_SkySpheres);
				Writer.Write(TEXT("skySpheres"), SkySpheres);
				Writer.EndExtension();
			}

			Writer.EndExtensions();
		}

		if (Extensions.Used.Num() > 0)
		{
			Writer.Write(TEXT("extensionsUsed"), Extensions.Used);
		}

		if (Extensions.Required.Num() > 0)
		{
			Writer.Write(TEXT("extensionsRequired"), Extensions.Required);
		}
	}

	void WriteJson(FArchive& Archive, bool bPrettyJson)
	{
		TSharedRef<IGLTFJsonWriter> Writer = IGLTFJsonWriter::Create(Archive, bPrettyJson, Extensions);
		Writer->Write(this);
		Writer->Close();
	}
};
