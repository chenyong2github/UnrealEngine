// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonEnums.h"
#include "Engine.h"

enum class ERGBFormat : int8;

struct FGLTFTextureUtility
{
	static bool IsHDRFormat(EPixelFormat Format);

	static bool CanPNGCompressFormat(ETextureSourceFormat InFormat, ERGBFormat& OutFormat, uint32& OutBitDepth);
	static bool CanPNGCompressFormat(EPixelFormat InFormat, ERGBFormat& OutFormat, uint32& OutBitDepth);

	static bool IsCubemap(const UTexture* Texture);
	static float GetCubeFaceRotation(ECubeFace CubeFace);

	static TextureFilter GetDefaultFilter(TextureGroup Group);

	static TTuple<TextureAddress, TextureAddress> GetAddressXY(const UTexture* Texture);

	static UTexture2D* CreateTransientTexture(const void* RawData, int64 ByteLength, const FIntPoint& Size, EPixelFormat Format, bool bUseSRGB = false);

	static UTextureRenderTarget2D* CreateRenderTarget(const FIntPoint& Size, EPixelFormat Format, bool bInForceLinearGamma = false);

	static bool DrawTexture(UTextureRenderTarget2D* OutTarget, const UTexture2D* InSource, const FMatrix& InTransform = FMatrix::Identity);
	static bool RotateTexture(UTextureRenderTarget2D* OutTarget, const UTexture2D* InSource, float InDegrees);

	static UTexture2D* CreateTextureFromCubeFace(const UTextureCube* TextureCube, ECubeFace CubeFace);
	static UTexture2D* CreateTextureFromCubeFace(const UTextureRenderTargetCube* RenderTargetCube, ECubeFace CubeFace);

	static bool ReadPixels(const UTextureRenderTarget2D* InRenderTarget, TArray<FColor>& OutPixels);
	static bool ReadEncodedPixels(const UTextureRenderTarget2D* InRenderTarget, TArray<FColor>& OutPixels, EGLTFJsonHDREncoding Encoding);

	static FColor EncodeRGBM(const FLinearColor& Color, float MaxRange = 8);
	static void EncodeRGBM(const TArray<FLinearColor>& InPixels, TArray<FColor>& OutPixels, float MaxRange = 8);

	// TODO: maybe use template specialization to avoid the need for duplicated functions
	static bool LoadPlatformData(UTexture2D* Texture);
	static bool LoadPlatformData(UTextureCube* TextureCube);

	static void FlipGreenChannel(TArray<FLinearColor>& Pixels);
	static void FlipGreenChannel(TArray<FColor>& Pixels);
};
