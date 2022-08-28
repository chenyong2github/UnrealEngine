// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFJsonUtility.h"
#include "Json/GLTFJsonEnums.h"
#include "Json/GLTFJsonColor3.h"
#include "Json/GLTFJsonColor4.h"
#include "Serialization/JsonSerializer.h"

struct FGLTFJsonPBRMetallicRoughness
{
	FGLTFJsonColor4 BaseColorFactor;

	float MetallicFactor;
	float RoughnessFactor;

	FGLTFJsonPBRMetallicRoughness()
		: BaseColorFactor(FGLTFJsonColor4::White)
		, MetallicFactor(1)
		, RoughnessFactor(1)
	{
	}

	template <class CharType = TCHAR, class PrintPolicy = TPrettyJsonPrintPolicy<CharType>>
    void WriteObject(TJsonWriter<CharType, PrintPolicy>& JsonWriter) const
	{
		JsonWriter.WriteObjectStart();

		if (BaseColorFactor != FGLTFJsonColor4::White)
		{
			JsonWriter.WriteIdentifierPrefix(TEXT("baseColorFactor"));
			BaseColorFactor.WriteArray(JsonWriter);
		}

		if (MetallicFactor != 1)
		{
			JsonWriter.WriteValue(TEXT("metallicFactor"), MetallicFactor);
		}

		if (RoughnessFactor != 1)
		{
			JsonWriter.WriteValue(TEXT("roughnessFactor"), RoughnessFactor);
		}

		JsonWriter.WriteObjectEnd();
	}
};

struct FGLTFJsonMaterial
{
	FString Name;

	FGLTFJsonPBRMetallicRoughness PBRMetallicRoughness;

	FGLTFJsonColor3 EmissiveFactor;

	EGLTFJsonAlphaMode AlphaMode;
	float AlphaCutoff;

	bool DoubleSided;

	FGLTFJsonMaterial()
        : EmissiveFactor(FGLTFJsonColor3::Black)
        , AlphaMode(EGLTFJsonAlphaMode::Opaque)
        , AlphaCutoff(0.5f)
		, DoubleSided(false)
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

		JsonWriter.WriteIdentifierPrefix(TEXT("pbrMetallicRoughness"));
		PBRMetallicRoughness.WriteObject(JsonWriter);

		if (EmissiveFactor != FGLTFJsonColor3::Black)
		{
			JsonWriter.WriteIdentifierPrefix(TEXT("emissiveFactor"));
			EmissiveFactor.WriteArray(JsonWriter);
		}

		if (AlphaMode != EGLTFJsonAlphaMode::Opaque)
		{
			JsonWriter.WriteValue(TEXT("alphaMode"), FGLTFJsonUtility::AlphaModeToString(AlphaMode));
		}

		if (AlphaMode == EGLTFJsonAlphaMode::Mask && AlphaCutoff != 0.5f)
		{
			JsonWriter.WriteValue(TEXT("alphaCutoff"), AlphaCutoff);
		}

		if (DoubleSided)
		{
			JsonWriter.WriteValue(TEXT("doubleSided"), DoubleSided);
		}

		JsonWriter.WriteObjectEnd();
	}
};
