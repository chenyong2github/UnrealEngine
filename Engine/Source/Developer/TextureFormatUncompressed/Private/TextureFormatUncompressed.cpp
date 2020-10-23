// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/ITextureFormat.h"
#include "Interfaces/ITextureFormatModule.h"
#include "TextureCompressorModule.h"
#include "PixelFormat.h"
#include "ImageCore.h"

DEFINE_LOG_CATEGORY_STATIC(LogTextureFormatUncompressed, Log, All);

/**
 * Macro trickery for supported format names.
 */
#define ENUM_SUPPORTED_FORMATS(op) \
	op(BGRA8) \
	op(G8) \
	op(G16) \
	op(VU8) \
	op(RGBA16F) \
	op(XGXR8) \
	op(RGBA8) \
	op(POTERROR) \
	op(R16F)

#define DECL_FORMAT_NAME(FormatName) static FName GTextureFormatName##FormatName = FName(TEXT(#FormatName));
ENUM_SUPPORTED_FORMATS(DECL_FORMAT_NAME);
#undef DECL_FORMAT_NAME

#define DECL_FORMAT_NAME_ENTRY(FormatName) GTextureFormatName##FormatName ,
static FName GSupportedTextureFormatNames[] =
{
	ENUM_SUPPORTED_FORMATS(DECL_FORMAT_NAME_ENTRY)
};
#undef DECL_FORMAT_NAME_ENTRY

#undef ENUM_SUPPORTED_FORMATS

/**
 * Uncompressed texture format handler.
 */
class FTextureFormatUncompressed : public ITextureFormat
{
	virtual bool AllowParallelBuild() const override
	{
		return true;
	}

	virtual uint16 GetVersion(
		FName Format,
		const struct FTextureBuildSettings* BuildSettings = nullptr
	) const override
	{
		return 0;
	}

	virtual void GetSupportedFormats(TArray<FName>& OutFormats) const override
	{
		for (int32 i = 0; i < UE_ARRAY_COUNT(GSupportedTextureFormatNames); ++i)
		{
			OutFormats.Add(GSupportedTextureFormatNames[i]);
		}
	}
	
	virtual FTextureFormatCompressorCaps GetFormatCapabilities() const override
	{
		return FTextureFormatCompressorCaps(); // Default capabilities.
	}

	virtual EPixelFormat GetPixelFormatForImage(const struct FTextureBuildSettings& BuildSettings, const struct FImage& Image, bool bImageHasAlphaChannel) const override
	{
		if (BuildSettings.TextureFormatName == GTextureFormatNameG8)
		{
			return PF_G8;
		}
		else if (BuildSettings.TextureFormatName == GTextureFormatNameG16)
		{
			return PF_G16;
		}
		else if (BuildSettings.TextureFormatName == GTextureFormatNameVU8)
		{
			return PF_V8U8;
		}
		else if (BuildSettings.TextureFormatName == GTextureFormatNameBGRA8 || BuildSettings.TextureFormatName == GTextureFormatNameRGBA8 ||
				 BuildSettings.TextureFormatName == GTextureFormatNameXGXR8)
		{
			return PF_B8G8R8A8;
		}
		else if (BuildSettings.TextureFormatName == GTextureFormatNameRGBA16F)
		{
			return PF_FloatRGBA;
		}
		else if (BuildSettings.TextureFormatName == GTextureFormatNameR16F)
		{
			return PF_R16F;
		}
		else if (BuildSettings.TextureFormatName == GTextureFormatNamePOTERROR)
		{
			return PF_B8G8R8A8;
		}

		UE_LOG(LogTextureFormatUncompressed, Fatal, TEXT("Unhandled texture format '%s' given to FTextureFormatUncompressed::GetPixelFormatForImage()"), *BuildSettings.TextureFormatName.ToString());
		return PF_Unknown;
	}

