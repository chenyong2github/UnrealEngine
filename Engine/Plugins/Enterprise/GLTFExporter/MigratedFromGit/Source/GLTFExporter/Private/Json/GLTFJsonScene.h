// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Serialization/JsonSerializer.h"

struct FGLTFJsonScene
{
	FString Name;

	TArray<FGLTFJsonNodeIndex>            Nodes;
	TArray<FGLTFJsonLevelVariantSetsIndex> LevelVariantSets;

	template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
	void WriteObject(TJsonWriter<CharType, PrintPolicy>& JsonWriter, FGLTFJsonExtensions& Extensions) const
	{
		JsonWriter.WriteObjectStart();

		if (!Name.IsEmpty())
		{
			JsonWriter.WriteValue(TEXT("name"), Name);
		}

		if (Nodes.Num() > 0)
		{
			JsonWriter.WriteValue(TEXT("nodes"), Nodes);
		}

		const bool bWriteExtensions = LevelVariantSets.Num() > 0;

		if (bWriteExtensions)
		{
			JsonWriter.WriteObjectStart(TEXT("extensions"));

			if (LevelVariantSets.Num() > 0)
			{
				const EGLTFJsonExtension Extension = EGLTFJsonExtension::EPIC_LevelVariantSets;

				Extensions.Used.Add(Extension);

				JsonWriter.WriteObjectStart(FGLTFJsonUtility::ToString(Extension));
				FGLTFJsonUtility::WritePrimitiveArray(JsonWriter, TEXT("levelVariantSets"), LevelVariantSets);
				JsonWriter.WriteObjectEnd();
			}

			JsonWriter.WriteObjectEnd();
		}

		JsonWriter.WriteObjectEnd();
	}
};
