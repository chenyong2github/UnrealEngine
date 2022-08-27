// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonObject.h"
#include "Json/GLTFJsonIndex.h"

struct GLTFEXPORTER_API FGLTFJsonTexture : IGLTFJsonObject
{
	FString Name;

	FGLTFJsonSamplerIndex Sampler;

	FGLTFJsonImageIndex Source;

	EGLTFJsonHDREncoding Encoding;

	FGLTFJsonTexture()
		: Encoding(EGLTFJsonHDREncoding::None)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};
