// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Serialization/JsonSerializer.h"

struct FGLTFJsonTexture
{
	FString Name;

	FGLTFJsonSamplerIndex Sampler;

	FGLTFJsonImageIndex Source;

	template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
	void WriteObject(TJsonWriter<CharType, PrintPolicy>& JsonWriter) const
	{
		JsonWriter.WriteObjectStart();

		if (!Name.IsEmpty())
		{
			JsonWriter.WriteValue(TEXT("name"), Name);
		}

		if (Sampler != INDEX_NONE)
		{
			JsonWriter.WriteValue(TEXT("sampler"), Sampler);
		}

		if (Source != INDEX_NONE)
		{
			JsonWriter.WriteValue(TEXT("source"), Source);
		}

		JsonWriter.WriteObjectEnd();
	}
};
