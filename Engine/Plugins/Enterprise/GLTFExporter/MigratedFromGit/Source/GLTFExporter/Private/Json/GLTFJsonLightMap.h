// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonObject.h"
#include "Json/GLTFJsonMaterial.h"
#include "Json/GLTFJsonVector.h"

struct FGLTFJsonLightMap : IGLTFJsonObject
{
	FString              Name;
	FGLTFJsonTextureInfo Texture;
	FGLTFJsonVector4     LightMapScale;
	FGLTFJsonVector4     LightMapAdd;
	FGLTFJsonVector4     CoordinateScaleBias;

	FGLTFJsonLightMap()
		: LightMapScale(FGLTFJsonVector4::One)
		, LightMapAdd(FGLTFJsonVector4::Zero)
		, CoordinateScaleBias({ 1, 1, 0, 0 })
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		if (!Name.IsEmpty())
		{
			Writer.Write(TEXT("name"), Name);
		}

		if (Texture.Index != INDEX_NONE)
		{
			Writer.Write(TEXT("texture"), Texture);
		}

		Writer.Write(TEXT("lightmapScale"), LightMapScale);
		Writer.Write(TEXT("lightmapAdd"), LightMapAdd);
		Writer.Write(TEXT("coordinateScaleBias"), CoordinateScaleBias);
	}
};
