// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureBuildFunction.h"

#include "DerivedDataCache.h"
#include "DerivedDataValueId.h"
#include "Engine/TextureDefines.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "ImageCore.h"
#include "Interfaces/ITextureFormat.h"
#include "Interfaces/ITextureFormatModule.h"
#include "Memory/CompositeBuffer.h"
#include "Memory/SharedBuffer.h"
#include "Modules/ModuleManager.h"
#include "PixelFormat.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinaryWriter.h"
#include "Serialization/FileRegions.h"
#include "Serialization/MemoryWriter.h"
#include "TextureCompressorModule.h"
#include "TextureFormatManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogTextureBuildFunction, Log, All);

// Any edits to the texture compressor or this file that will change the output of texture builds
// MUST have a corresponding change to this version. Individual texture formats have a version to
// change that is specific to the format. A merge conflict affecting the version MUST be resolved
// by generating a new version.
static const FGuid TextureDerivedDataVersion(TEXT("19ffd7ab-ea5c-4a18-b6f4-6b0b5eedd606"));

#ifndef CASE_ENUM_TO_TEXT
#define CASE_ENUM_TO_TEXT(txt) case txt: return TEXT(#txt);
#endif

static const TCHAR* GetPixelFormatString(EPixelFormat InPixelFormat)
{
	switch (InPixelFormat)
	{
		FOREACH_ENUM_EPIXELFORMAT(CASE_ENUM_TO_TEXT)
	default:
		return TEXT("PF_Unknown");
	}
}

static void ReadCbField(FCbFieldView Field, bool& OutValue) { OutValue = Field.AsBool(OutValue); }
static void ReadCbField(FCbFieldView Field, int32& OutValue) { OutValue = Field.AsInt32(OutValue); }
static void ReadCbField(FCbFieldView Field, uint8& OutValue) { OutValue = Field.AsUInt8(OutValue); }
static void ReadCbField(FCbFieldView Field, uint32& OutValue) { OutValue = Field.AsUInt32(OutValue); }
static void ReadCbField(FCbFieldView Field, float& OutValue) { OutValue = Field.AsFloat(OutValue); }

static void ReadCbField(FCbFieldView Field, FName& OutValue)
{
	if (Field.IsString())
	{
		OutValue = FName(FUTF8ToTCHAR(Field.AsString()));
	}
}

static void ReadCbField(FCbFieldView Field, FColor& OutValue)
{
	FCbFieldViewIterator It = Field.AsArrayView().CreateViewIterator();
	OutValue.A = It++->AsUInt8(OutValue.A);
	OutValue.R = It++->AsUInt8(OutValue.R);
	OutValue.G = It++->AsUInt8(OutValue.G);
	OutValue.B = It++->AsUInt8(OutValue.B);
}

static void ReadCbField(FCbFieldView Field, FVector2f& OutValue)
{
	FCbFieldViewIterator It = Field.AsArrayView().CreateViewIterator();
	OutValue.X = It++->AsFloat(OutValue.X);
	OutValue.Y = It++->AsFloat(OutValue.Y);
}

static void ReadCbField(FCbFieldView Field, FVector4f& OutValue)
{
	FCbFieldViewIterator It = Field.AsArrayView().CreateViewIterator();
	OutValue.X = It++->AsFloat(OutValue.X);
	OutValue.Y = It++->AsFloat(OutValue.Y);
	OutValue.Z = It++->AsFloat(OutValue.Z);
	OutValue.W = It++->AsFloat(OutValue.W);
}

static void ReadCbField(FCbFieldView Field, FIntPoint& OutValue)
{
	FCbFieldViewIterator It = Field.AsArrayView().CreateViewIterator();
	OutValue.X = It++->AsInt32(OutValue.X);
	OutValue.Y = It++->AsInt32(OutValue.Y);
}

