// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Json/GLTFJsonVector3.h"
#include "Json/GLTFJsonUtility.h"

struct FGLTFJsonBackdrop
{
	FString Name;

	FGLTFJsonMeshIndex Mesh;
	FGLTFJsonTextureIndex Cubemap[6];

	float Intensity;
	float Size;

	FGLTFJsonVector3 ProjectionCenter;

	float LightingDistanceFactor;
	bool UseCameraProjection;

	FGLTFJsonBackdrop()
		: Intensity(1)
		, Size(1)
		, ProjectionCenter(FGLTFJsonVector3::Zero)
		, LightingDistanceFactor(0)
		, UseCameraProjection(false)
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

		if (Mesh != INDEX_NONE)
		{
			JsonWriter.WriteValue(TEXT("mesh"), Mesh);
		}

		FGLTFJsonUtility::WriteFixedArray(JsonWriter, TEXT("cubemap"), Cubemap);

		FGLTFJsonUtility::WriteExactValue(JsonWriter, TEXT("intensity"), Intensity);
		FGLTFJsonUtility::WriteExactValue(JsonWriter, TEXT("size"), Size);

		JsonWriter.WriteIdentifierPrefix(TEXT("projectionCenter"));
		ProjectionCenter.WriteArray(JsonWriter);

		FGLTFJsonUtility::WriteExactValue(JsonWriter, TEXT("lightingDistanceFactor"), LightingDistanceFactor);
		JsonWriter.WriteValue(TEXT("useCameraProjection"), UseCameraProjection);

		JsonWriter.WriteObjectEnd();
	}
};
