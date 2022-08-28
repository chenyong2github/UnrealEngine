// Copyright Epic Games, Inc. All Rights Reserved.

#include "Json/GLTFJsonTexture.h"

void FGLTFJsonTexture::WriteObject(IGLTFJsonWriter& Writer) const
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
