// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFJsonEnums.h"
#include "GLTFJsonObject.h"

struct GLTFEXPORTER_API FGLTFJsonAttributes : FGLTFJsonObject
{
	FGLTFJsonIndex Position; // always required
	FGLTFJsonIndex Normal;
	FGLTFJsonIndex Tangent;
	FGLTFJsonIndex TexCoord0;
	FGLTFJsonIndex TexCoord1;
	FGLTFJsonIndex Color0;
	// skeletal mesh attributes
	FGLTFJsonIndex Joints0;
	FGLTFJsonIndex Weights0;

	FGLTFJsonAttributes()
		: Position(INDEX_NONE)
		, Normal(INDEX_NONE)
		, Tangent(INDEX_NONE)
		, TexCoord0(INDEX_NONE)
		, TexCoord1(INDEX_NONE)
		, Color0(INDEX_NONE)
		, Joints0(INDEX_NONE)
		, Weights0(INDEX_NONE)
	{
	}

	template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
	void Write(TJsonWriter<CharType, PrintPolicy>& JsonWriter) const
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

struct GLTFEXPORTER_API FGLTFJsonPrimitive : FGLTFJsonObject
{
	FGLTFJsonIndex         Indices;
	FGLTFJsonIndex         Material;
	EGLTFJsonPrimitiveMode Mode;
	FGLTFJsonAttributes    Attributes;

	FGLTFJsonPrimitive()
		: Indices(INDEX_NONE)
		, Material(INDEX_NONE)
		, Mode(EGLTFJsonPrimitiveMode::None)
	{
	}

	template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
	void Write(TJsonWriter<CharType, PrintPolicy>& JsonWriter) const
	{
		JsonWriter.WriteObjectStart();

		JsonWriter.WriteIdentifierPrefix(TEXT("attributes"));
		Attributes.Write(JsonWriter);

		if (Indices != INDEX_NONE) JsonWriter.WriteValue(TEXT("indices"), Indices);
		if (Material != INDEX_NONE) JsonWriter.WriteValue(TEXT("material"), Material);
		if (Mode != EGLTFJsonPrimitiveMode::None) JsonWriter.WriteValue(TEXT("mode"), PrimitiveModeToNumber(Mode));

		JsonWriter.WriteObjectEnd();
	}
};

struct GLTFEXPORTER_API FGLTFJsonMesh : FGLTFJsonObject
{
	FString Name;

	TArray<FGLTFJsonPrimitive> Primitives;

	template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
	void Write(TJsonWriter<CharType, PrintPolicy>& JsonWriter) const
	{
		JsonWriter.WriteObjectStart();

		if (!Name.IsEmpty()) JsonWriter.WriteValue(TEXT("name"), Name);

		JsonWriter.WriteArrayStart(TEXT("primitives"));
		for (const FGLTFJsonPrimitive& Primitive : Primitives)
		{
			Primitive.Write(JsonWriter);
		}
		JsonWriter.WriteArrayEnd();

		JsonWriter.WriteObjectEnd();
	}
};
