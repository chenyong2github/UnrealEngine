// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Json/GLTFJsonAccessor.h"
#include "Json/GLTFJsonBuffer.h"
#include "Json/GLTFJsonBufferView.h"
#include "Json/GLTFJsonImage.h"
#include "Json/GLTFJsonMaterial.h"
#include "Json/GLTFJsonMesh.h"
#include "Json/GLTFJsonNode.h"
#include "Json/GLTFJsonSampler.h"
#include "Json/GLTFJsonScene.h"
#include "Json/GLTFJsonTexture.h"

#include "Containers/Set.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Runtime/Launch/Resources/Version.h"


struct FGLTFJsonAsset
{
	FString Version;
	FString Generator;
	FString Copyright;

	FGLTFJsonAsset()
		: Version(TEXT("2.0"))
		, Generator(TEXT(EPIC_PRODUCT_NAME) TEXT(" ") ENGINE_VERSION_STRING)
	{
	}

	template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
	void WriteObject(TJsonWriter<CharType, PrintPolicy>& JsonWriter) const
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

	TSet<EGLTFJsonExtension> ExtensionsUsed;
	TSet<EGLTFJsonExtension> ExtensionsRequired;

	FGLTFJsonSceneIndex DefaultScene;

	TArray<FGLTFJsonAccessor>   Accessors;
	TArray<FGLTFJsonBuffer>     Buffers;
	TArray<FGLTFJsonBufferView> BufferViews;
	TArray<FGLTFJsonMaterial>   Materials;
	TArray<FGLTFJsonMesh>       Meshes;
	TArray<FGLTFJsonNode>       Nodes;
	TArray<FGLTFJsonImage>      Images;
	TArray<FGLTFJsonSampler>    Samplers;
	TArray<FGLTFJsonScene>      Scenes;
	TArray<FGLTFJsonTexture>    Textures;

	template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
	void WriteObject(TJsonWriter<CharType, PrintPolicy>& JsonWriter) const
	{
		JsonWriter.WriteObjectStart();

		JsonWriter.WriteIdentifierPrefix(TEXT("asset"));
		Asset.WriteObject(JsonWriter);

		if (ExtensionsUsed.Num() > 0)
		{
			JsonWriter.WriteIdentifierPrefix(TEXT("extensionsUsed"));
			JsonWriter.WriteArrayStart();
			for (const EGLTFJsonExtension& Extension : ExtensionsUsed)
			{
				JsonWriter.WriteValue(FGLTFJsonUtility::ToString(Extension));
			}
			JsonWriter.WriteArrayEnd();
		}

		if (ExtensionsRequired.Num() > 0)
		{
			JsonWriter.WriteIdentifierPrefix(TEXT("extensionsRequired"));
			JsonWriter.WriteArrayStart();
			for (const EGLTFJsonExtension& Extension : ExtensionsRequired)
			{
				JsonWriter.WriteValue(FGLTFJsonUtility::ToString(Extension));
			}
			JsonWriter.WriteArrayEnd();
		}

		if (DefaultScene != INDEX_NONE)
		{
			JsonWriter.WriteValue(TEXT("scene"), DefaultScene);
		}

		FGLTFJsonUtility::WriteObjectArray(JsonWriter, TEXT("accessors"), Accessors);
		FGLTFJsonUtility::WriteObjectArray(JsonWriter, TEXT("buffers"), Buffers);
		FGLTFJsonUtility::WriteObjectArray(JsonWriter, TEXT("bufferViews"), BufferViews);
		FGLTFJsonUtility::WriteObjectArray(JsonWriter, TEXT("images"), Images);
		FGLTFJsonUtility::WriteObjectArray(JsonWriter, TEXT("materials"), Materials);
		FGLTFJsonUtility::WriteObjectArray(JsonWriter, TEXT("meshes"), Meshes);
		FGLTFJsonUtility::WriteObjectArray(JsonWriter, TEXT("nodes"), Nodes);
		FGLTFJsonUtility::WriteObjectArray(JsonWriter, TEXT("samplers"), Samplers);
		FGLTFJsonUtility::WriteObjectArray(JsonWriter, TEXT("scenes"), Scenes);
		FGLTFJsonUtility::WriteObjectArray(JsonWriter, TEXT("textures"), Textures);

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
