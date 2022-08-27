// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonEnums.h"
#include "Json/GLTFJsonIndex.h"
#include "Serialization/JsonSerializer.h"

struct FGLTFJsonAttributes
{
	FGLTFJsonAccessorIndex Position; // always required
	FGLTFJsonAccessorIndex Normal;
	FGLTFJsonAccessorIndex Tangent;
	FGLTFJsonAccessorIndex TexCoord0;
	FGLTFJsonAccessorIndex TexCoord1;
	FGLTFJsonAccessorIndex Color0;
	// skeletal mesh attributes
	FGLTFJsonAccessorIndex Joints0;
	FGLTFJsonAccessorIndex Weights0;

	template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
	void WriteObject(TJsonWriter<CharType, PrintPolicy>& JsonWriter) const
	{
		JsonWriter.WriteObjectStart();

		JsonWriter.WriteValue(TEXT("POSITION"), Position);
		if (Normal != INDEX_NONE) JsonWriter.WriteValue(TEXT("NORMAL"), Normal);
		if (Tangent != INDEX_NONE) JsonWriter.WriteValue(TEXT("TANGENT"), Tangent);
		if (TexCoord0 != INDEX_NONE) JsonWriter.WriteValue(TEXT("TEXCOORD_0"), TexCoord0);
		if (TexCoord1 != INDEX_NONE) JsonWriter.WriteValue(TEXT("TEXCOORD_1"), TexCoord1);
		if (Color0 != INDEX_NONE) JsonWriter.WriteValue(TEXT("COLOR_0"), Color0);
		if (Joints0 != INDEX_NONE) JsonWriter.WriteValue(TEXT("JOINTS_0"), Joints0);
		if (Weights0 != INDEX_NONE) JsonWriter.WriteValue(TEXT("WEIGHTS_0"), Weights0);

		JsonWriter.WriteObjectEnd();
	}
};

struct FGLTFJsonPrimitive
{
	FGLTFJsonAccessorIndex Indices;
	FGLTFJsonMaterialIndex Material;
	EGLTFJsonPrimitiveMode Mode;
	FGLTFJsonAttributes    Attributes;

	FGLTFJsonPrimitive()
		: Mode(EGLTFJsonPrimitiveMode::None)
	{
	}

	template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
	void WriteObject(TJsonWriter<CharType, PrintPolicy>& JsonWriter) const
	{
		JsonWriter.WriteObjectStart();

		JsonWriter.WriteIdentifierPrefix(TEXT("attributes"));
		Attributes.WriteObject(JsonWriter);

		if (Indices != INDEX_NONE)
		{
			JsonWriter.WriteValue(TEXT("indices"), Indices);
		}

		if (Material != INDEX_NONE)
		{
			JsonWriter.WriteValue(TEXT("material"), Material);
		}

		if (Mode != EGLTFJsonPrimitiveMode::None)
		{
			JsonWriter.WriteValue(TEXT("mode"), FGLTFJsonUtility::ToInteger(Mode));
		}

		JsonWriter.WriteObjectEnd();
	}
};

struct FGLTFJsonMesh
{
	FString Name;

	TArray<FGLTFJsonPrimitive> Primitives;

	template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
	void WriteObject(TJsonWriter<CharType, PrintPolicy>& JsonWriter) const
	{
		JsonWriter.WriteObjectStart();

		if (!Name.IsEmpty())
		{
			JsonWriter.WriteValue(TEXT("name"), Name);
		}

		JsonWriter.WriteArrayStart(TEXT("primitives"));
		for (const FGLTFJsonPrimitive& Primitive : Primitives)
		{
			Primitive.WriteObject(JsonWriter);
		}
		JsonWriter.WriteArrayEnd();

		JsonWriter.WriteObjectEnd();
	}
};
