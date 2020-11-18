// Copyright Epic Games, Inc. All Rights Reserved. 

#include "AssetUtils/Texture2DUtil.h"






static bool ReadTexture_PlatformData(
	UTexture2D* TextureMap,
	FImageDimensions& Dimensions,
	TImageBuilder<FVector4f>& DestImage)
{
	// Read from PlatformData
	// UBlueprintMaterialTextureNodesBPLibrary::Texture2D_SampleUV_EditorOnly() shows how to read from PlatformData
	// without converting formats if it's already uncompressed. And can read PF_FloatRGBA. Would make sense to do
	// that when possible, and perhaps is possible to convert to PF_FloatRGBA instead of using TC_VectorDisplacementmap?
	// 
	// Note that the current code cannot run on a background thread, UpdateResource() will call FlushRenderingCommands()
	// which will check() if it's on the Game Thread

	check(TextureMap->PlatformData);
	int32 Width = TextureMap->PlatformData->Mips[0].SizeX;
	int32 Height = TextureMap->PlatformData->Mips[0].SizeY;
	Dimensions = FImageDimensions(Width, Height);
	DestImage.SetDimensions(Dimensions);
	int64 Num = Dimensions.Num();

	// convert built platform texture data to uncompressed RGBA8 format
	TextureCompressionSettings InitialCompressionSettings = TextureMap->CompressionSettings;
	bool bWasSRGB = TextureMap->SRGB;
#if WITH_EDITOR
	TextureMipGenSettings InitialMipGenSettings = TextureMap->MipGenSettings;
#endif
	TextureMap->CompressionSettings = TextureCompressionSettings::TC_VectorDisplacementmap;
	TextureMap->SRGB = false;
#if WITH_EDITOR
	TextureMap->MipGenSettings = TextureMipGenSettings::TMGS_NoMipmaps;
#endif
	TextureMap->UpdateResource();

	// lock texture and represet as FColor
	const FColor* FormattedImageData = reinterpret_cast<const FColor*>(TextureMap->PlatformData->Mips[0].BulkData.LockReadOnly());

	// maybe could be done more quickly by row?
	for (int32 i = 0; i < Num; ++i)
	{
		FColor ByteColor = FormattedImageData[i];
		FLinearColor FloatColor(ByteColor);
		DestImage.SetPixel(i, FVector4f(FloatColor));
	}

	// restore built platform texture data to initial state
	TextureMap->PlatformData->Mips[0].BulkData.Unlock();
	TextureMap->CompressionSettings = InitialCompressionSettings;
	TextureMap->SRGB = bWasSRGB;
#if WITH_EDITOR
	TextureMap->MipGenSettings = InitialMipGenSettings;
#endif
	TextureMap->UpdateResource();

	return true;
}


#if WITH_EDITOR
static bool ReadTexture_SourceData(
	UTexture2D* TextureMap,
	FImageDimensions& Dimensions,
	TImageBuilder<FVector4f>& DestImage)
{
	FTextureSource& TextureSource = TextureMap->Source;

	int32 Width = TextureSource.GetSizeX();
	int32 Height = TextureSource.GetSizeY();
	Dimensions = FImageDimensions(Width, Height);
	DestImage.SetDimensions(Dimensions);
	int64 Num = Dimensions.Num();

	TArray64<uint8> SourceData;
	TextureMap->Source.GetMipData(SourceData, 0, 0, 0);
	ETextureSourceFormat SourceFormat = TextureSource.GetFormat();
	int32 BytesPerPixel = TextureSource.GetBytesPerPixel();
	const uint8* SourceDataPtr = SourceData.GetData();

	// code below is derived from UBlueprintMaterialTextureNodesBPLibrary::Texture2D_SampleUV_EditorOnly()
	if ((SourceFormat == TSF_BGRA8 || SourceFormat == TSF_BGRE8))
	{
		check(BytesPerPixel == sizeof(FColor));
		for (int32 i = 0; i < Num; ++i)
		{
			const uint8* PixelPtr = SourceDataPtr + (i * BytesPerPixel);
			FColor PixelColor = *((FColor*)PixelPtr);

			FLinearColor FloatColor = (TextureMap->SRGB) ?
				FLinearColor::FromSRGBColor(PixelColor) :
				FLinearColor(float(PixelColor.R), float(PixelColor.G), float(PixelColor.B), float(PixelColor.A)) / 255.0f;

			DestImage.SetPixel(i, FVector4f(FloatColor));
		}
	}
	else if ((SourceFormat == TSF_RGBA16 || SourceFormat == TSF_RGBA16F))
	{
		check(BytesPerPixel == sizeof(FFloat16Color));
		for (int32 i = 0; i < Num; ++i)
		{
			const uint8* PixelPtr = SourceDataPtr + (i * BytesPerPixel);
			FFloat16Color PixelColor = *((FFloat16Color*)PixelPtr);
			FLinearColor FloatColor(float(PixelColor.R), float(PixelColor.G), float(PixelColor.B), float(PixelColor.A));
			DestImage.SetPixel(i, FVector4f(FloatColor));
		}
	}
	else if (SourceFormat == TSF_G8)
	{
		check(BytesPerPixel == 1);
		for (int32 i = 0; i < Num; ++i)
		{
			const uint8* PixelPtr = SourceDataPtr + (i * BytesPerPixel);
			uint8 PixelColor = *PixelPtr;
			float PixelColorf = float(PixelColor) / 255.0f;
			FLinearColor FloatColor = (TextureMap->SRGB) ?
				FLinearColor::FromSRGBColor(FColor(PixelColor, PixelColor, PixelColor, 255)) :
				FLinearColor(PixelColorf, PixelColorf, PixelColorf, 1.0);
			DestImage.SetPixel(i, FVector4f(FloatColor));
		}
	}

	return true;
}
#endif 


bool UE::AssetUtils::ReadTexture(
	UTexture2D* TextureMap,
	FImageDimensions& Dimensions,
	TImageBuilder<FVector4f>& DestImage,
	bool bPreferPlatformData)
{
	if (ensure(TextureMap) == false) return false;

#if WITH_EDITOR
	if (TextureMap->Source.IsValid() && bPreferPlatformData == false)
	{
		return ReadTexture_SourceData(TextureMap, Dimensions, DestImage);
	}
#endif

	return ReadTexture_PlatformData(TextureMap, Dimensions, DestImage);
}