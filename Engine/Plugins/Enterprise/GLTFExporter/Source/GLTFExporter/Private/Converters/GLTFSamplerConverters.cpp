// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFSamplerConverters.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Utilities/GLTFCoreUtilities.h"
#include "Converters/GLTFTextureUtility.h"

FGLTFJsonSampler* FGLTFTextureSamplerConverter::Convert(const UTexture* Texture)
{
	TextureAddress AddressX;
	TextureAddress AddressY;

	if (FGLTFTextureUtility::IsCubemap(Texture))
	{
		AddressX = TA_Clamp;
		AddressY = TA_Clamp;
	}
	else
	{
		FGLTFTextureUtility::GetAddressXY(Texture, AddressX, AddressY);
	}

	return Builder.AddUniqueSampler(AddressX, AddressY, Texture->Filter, Texture->LODGroup);
}

void FGLTFSamplerConverter::Sanitize(TextureAddress& AddressX, TextureAddress& AddressY, TextureFilter& Filter, TextureGroup& LODGroup)
{
	if (Filter == TF_Default)
	{
		Filter = FGLTFTextureUtility::GetDefaultFilter(LODGroup);
	}

	LODGroup = TEXTUREGROUP_MAX; // Ignore it, since Filter should cover any missing information
}

FGLTFJsonSampler* FGLTFSamplerConverter::Convert(TextureAddress AddressX, TextureAddress AddressY, TextureFilter Filter, TextureGroup LODGroup)
{
	FGLTFJsonSampler* JsonSampler = Builder.AddSampler();
	JsonSampler->MinFilter = FGLTFCoreUtilities::ConvertMinFilter(Filter);
	JsonSampler->MagFilter = FGLTFCoreUtilities::ConvertMagFilter(Filter);
	JsonSampler->WrapS = FGLTFCoreUtilities::ConvertWrap(AddressX);
	JsonSampler->WrapT = FGLTFCoreUtilities::ConvertWrap(AddressY);
	return JsonSampler;
}
