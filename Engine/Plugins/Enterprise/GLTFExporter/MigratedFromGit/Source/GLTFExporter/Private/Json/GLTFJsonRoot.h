// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
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
#include "Json/GLTFJsonVariation.h"
#include "Json/GLTFJsonLightMap.h"
#include "Json/GLTFJsonLight.h"
#include "Json/FGLTFJsonHotspot.h"
#include "Policies/CondensedJsonPrintPolicy.h"


struct FGLTFJsonAsset
{
	FString Version;
	FString Generator;
	FString Copyright;

	FGLTFJsonAsset();

	template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
	void WriteObject(TJsonWriter<CharType, PrintPolicy>& JsonWriter, FGLTFJsonExtensions& Extensions) const
	{
		JsonWriter.WriteObjectStart();

		JsonWriter.WriteValue(TEXT("version"), Version);

		if (!Generator.IsEmpty())
		{
			JsonWriter.WriteValue(TEXT("generator"), Generator);
		}

		if (!Copyright.IsEmpty())
		{
			JsonWriter.WriteValue(TEXT("copyright"), Copyright);
		}

		JsonWriter.WriteObjectEnd();
	}
};

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
	TArray<TUniquePtr<FGLTFJsonLightMap>>   LightMaps;
	TArray<TUniquePtr<FGLTFJsonLight>>      Lights;
	TArray<TUniquePtr<FGLTFJsonVariation>>  Variations;
	TArray<TUniquePtr<FGLTFJsonHotspot>>    Hotspots;

	template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
	void WriteObject(TJsonWriter<CharType, PrintPolicy>& JsonWriter) const
	{
		FGLTFJsonExtensions AllExtensions(Extensions);

		JsonWriter.WriteObjectStart();

		JsonWriter.WriteIdentifierPrefix(TEXT("asset"));
		Asset.WriteObject(JsonWriter, AllExtensions);

		if (DefaultScene != INDEX_NONE)
		{
			JsonWriter.WriteValue(TEXT("scene"), DefaultScene);
		}

		FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("accessors"), Accessors, AllExtensions);
		FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("animations"), Animations, AllExtensions);
		FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("buffers"), Buffers, AllExtensions);
		FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("bufferViews"), BufferViews, AllExtensions);
		FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("cameras"), Cameras, AllExtensions);
		FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("images"), Images, AllExtensions);
		FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("materials"), Materials, AllExtensions);
		FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("meshes"), Meshes, AllExtensions);
		FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("nodes"), Nodes, AllExtensions);
		FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("samplers"), Samplers, AllExtensions);
		FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("scenes"), Scenes, AllExtensions);
		FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("skins"), Skins, AllExtensions);
		FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("textures"), Textures, AllExtensions);

		if (Backdrops.Num() > 0 || Variations.Num() > 0 || LightMaps.Num() > 0 || Lights.Num() > 0 || Hotspots.Num() > 0 /* TODO: add more extension support */)
		{
			JsonWriter.WriteObjectStart(TEXT("extensions"));

			if (Backdrops.Num() > 0)
			{
				const EGLTFJsonExtension Extension = EGLTFJsonExtension::EPIC_HDRIBackdrops;
				AllExtensions.Used.Add(Extension);

				JsonWriter.WriteObjectStart(FGLTFJsonUtility::ToString(Extension));
				FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("backdrops"), Backdrops, AllExtensions);
				JsonWriter.WriteObjectEnd();
			}

			if (Variations.Num() > 0)
			{
				const EGLTFJsonExtension Extension = EGLTFJsonExtension::EPIC_LevelVariantSets;
				AllExtensions.Used.Add(Extension);

				JsonWriter.WriteObjectStart(FGLTFJsonUtility::ToString(Extension));
				FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("levelVariantSets"), Variations, AllExtensions);
				JsonWriter.WriteObjectEnd();
			}

			if (LightMaps.Num() > 0)
			{
				const EGLTFJsonExtension Extension = EGLTFJsonExtension::EPIC_LightmapTextures;
				AllExtensions.Used.Add(Extension);

				JsonWriter.WriteObjectStart(FGLTFJsonUtility::ToString(Extension));
				FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("lightmaps"), LightMaps, AllExtensions);
				JsonWriter.WriteObjectEnd();
			}

			if (Lights.Num() > 0)
			{
				const EGLTFJsonExtension Extension = EGLTFJsonExtension::KHR_LightsPunctual;
				AllExtensions.Used.Add(Extension);

				JsonWriter.WriteObjectStart(FGLTFJsonUtility::ToString(Extension));
				FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("lights"), Lights, AllExtensions);
				JsonWriter.WriteObjectEnd();
			}

			if (Hotspots.Num() > 0)
			{
				const EGLTFJsonExtension Extension = EGLTFJsonExtension::EPIC_InteractionHotspots;
				AllExtensions.Used.Add(Extension);

				JsonWriter.WriteObjectStart(FGLTFJsonUtility::ToString(Extension));
				FGLTFJsonUtility::WriteObjectPtrArray(JsonWriter, TEXT("interactions"), Hotspots, AllExtensions);
				JsonWriter.WriteObjectEnd();
			}

			JsonWriter.WriteObjectEnd();
		}

		FGLTFJsonUtility::WriteStringArray(JsonWriter, TEXT("extensionsUsed"), AllExtensions.Used);
		FGLTFJsonUtility::WriteStringArray(JsonWriter, TEXT("extensionsRequired"), AllExtensions.Required);

		JsonWriter.WriteObjectEnd();
	}

	template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
	void ToJson(FArchive* const Archive) const
	{
		TSharedRef<TJsonWriter<CharType, PrintPolicy>> JsonWriter = TJsonWriterFactory<CharType, PrintPolicy>::Create(Archive);
		WriteObject(*JsonWriter);
		JsonWriter->Close();
	}

	void ToJson(FArchive* const Archive, bool bPrettyPrint = true) const
	{
		if (bPrettyPrint)
		{
			ToJson<UTF8CHAR, TPrettyJsonPrintPolicy<UTF8CHAR>>(Archive);
		}
		else
		{
			ToJson<UTF8CHAR, TCondensedJsonPrintPolicy<UTF8CHAR>>(Archive);
		}
	}
};
