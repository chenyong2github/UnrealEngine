// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonExtensions.h"
#include "Serialization/JsonSerializer.h"

struct FGLTFJsonAsset
{
	FString Version;
	FString Generator;
	FString Copyright;

	FGLTFJsonAsset()
		: Version(TEXT("2.0"))
	{
	}

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
