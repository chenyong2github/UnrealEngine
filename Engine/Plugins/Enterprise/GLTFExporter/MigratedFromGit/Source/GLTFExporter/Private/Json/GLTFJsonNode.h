// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Json/GLTFJsonVector3.h"
#include "Json/GLTFJsonQuaternion.h"
#include "Serialization/JsonSerializer.h"

struct FGLTFJsonNode
{
	FString Name;

	FGLTFJsonVector3    Translation;
	FGLTFJsonQuaternion Rotation;
	FGLTFJsonVector3    Scale;

	FGLTFJsonCameraIndex    Camera;
	FGLTFJsonSkinIndex      Skin;
	FGLTFJsonMeshIndex      Mesh;
	FGLTFJsonLightMapIndex  LightMap;

	TArray<FGLTFJsonNodeIndex> Children;

	FGLTFJsonNode()
		: Translation(FGLTFJsonVector3::Zero)
		, Rotation(FGLTFJsonQuaternion::Identity)
		, Scale(FGLTFJsonVector3::One)
	{
	}

	template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
	void WriteObject(TJsonWriter<CharType, PrintPolicy>& JsonWriter, FGLTFJsonExtensions& Extensions) const
	{
		JsonWriter.WriteObjectStart();

		if (!Name.IsEmpty())
		{
			JsonWriter.WriteValue(TEXT("name"), Name);
		}

		if (Translation != FGLTFJsonVector3::Zero)
		{
			JsonWriter.WriteIdentifierPrefix(TEXT("translation"));
			Translation.WriteArray(JsonWriter);
		}

		if (Rotation != FGLTFJsonQuaternion::Identity)
		{
			JsonWriter.WriteIdentifierPrefix(TEXT("rotation"));
			Rotation.WriteArray(JsonWriter);
		}

		if (Scale != FGLTFJsonVector3::One)
		{
			JsonWriter.WriteIdentifierPrefix(TEXT("scale"));
			Scale.WriteArray(JsonWriter);
		}

		if (Camera != INDEX_NONE)
		{
			JsonWriter.WriteValue(TEXT("camera"), Camera);
		}

		if (Skin != INDEX_NONE)
		{
			JsonWriter.WriteValue(TEXT("skin"), Skin);
		}

		if (Mesh != INDEX_NONE)
		{
			JsonWriter.WriteValue(TEXT("mesh"), Mesh);
		}

		const bool bWriteExtensions = LightMap != INDEX_NONE;

		if (bWriteExtensions)
		{
			JsonWriter.WriteObjectStart(TEXT("extensions"));

			if (LightMap != INDEX_NONE)
			{
				const EGLTFJsonExtension Extension = EGLTFJsonExtension::EPIC_LightmapTextures;
				Extensions.Used.Add(Extension);

				JsonWriter.WriteObjectStart(FGLTFJsonUtility::ToString(Extension));
				JsonWriter.WriteValue(TEXT("lightmap"), LightMap);
				JsonWriter.WriteObjectEnd();
			}

			JsonWriter.WriteObjectEnd();
		}

		if (Children.Num() > 0)
		{
			JsonWriter.WriteValue(TEXT("children"), Children);
		}

		JsonWriter.WriteObjectEnd();
	}
};
