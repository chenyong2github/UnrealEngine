// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFJsonIndex.h"
#include "GLTFJsonAccessor.h"
#include "GLTFJsonBufferView.h"
#include "GLTFJsonBuffer.h"
#include "GLTFJsonMesh.h"
#include "GLTFJsonScene.h"
#include "GLTFJsonNode.h"

#include "Containers/Set.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Runtime/Launch/Resources/Version.h"


struct GLTFEXPORTER_API FGLTFJsonAsset
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
	void Write(TJsonWriter<CharType, PrintPolicy>& JsonWriter) const
	{
		JsonWriter.WriteObjectStart();

		JsonWriter.WriteValue(TEXT("version"), Version);
		if (!Generator.IsEmpty()) JsonWriter.WriteValue(TEXT("generator"), Generator);
		if (!Copyright.IsEmpty()) JsonWriter.WriteValue(TEXT("copyright"), Copyright);

		JsonWriter.WriteObjectEnd();
	}
};

struct GLTFEXPORTER_API FGLTFJsonRoot
{
	FGLTFJsonAsset      Asset;
	FGLTFJsonSceneIndex DefaultScene;

	TArray<FGLTFJsonAccessor>   Accessors;
	TArray<FGLTFJsonBuffer>     Buffers;
	TArray<FGLTFJsonBufferView> BufferViews;
	TArray<FGLTFJsonMesh>       Meshes;
	TArray<FGLTFJsonNode>       Nodes;
	TArray<FGLTFJsonScene>      Scenes;

	FGLTFJsonRoot()
		: DefaultScene(INDEX_NONE)
	{
	}

	template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
	void Write(TJsonWriter<CharType, PrintPolicy>& JsonWriter) const
	{
		JsonWriter.WriteObjectStart();

		JsonWriter.WriteIdentifierPrefix(TEXT("asset"));
		Asset.Write(JsonWriter);

		if (DefaultScene != INDEX_NONE) JsonWriter.WriteValue(TEXT("scene"), DefaultScene);

		if (Accessors.Num() > 0)
		{
			JsonWriter.WriteArrayStart(TEXT("accessors"));
			for (const FGLTFJsonAccessor& Accessor : Accessors)
			{
				Accessor.Write(JsonWriter);
			}
			JsonWriter.WriteArrayEnd();
		}

		if (BufferViews.Num() > 0)
		{
			JsonWriter.WriteArrayStart(TEXT("bufferViews"));
			for (const FGLTFJsonBufferView& BufferView : BufferViews)
			{
				BufferView.Write(JsonWriter);
			}
			JsonWriter.WriteArrayEnd();
		}

		if (Meshes.Num() > 0)
		{
			JsonWriter.WriteArrayStart(TEXT("meshes"));
			for (const FGLTFJsonMesh& Mesh : Meshes)
			{
				Mesh.Write(JsonWriter);
			}
			JsonWriter.WriteArrayEnd();
		}

		if (Nodes.Num() > 0)
		{
			JsonWriter.WriteArrayStart(TEXT("nodes"));
			for (const FGLTFJsonNode& Node : Nodes)
			{
				Node.Write(JsonWriter);
			}
			JsonWriter.WriteArrayEnd();
		}

		if (Scenes.Num() > 0)
		{
			JsonWriter.WriteArrayStart(TEXT("scenes"));
			for (const FGLTFJsonScene& Scene : Scenes)
			{
				Scene.Write(JsonWriter);
			}
			JsonWriter.WriteArrayEnd();
		}

		if (Buffers.Num() > 0)
		{
			JsonWriter.WriteArrayStart(TEXT("buffers"));
			for (const FGLTFJsonBuffer& Buffer : Buffers)
			{
				Buffer.Write(JsonWriter);
			}
			JsonWriter.WriteArrayEnd();
		}

		JsonWriter.WriteObjectEnd();
	}

	template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
	void Serialize(FArchive* const Archive) const
	{
		TSharedRef<TJsonWriter<CharType, PrintPolicy>> JsonWriter = TJsonWriterFactory<CharType, PrintPolicy>::Create(Archive);
		Write(*JsonWriter);
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