	virtual bool CompressImage(
		const FImage& InImage,
		const struct FTextureBuildSettings& BuildSettings,
		bool bImageHasAlphaChannel,
		FCompressedImage2D& OutCompressedImage
		) const override
	{
		OutCompressedImage.PixelFormat = GetPixelFormatForImage(BuildSettings, InImage, bImageHasAlphaChannel);

		if (BuildSettings.TextureFormatName == GTextureFormatNameG8)
		{
			FImage Image;
			InImage.CopyTo(Image, ERawImageFormat::G8, BuildSettings.GetGammaSpace());

			OutCompressedImage.SizeX = Image.SizeX;
			OutCompressedImage.SizeY = Image.SizeY;
			OutCompressedImage.SizeZ = (BuildSettings.bVolume || BuildSettings.bTextureArray) ? Image.NumSlices : 1;
			OutCompressedImage.RawData = MoveTemp(Image.RawData);

			return true;
		}
		else if (BuildSettings.TextureFormatName == GTextureFormatNameG16)
		{
			FImage Image;
			InImage.CopyTo(Image, ERawImageFormat::G16, EGammaSpace::Linear);

			OutCompressedImage.SizeX = Image.SizeX;
			OutCompressedImage.SizeY = Image.SizeY;
			OutCompressedImage.SizeZ = BuildSettings.bVolume ? Image.NumSlices : 1;
			OutCompressedImage.RawData = MoveTemp(Image.RawData);

			return true;
		}
		else if (BuildSettings.TextureFormatName == GTextureFormatNameVU8)
		{
			FImage Image;
			InImage.CopyTo(Image, ERawImageFormat::BGRA8, BuildSettings.GetGammaSpace());

			OutCompressedImage.SizeX = Image.SizeX;
			OutCompressedImage.SizeY = Image.SizeY;
			OutCompressedImage.SizeZ = (BuildSettings.bVolume || BuildSettings.bTextureArray) ? Image.NumSlices : 1;

			uint64 NumTexels = (uint64)Image.SizeX * Image.SizeY * Image.NumSlices;
			OutCompressedImage.RawData.Empty(NumTexels * 2);
			OutCompressedImage.RawData.AddUninitialized(NumTexels * 2);
			const FColor* FirstColor = (&Image.AsBGRA8()[0]);
			const FColor* LastColor = FirstColor + NumTexels;
			int8* Dest = (int8*)OutCompressedImage.RawData.GetData();

			for (const FColor* Color = FirstColor; Color < LastColor; ++Color)
			{
				*Dest++ = (int32)Color->R - 128;
				*Dest++ = (int32)Color->G - 128;
			}

			return true;
		}
		else if (BuildSettings.TextureFormatName == GTextureFormatNameBGRA8)
		{
			FImage Image;
			InImage.CopyTo(Image, ERawImageFormat::BGRA8, BuildSettings.GetGammaSpace());

			OutCompressedImage.SizeX = Image.SizeX;
			OutCompressedImage.SizeY = Image.SizeY;
			OutCompressedImage.SizeZ = (BuildSettings.bVolume || BuildSettings.bTextureArray) ? Image.NumSlices : 1;
			OutCompressedImage.RawData = MoveTemp(Image.RawData);

			return true;
		}
		else if (BuildSettings.TextureFormatName == GTextureFormatNameRGBA8)
		{
			FImage Image;
			InImage.CopyTo(Image, ERawImageFormat::BGRA8, BuildSettings.GetGammaSpace());

			OutCompressedImage.SizeX = Image.SizeX;
			OutCompressedImage.SizeY = Image.SizeY;
			OutCompressedImage.SizeZ = (BuildSettings.bVolume || BuildSettings.bTextureArray) ? Image.NumSlices : 1;

			// swizzle each texel
			uint64 NumTexels = (uint64)Image.SizeX * Image.SizeY * Image.NumSlices;
			OutCompressedImage.RawData.Empty(NumTexels * 4);
			OutCompressedImage.RawData.AddUninitialized(NumTexels * 4);
			const FColor* FirstColor = (&Image.AsBGRA8()[0]);
			const FColor* LastColor = FirstColor + NumTexels;
			int8* Dest = (int8*)OutCompressedImage.RawData.GetData();

			for (const FColor* Color = FirstColor; Color < LastColor; ++Color)
			{
				*Dest++ = (int32)Color->R;
				*Dest++ = (int32)Color->G;
				*Dest++ = (int32)Color->B;
				*Dest++ = (int32)Color->A;
			}

			return true;
		}
		else if (BuildSettings.TextureFormatName == GTextureFormatNameXGXR8)
		{
			FImage Image;
			InImage.CopyTo(Image, ERawImageFormat::BGRA8, BuildSettings.GetGammaSpace());

			OutCompressedImage.SizeX = Image.SizeX;
			OutCompressedImage.SizeY = Image.SizeY;
			OutCompressedImage.SizeZ = (BuildSettings.bVolume || BuildSettings.bTextureArray) ? Image.NumSlices : 1;

			// swizzle each texel
			uint64 NumTexels = (uint64)Image.SizeX * Image.SizeY * Image.NumSlices;
			OutCompressedImage.RawData.Empty(NumTexels * 4);
			OutCompressedImage.RawData.AddUninitialized(NumTexels * 4);
			const FColor* FirstColor = (&Image.AsBGRA8()[0]);
			const FColor* LastColor = FirstColor + NumTexels;
			int8* Dest = (int8*)OutCompressedImage.RawData.GetData();

			for (const FColor* Color = FirstColor; Color < LastColor; ++Color)
			{
				*Dest++ = (int32)Color->B;
				*Dest++ = (int32)Color->G;
				*Dest++ = (int32)Color->A;
				*Dest++ = (int32)Color->R;
			}

			return true;
		}
		else if (BuildSettings.TextureFormatName == GTextureFormatNameRGBA16F)
		{
			FImage Image;
			InImage.CopyTo(Image, ERawImageFormat::RGBA16F, EGammaSpace::Linear);

			OutCompressedImage.SizeX = Image.SizeX;
			OutCompressedImage.SizeY = Image.SizeY;
			OutCompressedImage.SizeZ = (BuildSettings.bVolume || BuildSettings.bTextureArray) ? Image.NumSlices : 1;
			OutCompressedImage.RawData = MoveTemp(Image.RawData);

			return true;
		}
		else if (BuildSettings.TextureFormatName == GTextureFormatNameR16F)
		{
			FImage Image;
			InImage.CopyTo(Image, ERawImageFormat::R16F, EGammaSpace::Linear);

			OutCompressedImage.SizeX = Image.SizeX;
			OutCompressedImage.SizeY = Image.SizeY;
			OutCompressedImage.SizeZ = BuildSettings.bVolume ? Image.NumSlices : 1;
			OutCompressedImage.RawData = MoveTemp(Image.RawData);

			return true;
		}
		else if (BuildSettings.TextureFormatName == GTextureFormatNamePOTERROR)
		{
			// load the error image data we will just repeat into the texture
			TArray64<uint8> ErrorData;
			FFileHelper::LoadFileToArray(ErrorData, *(FPaths::EngineDir() / TEXT("Content/MobileResources/PowerOfTwoError64x64.raw")));
			check(ErrorData.Num() == 16384);

			// set output
			OutCompressedImage.SizeX = InImage.SizeX;
			OutCompressedImage.SizeY = InImage.SizeY;
			OutCompressedImage.SizeZ = (BuildSettings.bVolume || BuildSettings.bTextureArray) ? InImage.NumSlices : 1;

			// allocate output memory
			check(InImage.NumSlices == 1);
			uint64 NumTexels = (uint64)InImage.SizeX * InImage.SizeY;
			OutCompressedImage.RawData.Empty(NumTexels * 4);
			OutCompressedImage.RawData.AddUninitialized(NumTexels * 4);

			// write out texels
			uint8* Src = ErrorData.GetData();
			uint8* Dest = (uint8*)OutCompressedImage.RawData.GetData();
			for (int32 Y = 0; Y < InImage.SizeY; Y++)
			{
				for (int32 X = 0; X < InImage.SizeX * 4; X++)
				{
					int32 SrcX = X & (64 * 4 - 1);
					int32 SrcY = Y & 63;
					Dest[(uint64)Y * InImage.SizeX * 4 + X] = Src[(uint64)SrcY * 64 * 4 + SrcX];
				}
			}

			
			return true;
		}

		UE_LOG(LogTextureFormatUncompressed, Warning,
			TEXT("Cannot convert uncompressed image to format '%s'."),
			*BuildSettings.TextureFormatName.ToString()
			);

		return false;
	}
};

/**
 * Module for uncompressed texture formats.
 */
static ITextureFormat* Singleton = NULL;

class FTextureFormatUncompressedModule : public ITextureFormatModule
{
public:
	virtual ~FTextureFormatUncompressedModule()
	{
		delete Singleton;
		Singleton = NULL;
	}
	virtual ITextureFormat* GetTextureFormat()
	{
		if (!Singleton)
		{
			Singleton = new FTextureFormatUncompressed();
		}
		return Singleton;
	}
};

IMPLEMENT_MODULE(FTextureFormatUncompressedModule, TextureFormatUncompressed);

