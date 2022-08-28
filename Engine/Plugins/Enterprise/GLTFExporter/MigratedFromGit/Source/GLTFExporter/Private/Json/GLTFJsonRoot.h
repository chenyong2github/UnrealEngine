// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Json/GLTFJsonAsset.h"
#include "Json/GLTFJsonExtensions.h"
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
#include "Policies/CondensedJsonPrintPolicy.h"

struct FGLTFJsonRoot
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

	template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
	void WriteObject(TJsonWriter<CharType, PrintPolicy>& JsonWriter)
	{
		JsonWriter.WriteObjectStart();

		JsonWriter.WriteIdentifierPrefix(TEXT("asset"));
		Asset.WriteObject(JsonWriter, Extensions);

		if (DefaultScene != INDEX_NONE)
		{
			JsonWriter.WriteValue(TEXT("scene"), DefaultScene);
		}

		FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("accessors"), Accessors, Extensions);
		FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("animations"), Animations, Extensions);
		FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("buffers"), Buffers, Extensions);
		FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("bufferViews"), BufferViews, Extensions);
		FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("cameras"), Cameras, Extensions);
		FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("images"), Images, Extensions);
		FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("materials"), Materials, Extensions);
		FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("meshes"), Meshes, Extensions);
		FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("nodes"), Nodes, Extensions);
		FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("samplers"), Samplers, Extensions);
		FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("scenes"), Scenes, Extensions);
		FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("skins"), Skins, Extensions);
		FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("textures"), Textures, Extensions);

		if (Backdrops.Num() > 0 || Hotspots.Num() > 0 || Lights.Num() > 0 || LightMaps.Num() > 0 || SkySpheres.Num() > 0 || LevelVariantSets.Num() > 0)
		{
			JsonWriter.WriteObjectStart(TEXT("extensions"));

			if (Backdrops.Num() > 0)
			{
				const EGLTFJsonExtension Extension = EGLTFJsonExtension::EPIC_HDRIBackdrops;
				Extensions.Used.Add(Extension);

				JsonWriter.WriteObjectStart(FGLTFJsonUtility::ToString(Extension));
				FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("backdrops"), Backdrops, Extensions);
				JsonWriter.WriteObjectEnd();
			}

			if (Hotspots.Num() > 0)
			{
				const EGLTFJsonExtension Extension = EGLTFJsonExtension::EPIC_AnimationHotspots;
				Extensions.Used.Add(Extension);

				JsonWriter.WriteObjectStart(FGLTFJsonUtility::ToString(Extension));
				FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("hotspots"), Hotspots, Extensions);
				JsonWriter.WriteObjectEnd();
			}

			if (LevelVariantSets.Num() > 0)
			{
				const EGLTFJsonExtension Extension = EGLTFJsonExtension::EPIC_LevelVariantSets;
				Extensions.Used.Add(Extension);

				JsonWriter.WriteObjectStart(FGLTFJsonUtility::ToString(Extension));
				FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("levelVariantSets"), LevelVariantSets, Extensions);
				JsonWriter.WriteObjectEnd();
			}

			if (Lights.Num() > 0)
			{
				const EGLTFJsonExtension Extension = EGLTFJsonExtension::KHR_LightsPunctual;
				Extensions.Used.Add(Extension);

				JsonWriter.WriteObjectStart(FGLTFJsonUtility::ToString(Extension));
				FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("lights"), Lights, Extensions);
				JsonWriter.WriteObjectEnd();
			}

			if (LightMaps.Num() > 0)
			{
				const EGLTFJsonExtension Extension = EGLTFJsonExtension::EPIC_LightmapTextures;
				Extensions.Used.Add(Extension);

				JsonWriter.WriteObjectStart(FGLTFJsonUtility::ToString(Extension));
				FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("lightmaps"), LightMaps, Extensions);
				JsonWriter.WriteObjectEnd();
			}

			if (SkySpheres.Num() > 0)
			{
				const EGLTFJsonExtension Extension = EGLTFJsonExtension::EPIC_SkySpheres;
				Extensions.Used.Add(Extension);

				JsonWriter.WriteObjectStart(FGLTFJsonUtility::ToString(Extension));
				FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("skySpheres"), SkySpheres, Extensions);
				JsonWriter.WriteObjectEnd();
			}

			JsonWriter.WriteObjectEnd();
		}

		FGLTFJsonUtility::WriteStringArray(JsonWriter, TEXT("extensionsUsed"), Extensions.Used);
		FGLTFJsonUtility::WriteStringArray(JsonWriter, TEXT("extensionsRequired"), Extensions.Required);

		JsonWriter.WriteObjectEnd();
	}

	template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
	void ToJson(FArchive* const Archive)
	{
		TSharedRef<TJsonWriter<CharType, PrintPolicy>> JsonWriter = TJsonWriterFactory<CharType, PrintPolicy>::Create(Archive);
		WriteObject(*JsonWriter);
		JsonWriter->Close();
	}

	void ToJson(FArchive* const Archive, bool bCondensedPrint)
	{
		if (bCondensedPrint)
		{
			ToJson<UTF8CHAR, TCondensedJsonPrintPolicy<UTF8CHAR>>(Archive);
		}
		else
		{
			ToJson<UTF8CHAR, TPrettyJsonPrintPolicy<UTF8CHAR>>(Archive);
		}
	}
};