static FTextureBuildSettings ReadBuildSettingsFromCompactBinary(const FCbObjectView& Object)
{
	FTextureBuildSettings BuildSettings;
	BuildSettings.FormatConfigOverride = Object["FormatConfigOverride"].AsObjectView();
	FCbObjectView ColorAdjustmentCbObj = Object["ColorAdjustment"].AsObjectView();
	FColorAdjustmentParameters& ColorAdjustment = BuildSettings.ColorAdjustment;
	ReadCbField(ColorAdjustmentCbObj["AdjustBrightness"], ColorAdjustment.AdjustBrightness);
	ReadCbField(ColorAdjustmentCbObj["AdjustBrightnessCurve"], ColorAdjustment.AdjustBrightnessCurve);
	ReadCbField(ColorAdjustmentCbObj["AdjustSaturation"], ColorAdjustment.AdjustSaturation);
	ReadCbField(ColorAdjustmentCbObj["AdjustVibrance"], ColorAdjustment.AdjustVibrance);
	ReadCbField(ColorAdjustmentCbObj["AdjustRGBCurve"], ColorAdjustment.AdjustRGBCurve);
	ReadCbField(ColorAdjustmentCbObj["AdjustHue"], ColorAdjustment.AdjustHue);
	ReadCbField(ColorAdjustmentCbObj["AdjustMinAlpha"], ColorAdjustment.AdjustMinAlpha);
	ReadCbField(ColorAdjustmentCbObj["AdjustMaxAlpha"], ColorAdjustment.AdjustMaxAlpha);
	BuildSettings.bDoScaleMipsForAlphaCoverage = Object["bDoScaleMipsForAlphaCoverage"].AsBool(BuildSettings.bDoScaleMipsForAlphaCoverage);
	ReadCbField(Object["AlphaCoverageThresholds"], BuildSettings.AlphaCoverageThresholds);
	ReadCbField(Object["MipSharpening"], BuildSettings.MipSharpening);
	ReadCbField(Object["DiffuseConvolveMipLevel"], BuildSettings.DiffuseConvolveMipLevel);
	ReadCbField(Object["SharpenMipKernelSize"], BuildSettings.SharpenMipKernelSize);
	ReadCbField(Object["MaxTextureResolution"], BuildSettings.MaxTextureResolution);
	ReadCbField(Object["TextureFormatName"], BuildSettings.TextureFormatName);
	ReadCbField(Object["bHDRSource"], BuildSettings.bHDRSource);
	ReadCbField(Object["MipGenSettings"], BuildSettings.MipGenSettings);
	BuildSettings.bCubemap = Object["bCubemap"].AsBool(BuildSettings.bCubemap);
	BuildSettings.bTextureArray = Object["bTextureArray"].AsBool(BuildSettings.bTextureArray);
	BuildSettings.bVolume = Object["bVolume"].AsBool(BuildSettings.bVolume);
	BuildSettings.bLongLatSource = Object["bLongLatSource"].AsBool(BuildSettings.bLongLatSource);
	BuildSettings.bSRGB = Object["bSRGB"].AsBool(BuildSettings.bSRGB);
	ReadCbField(Object["SourceEncodingOverride"], BuildSettings.SourceEncodingOverride);
	BuildSettings.bHasColorSpaceDefinition = Object["bHasColorSpaceDefinition"].AsBool(BuildSettings.bHasColorSpaceDefinition);
	ReadCbField(Object["RedChromaticityCoordinate"], BuildSettings.RedChromaticityCoordinate);
	ReadCbField(Object["GreenChromaticityCoordinate"], BuildSettings.GreenChromaticityCoordinate);
	ReadCbField(Object["BlueChromaticityCoordinate"], BuildSettings.BlueChromaticityCoordinate);
	ReadCbField(Object["WhiteChromaticityCoordinate"], BuildSettings.WhiteChromaticityCoordinate);
	ReadCbField(Object["ChromaticAdaptationMethod"], BuildSettings.ChromaticAdaptationMethod);
	BuildSettings.bUseLegacyGamma = Object["bUseLegacyGamma"].AsBool(BuildSettings.bUseLegacyGamma);
	BuildSettings.bPreserveBorder = Object["bPreserveBorder"].AsBool(BuildSettings.bPreserveBorder);
	BuildSettings.bForceNoAlphaChannel = Object["bForceNoAlphaChannel"].AsBool(BuildSettings.bForceNoAlphaChannel);
	BuildSettings.bForceAlphaChannel = Object["bForceAlphaChannel"].AsBool(BuildSettings.bForceAlphaChannel);
	BuildSettings.bDitherMipMapAlpha = Object["bDitherMipMapAlpha"].AsBool(BuildSettings.bDitherMipMapAlpha);
	BuildSettings.bComputeBokehAlpha = Object["bComputeBokehAlpha"].AsBool(BuildSettings.bComputeBokehAlpha);
	BuildSettings.bReplicateRed = Object["bReplicateRed"].AsBool(BuildSettings.bReplicateRed);
	BuildSettings.bReplicateAlpha = Object["bReplicateAlpha"].AsBool(BuildSettings.bReplicateAlpha);
	BuildSettings.bDownsampleWithAverage = Object["bDownsampleWithAverage"].AsBool(BuildSettings.bDownsampleWithAverage);
	BuildSettings.bSharpenWithoutColorShift = Object["bSharpenWithoutColorShift"].AsBool(BuildSettings.bSharpenWithoutColorShift);
	BuildSettings.bBorderColorBlack = Object["bBorderColorBlack"].AsBool(BuildSettings.bBorderColorBlack);
	BuildSettings.bFlipGreenChannel = Object["bFlipGreenChannel"].AsBool(BuildSettings.bFlipGreenChannel);
	BuildSettings.bApplyYCoCgBlockScale = Object["bApplyYCoCgBlockScale"].AsBool(BuildSettings.bApplyYCoCgBlockScale);
	BuildSettings.bApplyKernelToTopMip = Object["bApplyKernelToTopMip"].AsBool(BuildSettings.bApplyKernelToTopMip);
	BuildSettings.bRenormalizeTopMip = Object["bRenormalizeTopMip"].AsBool(BuildSettings.bRenormalizeTopMip);
	ReadCbField(Object["CompositeTextureMode"], BuildSettings.CompositeTextureMode);
	ReadCbField(Object["CompositePower"], BuildSettings.CompositePower);
	ReadCbField(Object["LODBias"], BuildSettings.LODBias);
	ReadCbField(Object["LODBiasWithCinematicMips"], BuildSettings.LODBiasWithCinematicMips);
	ReadCbField(Object["TopMipSize"], BuildSettings.TopMipSize);
	ReadCbField(Object["VolumeSizeZ"], BuildSettings.VolumeSizeZ);
	ReadCbField(Object["ArraySlices"], BuildSettings.ArraySlices);
	BuildSettings.bStreamable = Object["bStreamable"].AsBool(BuildSettings.bStreamable);
	BuildSettings.bVirtualStreamable = Object["bVirtualStreamable"].AsBool(BuildSettings.bVirtualStreamable);
	BuildSettings.bChromaKeyTexture = Object["bChromaKeyTexture"].AsBool(BuildSettings.bChromaKeyTexture);
	ReadCbField(Object["PowerOfTwoMode"], BuildSettings.PowerOfTwoMode);
	ReadCbField(Object["PaddingColor"], BuildSettings.PaddingColor);
	ReadCbField(Object["ChromaKeyColor"], BuildSettings.ChromaKeyColor);
	ReadCbField(Object["ChromaKeyThreshold"], BuildSettings.ChromaKeyThreshold);
	ReadCbField(Object["CompressionQuality"], BuildSettings.CompressionQuality);
	ReadCbField(Object["LossyCompressionAmount"], BuildSettings.LossyCompressionAmount);
	ReadCbField(Object["Downscale"], BuildSettings.Downscale);
	ReadCbField(Object["DownscaleOptions"], BuildSettings.DownscaleOptions);
	ReadCbField(Object["VirtualAddressingModeX"], BuildSettings.VirtualAddressingModeX);
	ReadCbField(Object["VirtualAddressingModeY"], BuildSettings.VirtualAddressingModeY);
	ReadCbField(Object["VirtualTextureTileSize"], BuildSettings.VirtualTextureTileSize);
	ReadCbField(Object["VirtualTextureBorderSize"], BuildSettings.VirtualTextureBorderSize);
	BuildSettings.bVirtualTextureEnableCompressZlib = Object["bVirtualTextureEnableCompressZlib"].AsBool(BuildSettings.bVirtualTextureEnableCompressZlib);
	BuildSettings.bVirtualTextureEnableCompressCrunch = Object["bVirtualTextureEnableCompressCrunch"].AsBool(BuildSettings.bVirtualTextureEnableCompressCrunch);
	BuildSettings.OodleEncodeEffort = Object["OodleEncodeEffort"].AsUInt8(BuildSettings.OodleEncodeEffort);
	BuildSettings.OodleUniversalTiling = Object["OodleUniversalTiling"].AsUInt8(BuildSettings.OodleUniversalTiling);
	BuildSettings.bOodleUsesRDO = Object["bOodleUsesRDO"].AsBool(BuildSettings.bOodleUsesRDO);
	BuildSettings.OodleRDO = Object["OodleRDO"].AsUInt8(BuildSettings.OodleRDO);
	ReadCbField(Object["OodleTextureSdkVersion"], BuildSettings.OodleTextureSdkVersion);

	return BuildSettings;
}

