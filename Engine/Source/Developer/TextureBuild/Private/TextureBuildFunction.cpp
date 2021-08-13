// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureBuildFunction.h"

#include "DerivedDataCache.h"
#include "DerivedDataPayload.h"
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
#include "Serialization/FileRegions.h"
#include "Serialization/MemoryWriter.h"
#include "TextureCompressorModule.h"
#include "TextureFormatManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogTextureBuildFunction, Log, All);

// Any edits to the texture compressor or this file that will change the output of texture builds
// MUST have a corresponding change to this version. Individual texture formats have a version to
// change that is specific to the format. A merge conflict affecting the version MUST be resolved
// by generating a new version.
static const FGuid TextureDerivedDataVersion(TEXT("0b123ca4-66c4-4ea4-b269-550de0c32eb1"));

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

static void ReadCbField(FCbFieldView Field, FVector4& OutValue)
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
	BuildSettings.bHasEditorOnlyData = Object["bHasEditorOnlyData"].AsBool(BuildSettings.bHasEditorOnlyData);
	return BuildSettings;
}

static void ReadOutputSettingsFromCompactBinary(const FCbObjectView& Object, int32& NumInlineMips, FString& MipKeyPrefix)
{
	NumInlineMips = Object["NumInlineMips"].AsInt32();
	MipKeyPrefix = FUTF8ToTCHAR(Object["MipKeyPrefix"].AsString());
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
	FSharedBuffer InputBuffer = Context.FindInput(FUTF8ToTCHAR(Source.GetName()));
	if (!InputBuffer)
	{
		UE_LOG(LogTextureBuildFunction, Error, TEXT("Missing input '%s'."), *WriteToString<64>(FUTF8ToTCHAR(Source.GetName())));
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
}

void FTextureBuildFunction::Build(UE::DerivedData::FBuildContext& Context) const
{
	const FCbObject Settings = Context.FindConstant(TEXT("Settings"_SV));
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
	FString MipKeyPrefix;
	ReadOutputSettingsFromCompactBinary(Settings["Output"].AsObjectView(), NumInlineMips, MipKeyPrefix);

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

	// Write out texture platform data and non-streaming mip tail as a single payload
	// This is meant to match the behavior in SerializePlatformData in TextureDerivedData.cpp.
	// TODO: The texture header/footer information here should not be serialized from the worker but could be left to the editor to fill in.
	//		 Doing it here causes more code duplication than I would like.
	{
		bool bHasOptData = (NumMipsInTail != 0) || (ExtData != 0);
		TArray<FSharedBuffer> OrderedBuffers;

		TArray<uint8> TextureHeaderArray;
		FMemoryWriter TextureHeaderWriter(TextureHeaderArray, /*bIsPersistent=*/ true);
		TextureHeaderWriter << CompressedMips[0].SizeX;
		TextureHeaderWriter << CompressedMips[0].SizeY;

		const int32 NumSlices = BuildSettings.bCubemap ? 6 : (BuildSettings.bVolume || BuildSettings.bTextureArray) ? CompressedMips[0].SizeZ : 1;
		static constexpr uint32 BitMask_CubeMap = 1u << 31u;
		static constexpr uint32 BitMask_HasOptData = 1u << 30u;
		static constexpr uint32 BitMask_NumSlices = BitMask_HasOptData - 1u;
		uint32 PackedData = (NumSlices & BitMask_NumSlices) | (BuildSettings.bCubemap ? BitMask_CubeMap : 0) | (bHasOptData ? BitMask_HasOptData : 0);
		TextureHeaderWriter << PackedData;

		FString PixelFormatString(GetPixelFormatString((EPixelFormat)CompressedMips[0].PixelFormat));
		TextureHeaderWriter << PixelFormatString;

		if (bHasOptData)
		{
			TextureHeaderWriter << ExtData;
			TextureHeaderWriter << NumMipsInTail;
		}

		TextureHeaderWriter << MipCount;

		OrderedBuffers.Add(FSharedBuffer::MakeView(TextureHeaderArray.GetData(), TextureHeaderArray.Num()));

		int64 CurrentOffset = TextureHeaderArray.Num();
		for (int32 MipIndex = 0; MipIndex < MipCount; ++MipIndex)
		{
			bool bIsInlineMip = MipIndex >= FirstInlineMip;
			FCompressedImage2D& CompressedMip = CompressedMips[MipIndex];

			TArray<uint8> MipHeader;
			FMemoryWriter MipHeaderWriter(MipHeader, /*bIsPersistent=*/ true);
			bool bCooked = false;
			MipHeaderWriter << bCooked;

			uint32 BulkDataFlags = 0;
			MipHeaderWriter << BulkDataFlags;

			int32 BulkDataElementCount = bIsInlineMip ? CompressedMip.RawData.Num() : 0;
			MipHeaderWriter << BulkDataElementCount;

			int32 BulkDataBytesOnDisk = bIsInlineMip ? CompressedMip.RawData.Num() : 0;
			MipHeaderWriter << BulkDataBytesOnDisk;

			int64 BulkDataOffset = CurrentOffset + MipHeader.Num() + sizeof(int64);
			MipHeaderWriter << BulkDataOffset;

			CurrentOffset += MipHeader.Num();
			OrderedBuffers.Add(MakeSharedBufferFromArray(MoveTemp(MipHeader)));

			if (bIsInlineMip)
			{
				OrderedBuffers.Add(FSharedBuffer::MakeView(CompressedMip.RawData.GetData(), CompressedMip.RawData.Num()));
				CurrentOffset += CompressedMip.RawData.Num();
			}

			TArray<uint8> MipFooter;
			FMemoryWriter MipFooterWriter(MipFooter, /*bIsPersistent=*/ true);

			MipFooterWriter << CompressedMip.SizeX;
			MipFooterWriter << CompressedMip.SizeY;
			MipFooterWriter << CompressedMip.SizeZ;

			EFileRegionType FileRegion = FFileRegion::SelectType(EPixelFormat(CompressedMip.PixelFormat));
			MipFooterWriter << FileRegion;

			TStringBuilder<512> DerivedDataKey;
			if (!bIsInlineMip)
			{
				// This has to match the behavior in GetTextureDerivedMipKey in TextureDerivedData.cpp
				DerivedDataKey << MipKeyPrefix;
				DerivedDataKey.Appendf(TEXT("_MIP%u_%dx%d"), MipIndex, CompressedMip.SizeX, CompressedMip.SizeY);
			}
			FString DerivedDataKeyFinal(DerivedDataKey.ToString());
			MipFooterWriter << DerivedDataKeyFinal;

			CurrentOffset += MipFooter.Num();
			OrderedBuffers.Add(MakeSharedBufferFromArray(MoveTemp(MipFooter)));
		}

		// Bool serialized as 32-bit int is the footer
		uint32 IsVirtual = 0;
		OrderedBuffers.Add(FSharedBuffer::MakeView(&IsVirtual, sizeof(IsVirtual)));

		FCompositeBuffer CompositeResult(OrderedBuffers);
		Context.AddPayload(UE::DerivedData::FPayloadId::FromName(TEXT("Texture"_SV)), CompositeResult);
	}

	// Write out streaming mips as payloads.
	const int32 WritableMipCount = MipCount - ((NumMipsInTail > 0) ? ((int32)NumMipsInTail - 1) : 0);
	for (int32 MipIndex = 0; MipIndex < WritableMipCount; ++MipIndex)
	{
		if (MipIndex < FirstInlineMip)
		{
			FCompressedImage2D& CompressedMip = CompressedMips[MipIndex];
			TStringBuilder<32> PayloadName;
			PayloadName << "Mip" << MipIndex;

			FSharedBuffer MipData = MakeSharedBufferFromArray(MoveTemp(CompressedMip.RawData));
			Context.AddPayload(UE::DerivedData::FPayloadId::FromName(*PayloadName), MipData);
		}
	}
}
