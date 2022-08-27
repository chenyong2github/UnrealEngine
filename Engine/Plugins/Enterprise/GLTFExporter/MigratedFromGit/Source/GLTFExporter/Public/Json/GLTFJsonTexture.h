// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonObject.h"
#include "Json/GLTFJsonIndex.h"

struct FGLTFJsonTexture : IGLTFJsonObject
{
	FString Name;

	FGLTFJsonSamplerIndex Sampler;

	FGLTFJsonImageIndex Source;

	EGLTFJsonHDREncoding Encoding;

	FGLTFJsonTexture()
		: Encoding(EGLTFJsonHDREncoding::None)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		if (!Name.IsEmpty())
		{
			Writer.Write(TEXT("name"), Name);
		}

		if (Sampler != INDEX_NONE)
		{
			Writer.Write(TEXT("sampler"), Sampler);
		}

		if (Source != INDEX_NONE)
		{
			Writer.Write(TEXT("source"), Source);
		}

		if (Encoding != EGLTFJsonHDREncoding::None)
		{
			Writer.StartExtensions();

			Writer.StartExtension(EGLTFJsonExtension::EPIC_TextureHDREncoding);
			Writer.Write(TEXT("encoding"), Encoding);
			Writer.EndExtension();

			Writer.EndExtensions();
		}
	}
};