static void ReadOutputSettingsFromCompactBinary(const FCbObjectView& Object, int32& NumInlineMips)
{
	NumInlineMips = Object["NumInlineMips"].AsInt32();
}

static ERawImageFormat::Type ComputeRawImageFormat(ETextureSourceFormat SourceFormat)
{
	switch (SourceFormat)
	{
	case TSF_G8:		return ERawImageFormat::G8;
	case TSF_G16:		return ERawImageFormat::G16;
	case TSF_BGRA8:		return ERawImageFormat::BGRA8;
	case TSF_BGRE8:		return ERawImageFormat::BGRE8;
	case TSF_RGBA16:	return ERawImageFormat::RGBA16;
	case TSF_RGBA16F:	return ERawImageFormat::RGBA16F;
	default:
		checkf(false, TEXT("Invalid source texture format encountered: %d."), SourceFormat);
		return ERawImageFormat::G8;
	}
}

static bool TryReadTextureSourceFromCompactBinary(FCbFieldView Source, UE::DerivedData::FBuildContext& Context, TArray<FImage>& OutMips)
{
	FSharedBuffer InputBuffer = Context.FindInput(Source.GetName());
	if (!InputBuffer)
	{
		UE_LOG(LogTextureBuildFunction, Error, TEXT("Missing input '%s'."), *WriteToString<64>(Source.GetName()));
		return false;
	}
	if ( InputBuffer.GetSize() == 0 )
	{
		UE_LOG(LogTextureBuildFunction, Error, TEXT("Input size zero '%s'."), *WriteToString<64>(Source.GetName()));
		return false;
	}

	ETextureSourceCompressionFormat CompressionFormat = (ETextureSourceCompressionFormat)Source["CompressionFormat"].AsUInt8();
	ETextureSourceFormat SourceFormat = (ETextureSourceFormat)Source["SourceFormat"].AsUInt8();

	ERawImageFormat::Type RawImageFormat = ComputeRawImageFormat(SourceFormat);

	EGammaSpace GammaSpace = (EGammaSpace)Source["GammaSpace"].AsUInt8();
	int32 NumSlices = Source["NumSlices"].AsInt32();
	int32 SizeX = Source["SizeX"].AsInt32();
	int32 SizeY = Source["SizeY"].AsInt32();
	int32 MipSizeX = SizeX;
	int32 MipSizeY = SizeY;

	const uint8* DecompressedSourceData = (const uint8*)InputBuffer.GetData();
	TArray64<uint8> IntermediateDecompressedData;
	if (CompressionFormat != TSCF_None)
	{
		switch (CompressionFormat)
		{
		case TSCF_JPEG:
		{
			TSharedPtr<IImageWrapper> ImageWrapper = FModuleManager::GetModuleChecked<IImageWrapperModule>(FName("ImageWrapper")).CreateImageWrapper(EImageFormat::JPEG);
			ImageWrapper->SetCompressed((const uint8*)InputBuffer.GetData(), InputBuffer.GetSize());
			ImageWrapper->GetRaw(SourceFormat == TSF_G8 ? ERGBFormat::Gray : ERGBFormat::BGRA, 8, IntermediateDecompressedData);
		}
		break;
		case TSCF_PNG:
		{
			TSharedPtr<IImageWrapper> ImageWrapper = FModuleManager::GetModuleChecked<IImageWrapperModule>(FName("ImageWrapper")).CreateImageWrapper(EImageFormat::PNG);
			ImageWrapper->SetCompressed((const uint8*)InputBuffer.GetData(), InputBuffer.GetSize());
			ERGBFormat RawFormat = (SourceFormat == TSF_G8 || SourceFormat == TSF_G16) ? ERGBFormat::Gray : ERGBFormat::RGBA;
			ImageWrapper->GetRaw(RawFormat, (SourceFormat == TSF_G16 || SourceFormat == TSF_RGBA16) ? 16 : 8, IntermediateDecompressedData);
		}
		break;
		default:
			UE_LOG(LogTextureBuildFunction, Error, TEXT("Unexpected source compression format encountered while attempting to build a texture."));
			return false;
		}
		DecompressedSourceData = IntermediateDecompressedData.GetData();
		InputBuffer.Reset();
	}

	FCbArrayView MipsCbArrayView = Source["Mips"].AsArrayView();
	OutMips.Reserve(MipsCbArrayView.Num());
	for (FCbFieldView MipsCbArrayIt : MipsCbArrayView)
	{
		FCbObjectView MipCbObjectView = MipsCbArrayIt.AsObjectView();
		int64 MipOffset = MipCbObjectView["Offset"].AsInt64();
		int64 MipSize = MipCbObjectView["Size"].AsInt64();

		FImage* SourceMip = new(OutMips) FImage(
			MipSizeX, MipSizeY,
			NumSlices,
			RawImageFormat,
			GammaSpace
		);

		if ((MipsCbArrayView.Num() == 1) && (CompressionFormat != TSCF_None))
		{
			// In the case where there is only one mip and its already in a TArray, there is no need to allocate new array contents, just use a move instead
			SourceMip->RawData = MoveTemp(IntermediateDecompressedData);
		}
		else
		{
			SourceMip->RawData.AddUninitialized(MipSize);
			FMemory::Memcpy(
				SourceMip->RawData.GetData(),
				DecompressedSourceData + MipOffset,
				MipSize
			);
		}

		MipSizeX = FMath::Max(MipSizeX / 2, 1);
		MipSizeY = FMath::Max(MipSizeY / 2, 1);
	}

	return true;
}

