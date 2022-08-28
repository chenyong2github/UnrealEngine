// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine.h"

enum class ERGBFormat : int8;

struct FGLTFTextureUtility
{
	static bool IsHDRFormat(EPixelFormat Format);

	static bool CanPNGCompressFormat(ETextureSourceFormat InFormat, ERGBFormat& OutFormat, uint32& OutBitDepth);
	static bool CanPNGCompressFormat(EPixelFormat InFormat, ERGBFormat& OutFormat, uint32& OutBitDepth);

	static bool IsCubemap(const UTexture* Texture);

	static TextureFilter GetDefaultFilter(TextureGroup Group);

	static TTuple<TextureAddress, TextureAddress> GetAddressXY(const UTexture* Texture);

	static const FByteBulkData& GetBulkData(const FTextureSource& TextureSource);

	static UTexture2D* CreateTransientTexture(const void* RawData, int64 ByteLength, const FIntPoint& Size, EPixelFormat Format, bool bUseSRGB = false);
};
