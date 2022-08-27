// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Serialization/JsonSerializer.h"

struct GLTFEXPORTER_API FGLTFJsonNode
{
	FString Name;

	FVector Translation;
	FQuat   Rotation;
	FVector Scale;

	FGLTFJsonCameraIndex Camera;
	FGLTFJsonSkinIndex   Skin;
	FGLTFJsonMeshIndex   Mesh;

	TArray<FGLTFJsonNodeIndex> Children;

	FGLTFJsonNode()
		: Translation(FVector::ZeroVector)
		, Rotation(FQuat::Identity)
		, Scale(FVector::OneVector)
		, Camera(INDEX_NONE)
		, Skin(INDEX_NONE)
		, Mesh(INDEX_NONE)
	{
	}

	template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
	void WriteObject(TJsonWriter<CharType, PrintPolicy>& JsonWriter) const
	{
		JsonWriter.WriteObjectStart();

		if (!Name.IsEmpty())
		{
			JsonWriter.WriteValue(TEXT("name"), Name);
		}

		if (Translation != FVector::ZeroVector)
		{
			JsonWriter.WriteArrayStart(TEXT("translation"));
			JsonWriter.WriteValue(Translation.X);
			JsonWriter.WriteValue(Translation.Y);
			JsonWriter.WriteValue(Translation.Z);
			JsonWriter.WriteArrayEnd();
		}

		if (Rotation != FQuat::Identity)
		{
			JsonWriter.WriteArrayStart(TEXT("rotation"));
			JsonWriter.WriteValue(Rotation.X);
			JsonWriter.WriteValue(Rotation.Y);
			JsonWriter.WriteValue(Rotation.Z);
			JsonWriter.WriteValue(Rotation.W);
			JsonWriter.WriteArrayEnd();
		}

		if (Scale != FVector::OneVector)
		{
			JsonWriter.WriteArrayStart(TEXT("scale"));
			JsonWriter.WriteValue(Scale.X);
			JsonWriter.WriteValue(Scale.Y);
			JsonWriter.WriteValue(Scale.Z);
			JsonWriter.WriteArrayEnd();
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

		if (Children.Num() > 0)
		{
			JsonWriter.WriteValue(TEXT("children"), Children);
		}

		JsonWriter.WriteObjectEnd();
	}
};
