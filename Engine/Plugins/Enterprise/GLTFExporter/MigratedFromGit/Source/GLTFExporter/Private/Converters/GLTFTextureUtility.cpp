// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFTextureUtility.h"
#include "Engine/Texture2DArray.h"

TextureAddress FGLTFTextureUtility::GetAddressX(const UTexture* Texture)
{
	if (const UTexture2D* Texture2D = Cast<UTexture2D>(Texture))
	{
		return Texture2D->AddressX;
	}

	if (const UTexture2DArray* Texture2DArray = Cast<UTexture2DArray>(Texture))
	{
		return Texture2DArray->AddressX;
	}

	if (const UTextureRenderTarget2D* RenderTarget2D = Cast<UTextureRenderTarget2D>(Texture))
	{
		return RenderTarget2D->AddressX;
	}

	// TODO: add support for UMediaTexture?

	return TextureAddress::TA_MAX;
}

TextureAddress FGLTFTextureUtility::GetAddressY(const UTexture* Texture)
{
	if (const UTexture2D* Texture2D = Cast<UTexture2D>(Texture))
	{
		return Texture2D->AddressY;
	}

	if (const UTexture2DArray* Texture2DArray = Cast<UTexture2DArray>(Texture))
	{
		return Texture2DArray->AddressY;
	}

	if (const UTextureRenderTarget2D* RenderTarget2D = Cast<UTextureRenderTarget2D>(Texture))
	{
		return RenderTarget2D->AddressY;
	}

	// TODO: add support for UMediaTexture?

	return TextureAddress::TA_MAX;
}
