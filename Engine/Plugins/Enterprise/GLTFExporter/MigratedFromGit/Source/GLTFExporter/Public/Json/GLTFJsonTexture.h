// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"

struct GLTFEXPORTER_API FGLTFJsonTexture : IGLTFJsonIndexedObject
{
	FString Name;

	FGLTFJsonSamplerIndex Sampler;

	FGLTFJsonImageIndex Source;

	EGLTFJsonHDREncoding Encoding;

	FGLTFJsonTexture(int32 Index = INDEX_NONE)
		: IGLTFJsonIndexedObject(Index)
		, Encoding(EGLTFJsonHDREncoding::None)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};