// Estimate peak memory usage required to cook this texture, excluding raw input / source data
// Similar to UTexture::GetBuildRequiredMemory()
// This is a rough blackbox estimate that can be improved
static uint64 EstimateTextureBuildMemoryUsage(const FCbObject& Settings)
{
	uint64 InputSize = 0;
	for (FCbField Mip : Settings["Source"]["Mips"])
	{
		InputSize += Mip["Size"].AsInt64();
	}

	return static_cast<uint64>(InputSize * 7.5);
}

FGuid FTextureBuildFunction::GetVersion() const
{
	UE::DerivedData::FBuildVersionBuilder Builder;
	Builder << TextureDerivedDataVersion;
	ITextureFormat* TextureFormat = nullptr;
	GetVersion(Builder, TextureFormat);
	if (TextureFormat)
	{
		TArray<FName> SupportedFormats;
		TextureFormat->GetSupportedFormats(SupportedFormats);
		TArray<uint16> SupportedFormatVersions;
		for (const FName& SupportedFormat : SupportedFormats)
		{
			SupportedFormatVersions.AddUnique(TextureFormat->GetVersion(SupportedFormat));
		}
		SupportedFormatVersions.Sort();
		Builder << SupportedFormatVersions;
	}
	return Builder.Build();
}

void FTextureBuildFunction::Configure(UE::DerivedData::FBuildConfigContext& Context) const
{
	Context.SetCacheBucket(UE::DerivedData::FCacheBucket("Texture"_ASV));
	Context.SetRequiredMemory(EstimateTextureBuildMemoryUsage(Context.FindConstant(UTF8TEXTVIEW("Settings"))));
}

