// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFJsonIndex.h"
#include "Serialization/JsonSerializer.h"

struct GLTFEXPORTER_API FGLTFJsonScene
{
	FString Name;

	TArray<FGLTFJsonNodeIndex> Nodes;

	template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
	void WriteObject(TJsonWriter<CharType, PrintPolicy>& JsonWriter) const
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

		JsonWriter.WriteObjectEnd();
	}
};
