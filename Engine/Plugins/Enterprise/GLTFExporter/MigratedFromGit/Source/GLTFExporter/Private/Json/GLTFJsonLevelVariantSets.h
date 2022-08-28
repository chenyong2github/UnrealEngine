// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFJsonIndex.h"
#include "Json/GLTFJsonExtensions.h"
#include "Json/GLTFJsonUtility.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Optional.h"

struct FGLTFJsonVariantNode
{
	FGLTFJsonNodeIndex Node;
	TOptional<bool>    bIsVisible;

	template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
	void WriteObject(TJsonWriter<CharType, PrintPolicy>& JsonWriter, FGLTFJsonExtensions& Extensions) const
	{
		JsonWriter.WriteObjectStart();

		if (Node != INDEX_NONE)
		{
			JsonWriter.WriteValue(TEXT("node"), Node);
		}

		JsonWriter.WriteObjectStart(TEXT("properties"));

		if (bIsVisible.IsSet())
		{
			JsonWriter.WriteValue(TEXT("visible"), bIsVisible.GetValue());
		}

		JsonWriter.WriteObjectEnd();
		JsonWriter.WriteObjectEnd();
	}
};

struct FGLTFJsonVariant
{
	FString Name;
	bool    bIsActive;

	FGLTFJsonTextureIndex        Thumbnail;
	TArray<FGLTFJsonVariantNode> Nodes;

	template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
	void WriteObject(TJsonWriter<CharType, PrintPolicy>& JsonWriter, FGLTFJsonExtensions& Extensions) const
	{
		JsonWriter.WriteObjectStart();
		JsonWriter.WriteValue(TEXT("name"), Name);
		JsonWriter.WriteValue(TEXT("active"), bIsActive);

		if (Thumbnail != INDEX_NONE)
		{
			JsonWriter.WriteValue(TEXT("thumbnail"), Thumbnail);
		}

		FGLTFJsonUtility::WriteObjectArray(JsonWriter, TEXT("nodes"), Nodes, Extensions);

		JsonWriter.WriteObjectEnd();
	}
};

struct FGLTFJsonVariantSet
{
	FString Name;

	TArray<FGLTFJsonVariant> Variants;

	template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
	void WriteObject(TJsonWriter<CharType, PrintPolicy>& JsonWriter, FGLTFJsonExtensions& Extensions) const
	{
		JsonWriter.WriteObjectStart();

		if (!Name.IsEmpty())
		{
			JsonWriter.WriteValue(TEXT("name"), Name);
		}

		FGLTFJsonUtility::WriteObjectArray(JsonWriter, TEXT("variants"), Variants, Extensions);

		JsonWriter.WriteObjectEnd();
	}
};

struct FGLTFJsonLevelVariantSets
{
	FString Name;

	TArray<FGLTFJsonVariantSet> VariantSets;

	template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
	void WriteObject(TJsonWriter<CharType, PrintPolicy>& JsonWriter, FGLTFJsonExtensions& Extensions) const
	{
		JsonWriter.WriteObjectStart();

		if (!Name.IsEmpty())
		{
			JsonWriter.WriteValue(TEXT("name"), Name);
		}

		FGLTFJsonUtility::WriteObjectArray(JsonWriter, TEXT("variantSets"), VariantSets, Extensions);

		JsonWriter.WriteObjectEnd();
	}
};
