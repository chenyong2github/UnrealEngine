// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Json/GLTFJsonExtensions.h"
#include "Json/GLTFJsonAccessor.h"
#include "Json/GLTFJsonBuffer.h"
#include "Json/GLTFJsonBufferView.h"
#include "Json/GLTFJsonCamera.h"
#include "Json/GLTFJsonImage.h"
#include "Json/GLTFJsonMaterial.h"
#include "Json/GLTFJsonMesh.h"
#include "Json/GLTFJsonNode.h"
#include "Json/GLTFJsonSampler.h"
#include "Json/GLTFJsonScene.h"
#include "Json/GLTFJsonTexture.h"
#include "Json/GLTFJsonBackdrop.h"
#include "Json/GLTFJsonLevelVariantSets.h"
#include "Json/GLTFJsonLightMap.h"
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

	TArray<FGLTFJsonAccessor>   Accessors;
	TArray<FGLTFJsonBuffer>     Buffers;
	TArray<FGLTFJsonBufferView> BufferViews;
	TArray<FGLTFJsonCamera>     Cameras;
	TArray<FGLTFJsonMaterial>   Materials;
	TArray<FGLTFJsonMesh>       Meshes;
	TArray<FGLTFJsonNode>       Nodes;
	TArray<FGLTFJsonImage>      Images;
	TArray<FGLTFJsonSampler>    Samplers;
	TArray<FGLTFJsonScene>      Scenes;
	TArray<FGLTFJsonTexture>    Textures;
	TArray<FGLTFJsonBackdrop>   Backdrops;
	TArray<FGLTFJsonLightMap>   LightMaps;
	TArray<FGLTFJsonLevelVariantSets> LevelVariantSets;

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

		FGLTFJsonUtility::WriteObjectArray(JsonWriter, TEXT("accessors"), Accessors, AllExtensions);
		FGLTFJsonUtility::WriteObjectArray(JsonWriter, TEXT("buffers"), Buffers, AllExtensions);
		FGLTFJsonUtility::WriteObjectArray(JsonWriter, TEXT("bufferViews"), BufferViews, AllExtensions);
		FGLTFJsonUtility::WriteObjectArray(JsonWriter, TEXT("cameras"), Cameras, AllExtensions);
		FGLTFJsonUtility::WriteObjectArray(JsonWriter, TEXT("images"), Images, AllExtensions);
		FGLTFJsonUtility::WriteObjectArray(JsonWriter, TEXT("materials"), Materials, AllExtensions);
		FGLTFJsonUtility::WriteObjectArray(JsonWriter, TEXT("meshes"), Meshes, AllExtensions);
		FGLTFJsonUtility::WriteObjectArray(JsonWriter, TEXT("nodes"), Nodes, AllExtensions);
		FGLTFJsonUtility::WriteObjectArray(JsonWriter, TEXT("samplers"), Samplers, AllExtensions);
		FGLTFJsonUtility::WriteObjectArray(JsonWriter, TEXT("scenes"), Scenes, AllExtensions);
		FGLTFJsonUtility::WriteObjectArray(JsonWriter, TEXT("textures"), Textures, AllExtensions);

		if (Backdrops.Num() > 0 || LevelVariantSets.Num() > 0 || LightMaps.Num() > 0 /* TODO: add more extension support */)
		{
			JsonWriter.WriteObjectStart(TEXT("extensions"));

			if (Backdrops.Num() > 0)
			{
				const EGLTFJsonExtension Extension = EGLTFJsonExtension::EPIC_HDRIBackdrops;
				AllExtensions.Used.Add(Extension);

				JsonWriter.WriteObjectStart(FGLTFJsonUtility::ToString(Extension));
				FGLTFJsonUtility::WriteObjectArray(JsonWriter, TEXT("backdrops"), Backdrops, AllExtensions);
				JsonWriter.WriteObjectEnd();
			}

			if (LevelVariantSets.Num() > 0)
			{
				const EGLTFJsonExtension Extension = EGLTFJsonExtension::EPIC_LevelVariantSets;
				AllExtensions.Used.Add(Extension);

				JsonWriter.WriteObjectStart(FGLTFJsonUtility::ToString(Extension));
				FGLTFJsonUtility::WriteObjectArray(JsonWriter, TEXT("levelVariantSets"), LevelVariantSets, AllExtensions);
				JsonWriter.WriteObjectEnd();
			}

			if (LightMaps.Num() > 0)
			{
				const EGLTFJsonExtension Extension = EGLTFJsonExtension::EPIC_LightmapTextures;
				AllExtensions.Used.Add(Extension);

				JsonWriter.WriteObjectStart(FGLTFJsonUtility::ToString(Extension));
				FGLTFJsonUtility::WriteObjectArray(JsonWriter, TEXT("lightmaps"), LightMaps, AllExtensions);
				JsonWriter.WriteObjectEnd();
			}

			JsonWriter.WriteObjectEnd();
		}

		FGLTFJsonUtility::WriteStringArray(JsonWriter, TEXT("extensionsUsed"), AllExtensions.Used);
		FGLTFJsonUtility::WriteStringArray(JsonWriter, TEXT("extensionsRequired"), AllExtensions.Required);

		JsonWriter.WriteObjectEnd();
	}

	template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
	void Serialize(FArchive* const Archive) const
	{
		TSharedRef<TJsonWriter<CharType, PrintPolicy>> JsonWriter = TJsonWriterFactory<CharType, PrintPolicy>::Create(Archive);
		WriteObject(*JsonWriter);
		JsonWriter->Close();
	}

	void Serialize(FArchive* const Archive, bool bPrettyPrint = true) const
	{
		if (bPrettyPrint)
		{
			Serialize<UTF8CHAR, TPrettyJsonPrintPolicy<UTF8CHAR>>(Archive);
		}
		else
		{
			Serialize<UTF8CHAR, TCondensedJsonPrintPolicy<UTF8CHAR>>(Archive);
		}
	}
};
