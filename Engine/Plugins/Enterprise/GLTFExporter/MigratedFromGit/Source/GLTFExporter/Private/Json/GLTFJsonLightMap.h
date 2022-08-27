// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonMaterial.h"
#include "Json/GLTFJsonVector2.h"
#include "Json/GLTFJsonVector4.h"
#include "Serialization/JsonSerializer.h"

struct FGLTFJsonLightMap
{
	FString              Name;
	FGLTFJsonVector4     ValueScale;
	FGLTFJsonVector4     ValueOffset;
	FGLTFJsonVector2     CoordinateScale;
	FGLTFJsonVector2     CoordinateOffset;
	FGLTFJsonTextureInfo Texture;

	FGLTFJsonLightMap()
		: ValueScale(FGLTFJsonVector4::One)
		, ValueOffset(FGLTFJsonVector4::Zero)
		, CoordinateScale(FGLTFJsonVector2::One)
		, CoordinateOffset(FGLTFJsonVector2::Zero)
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

		if (Texture.Index != INDEX_NONE)
		{
			JsonWriter.WriteIdentifierPrefix(TEXT("texture"));
			Texture.WriteObject(JsonWriter, Extensions);
		}

		JsonWriter.WriteIdentifierPrefix(TEXT("valueScale"));
		ValueScale.WriteArray(JsonWriter);

		JsonWriter.WriteIdentifierPrefix(TEXT("valueOffset"));
		ValueOffset.WriteArray(JsonWriter);

		JsonWriter.WriteIdentifierPrefix(TEXT("coordinateScale"));
		CoordinateScale.WriteArray(JsonWriter);

		JsonWriter.WriteIdentifierPrefix(TEXT("coordinateOffset"));
		CoordinateOffset.WriteArray(JsonWriter);

		JsonWriter.WriteObjectEnd();
	}
};