void FTextureBuildFunction::Build(UE::DerivedData::FBuildContext& Context) const
{
	const FCbObject Settings = Context.FindConstant(UTF8TEXTVIEW("Settings"));
	if (!Settings)
	{
		UE_LOG(LogTextureBuildFunction, Error, TEXT("Settings are not available."));
		return;
	}

	const FTextureBuildSettings BuildSettings = ReadBuildSettingsFromCompactBinary(Settings["Build"].AsObjectView());

	const uint16 RequiredTextureFormatVersion = Settings["FormatVersion"].AsUInt16();
	if (ITextureFormatManagerModule* TFM = GetTextureFormatManager())
	{
		const ITextureFormat* TextureFormat = TFM->FindTextureFormat(BuildSettings.TextureFormatName);
		const uint16 CurrentTextureFormatVersion = TextureFormat ? TextureFormat->GetVersion(BuildSettings.TextureFormatName, &BuildSettings) : 0;
		if (CurrentTextureFormatVersion != RequiredTextureFormatVersion)
		{
			UE_LOG(LogTextureBuildFunction, Error, TEXT("%s has version %hu when version %hu is required."),
				*BuildSettings.TextureFormatName.ToString(), CurrentTextureFormatVersion, RequiredTextureFormatVersion);;
			return;
		}
	}

	int32 NumInlineMips;
	ReadOutputSettingsFromCompactBinary(Settings["Output"].AsObjectView(), NumInlineMips);

	TArray<FImage> SourceMips;
	if (!TryReadTextureSourceFromCompactBinary(Settings["Source"], Context, SourceMips))
	{
		return;
	}

	TArray<FImage> AssociatedNormalSourceMips;
	if (FCbFieldView CompositeSource = Settings["CompositeSource"];
		CompositeSource && !TryReadTextureSourceFromCompactBinary(CompositeSource, Context, AssociatedNormalSourceMips))
	{
		return;
	}

	UE_LOG(LogTextureBuildFunction, Display, TEXT("Compressing %d source mip(s) (%dx%d) to %s..."), SourceMips.Num(), SourceMips[0].SizeX, SourceMips[0].SizeY, *BuildSettings.TextureFormatName.ToString());

	TArray<FCompressedImage2D> CompressedMips;
	uint32 NumMipsInTail;
	uint32 ExtData;

	bool bBuildSucceeded = FModuleManager::GetModuleChecked<ITextureCompressorModule>(TEXTURE_COMPRESSOR_MODULENAME).BuildTexture(
		SourceMips,
		AssociatedNormalSourceMips,
		BuildSettings,
		Context.GetName(),
		CompressedMips,
		NumMipsInTail,
		ExtData);
	if (!bBuildSucceeded)
	{
		return;
	}
	check(CompressedMips.Num() > 0);

	int32 MipCount = CompressedMips.Num();
	const bool bForceAllMipsToBeInlined = BuildSettings.bCubemap || (BuildSettings.bVolume && !BuildSettings.bStreamable) || (BuildSettings.bTextureArray && !BuildSettings.bStreamable);
	const int32 FirstInlineMip = bForceAllMipsToBeInlined ? 0 : FMath::Max(0, MipCount - FMath::Max(NumInlineMips, (int32)NumMipsInTail));

	{
		FCbWriter DescriptionWriter;
		DescriptionWriter.BeginObject();

		DescriptionWriter.BeginArray("Size"_ASV);
		DescriptionWriter.AddInteger(CompressedMips[0].SizeX);
		DescriptionWriter.AddInteger(CompressedMips[0].SizeY);
		const int32 NumSlices = (BuildSettings.bVolume || BuildSettings.bTextureArray) ? CompressedMips[0].SizeZ : BuildSettings.bCubemap ? 6 : 1;
		DescriptionWriter.AddInteger(NumSlices);
		DescriptionWriter.EndArray();

		DescriptionWriter.AddString("PixelFormat"_ASV, GetPixelFormatString((EPixelFormat)CompressedMips[0].PixelFormat));
		DescriptionWriter.AddBool("bCubeMap"_ASV, BuildSettings.bCubemap);
		DescriptionWriter.AddInteger("ExtData"_ASV, ExtData);
		DescriptionWriter.AddInteger("NumMips"_ASV, MipCount);
		DescriptionWriter.AddInteger("NumStreamingMips"_ASV, FirstInlineMip);
		DescriptionWriter.AddInteger("NumMipsInTail"_ASV, NumMipsInTail);

		DescriptionWriter.BeginArray("Mips"_ASV);
		int64 MipOffset = 0;
		for (int32 MipIndex = 0; MipIndex < MipCount; ++MipIndex)
		{
			FCompressedImage2D& CompressedMip = CompressedMips[MipIndex];

			DescriptionWriter.BeginObject();
			
			DescriptionWriter.BeginArray("Size"_ASV);
			DescriptionWriter.AddInteger(CompressedMip.SizeX);
			DescriptionWriter.AddInteger(CompressedMip.SizeY);
			DescriptionWriter.AddInteger(CompressedMip.SizeZ);
			DescriptionWriter.EndArray();

			const bool bIsInlineMip = MipIndex >= FirstInlineMip;

			EFileRegionType FileRegion = FFileRegion::SelectType(EPixelFormat(CompressedMip.PixelFormat));
			DescriptionWriter.AddInteger("FileRegion"_ASV, static_cast<int32>(FileRegion));
			if (bIsInlineMip)
			{
				DescriptionWriter.AddInteger("MipOffset"_ASV, MipOffset);
			}
			DescriptionWriter.AddInteger("NumBytes"_ASV, CompressedMip.RawData.Num());

			DescriptionWriter.EndObject();

			if (bIsInlineMip)
			{
				MipOffset += CompressedMip.RawData.Num();
			}
		}
		DescriptionWriter.EndArray();

		DescriptionWriter.EndObject();
		FCbObject DescriptionObject = DescriptionWriter.Save().AsObject();
		Context.AddValue(UE::DerivedData::FValueId::FromName("Description"_ASV), DescriptionObject);
	}

	// Streaming mips
	for (int32 MipIndex = 0; MipIndex < FirstInlineMip; ++MipIndex)
	{
		TAnsiStringBuilder<16> MipName;
		MipName << "Mip"_ASV << MipIndex;

		FSharedBuffer MipData = MakeSharedBufferFromArray(MoveTemp(CompressedMips[MipIndex].RawData));
		Context.AddValue(UE::DerivedData::FValueId::FromName(MipName), MipData);
	}

	// Mip tail
	TArray<FSharedBuffer> MipTailComponents;
	for (int32 MipIndex = FirstInlineMip; MipIndex < MipCount; ++MipIndex)
	{
		FSharedBuffer MipData = MakeSharedBufferFromArray(MoveTemp(CompressedMips[MipIndex].RawData));
		MipTailComponents.Add(MipData);
	}
	FCompositeBuffer MipTail(MipTailComponents);
	if (MipTail.GetSize() > 0)
	{
		Context.AddValue(UE::DerivedData::FValueId::FromName("MipTail"_ASV), MipTail);
	}
}
