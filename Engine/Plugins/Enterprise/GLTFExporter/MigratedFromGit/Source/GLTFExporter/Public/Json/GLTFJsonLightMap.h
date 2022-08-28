// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"
#include "Json/GLTFJsonMaterial.h"

struct GLTFEXPORTER_API FGLTFJsonLightMap : IGLTFJsonIndexedObject
{
	FString              Name;
	FGLTFJsonTextureInfo Texture;
	FGLTFJsonVector4     LightMapScale;
	FGLTFJsonVector4     LightMapAdd;
	FGLTFJsonVector4     CoordinateScaleBias;

	FGLTFJsonLightMap(int32 Index = INDEX_NONE)
		: IGLTFJsonIndexedObject(Index)
		, LightMapScale(FGLTFJsonVector4::One)
		, LightMapAdd(FGLTFJsonVector4::Zero)
		, CoordinateScaleBias({ 1, 1, 0, 0 })
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};
