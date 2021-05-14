// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureBuildFunction.h"

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

DEFINE_LOG_CATEGORY_STATIC(LogTextureBuildWorker, Log, All);

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

static void ReadCbField(FCbFieldView FieldView, FColor& OutColor)
{
	// Choosing the big endian ordering
	FCbFieldViewIterator ColorCbArrayIt = FieldView.AsArrayView().CreateViewIterator();
	OutColor.A = ColorCbArrayIt->AsUInt8(); ++ColorCbArrayIt;
	OutColor.R = ColorCbArrayIt->AsUInt8(); ++ColorCbArrayIt;
	OutColor.G = ColorCbArrayIt->AsUInt8(); ++ColorCbArrayIt;
	OutColor.B = ColorCbArrayIt->AsUInt8(); ++ColorCbArrayIt;
}

static void ReadCbField(FCbFieldView FieldView, FVector4& OutVec4)
{
	FCbFieldViewIterator Vec4CbArrayIt = FieldView.AsArrayView().CreateViewIterator();
	OutVec4.X = Vec4CbArrayIt->AsFloat(); ++Vec4CbArrayIt;
	OutVec4.Y = Vec4CbArrayIt->AsFloat(); ++Vec4CbArrayIt;
	OutVec4.Z = Vec4CbArrayIt->AsFloat(); ++Vec4CbArrayIt;
	OutVec4.W = Vec4CbArrayIt->AsFloat(); ++Vec4CbArrayIt;
}

static void ReadCbField(FCbFieldView FieldView, FIntPoint& OutIntPoint)
{
	FCbFieldViewIterator IntPointCbArrayIt = FieldView.AsArrayView().CreateViewIterator();
	OutIntPoint.X = IntPointCbArrayIt->AsInt32(); ++IntPointCbArrayIt;
	OutIntPoint.Y = IntPointCbArrayIt->AsInt32(); ++IntPointCbArrayIt;
}

static void ReadBuildSettingsFromCompactBinary(const FCbObject& Object, FTextureBuildSettings& OutBuildSettings)
{
	FCbObjectView ColorAdjustmentCbObj = Object.FindView("ColorAdjustment").AsObjectView();
	FColorAdjustmentParameters& ColorAdjustment = OutBuildSettings.ColorAdjustment;
	ColorAdjustment.AdjustBrightness = ColorAdjustmentCbObj.FindView("AdjustBrightness").AsFloat();
	ColorAdjustment.AdjustBrightnessCurve = ColorAdjustmentCbObj.FindView("AdjustBrightnessCurve").AsFloat();
	ColorAdjustment.AdjustSaturation = ColorAdjustmentCbObj.FindView("AdjustSaturation").AsFloat();
	ColorAdjustment.AdjustVibrance = ColorAdjustmentCbObj.FindView("AdjustVibrance").AsFloat();
	ColorAdjustment.AdjustRGBCurve = ColorAdjustmentCbObj.FindView("AdjustRGBCurve").AsFloat();
	ColorAdjustment.AdjustHue = ColorAdjustmentCbObj.FindView("AdjustHue").AsFloat();
	ColorAdjustment.AdjustMinAlpha = ColorAdjustmentCbObj.FindView("AdjustMinAlpha").AsFloat();
	ColorAdjustment.AdjustMaxAlpha = ColorAdjustmentCbObj.FindView("AdjustMaxAlpha").AsFloat();
	ReadCbField(Object.FindView("AlphaCoverageThresholds"), OutBuildSettings.AlphaCoverageThresholds);
	OutBuildSettings.MipSharpening = Object.FindView("MipSharpening").AsFloat();
	OutBuildSettings.DiffuseConvolveMipLevel = Object.FindView("DiffuseConvolveMipLevel").AsUInt32();
	OutBuildSettings.SharpenMipKernelSize = Object.FindView("SharpenMipKernelSize").AsUInt32();
	OutBuildSettings.MaxTextureResolution = Object.FindView("MaxTextureResolution").AsUInt32();
	OutBuildSettings.TextureFormatName = FName(Object.FindView("TextureFormatName").AsString());
	OutBuildSettings.bHDRSource = Object.FindView("bHDRSource").AsBool();
	OutBuildSettings.MipGenSettings = Object.FindView("MipGenSettings").AsUInt8();
	OutBuildSettings.bCubemap = Object.FindView("bCubemap").AsBool();
	OutBuildSettings.bTextureArray = Object.FindView("bTextureArray").AsBool();
	OutBuildSettings.bVolume = Object.FindView("bVolume").AsBool();
	OutBuildSettings.bLongLatSource = Object.FindView("bLongLatSource").AsBool();
	OutBuildSettings.bSRGB = Object.FindView("bSRGB").AsBool();
	OutBuildSettings.bUseLegacyGamma = Object.FindView("bUseLegacyGamma").AsBool();
	OutBuildSettings.bPreserveBorder = Object.FindView("bPreserveBorder").AsBool();
	OutBuildSettings.bForceAlphaChannel = Object.FindView("bForceAlphaChannel").AsBool();
	OutBuildSettings.bDitherMipMapAlpha = Object.FindView("bDitherMipMapAlpha").AsBool();
	OutBuildSettings.bComputeBokehAlpha = Object.FindView("bComputeBokehAlpha").AsBool();
	OutBuildSettings.bReplicateRed = Object.FindView("bReplicateRed").AsBool();
	OutBuildSettings.bReplicateAlpha = Object.FindView("bReplicateAlpha").AsBool();
	OutBuildSettings.bDownsampleWithAverage = Object.FindView("bDownsampleWithAverage").AsBool();
	OutBuildSettings.bSharpenWithoutColorShift = Object.FindView("bSharpenWithoutColorShift").AsBool();
	OutBuildSettings.bBorderColorBlack = Object.FindView("bBorderColorBlack").AsBool();
	OutBuildSettings.bFlipGreenChannel = Object.FindView("bFlipGreenChannel").AsBool();
	OutBuildSettings.bApplyYCoCgBlockScale = Object.FindView("bApplyYCoCgBlockScale").AsBool();
	OutBuildSettings.bApplyKernelToTopMip = Object.FindView("bApplyKernelToTopMip").AsBool();
	OutBuildSettings.bRenormalizeTopMip = Object.FindView("bRenormalizeTopMip").AsBool();
	OutBuildSettings.CompositeTextureMode = Object.FindView("CompositeTextureMode").AsUInt8();
	OutBuildSettings.CompositePower = Object.FindView("CompositePower").AsFloat();
	OutBuildSettings.LODBias = Object.FindView("LODBias").AsUInt32();
	OutBuildSettings.LODBiasWithCinematicMips = Object.FindView("LODBiasWithCinematicMips").AsUInt32();
	ReadCbField(Object.FindView("TopMipSize"), OutBuildSettings.TopMipSize);
	OutBuildSettings.VolumeSizeZ = Object.FindView("VolumeSizeZ").AsInt32();
	OutBuildSettings.ArraySlices = Object.FindView("ArraySlices").AsInt32();
	OutBuildSettings.bStreamable = Object.FindView("bStreamable").AsBool();
	OutBuildSettings.bVirtualStreamable = Object.FindView("bVirtualStreamable").AsBool();
	OutBuildSettings.bChromaKeyTexture = Object.FindView("bChromaKeyTexture").AsBool();
	OutBuildSettings.PowerOfTwoMode = Object.FindView("PowerOfTwoMode").AsUInt8();
	ReadCbField(Object.FindView("PaddingColor"), OutBuildSettings.PaddingColor);
	ReadCbField(Object.FindView("ChromaKeyColor"), OutBuildSettings.ChromaKeyColor);
	OutBuildSettings.ChromaKeyThreshold = Object.FindView("ChromaKeyThreshold").AsFloat();
	OutBuildSettings.CompressionQuality = Object.FindView("CompressionQuality").AsInt32();
	OutBuildSettings.LossyCompressionAmount = Object.FindView("LossyCompressionAmount").AsInt32();
	OutBuildSettings.Downscale = Object.FindView("Downscale").AsFloat();
	OutBuildSettings.DownscaleOptions = Object.FindView("DownscaleOptions").AsUInt8();
	OutBuildSettings.VirtualAddressingModeX = Object.FindView("VirtualAddressingModeX").AsInt32();
	OutBuildSettings.VirtualAddressingModeY = Object.FindView("VirtualAddressingModeY").AsInt32();
	OutBuildSettings.VirtualTextureTileSize = Object.FindView("VirtualTextureTileSize").AsInt32();
	OutBuildSettings.VirtualTextureBorderSize = Object.FindView("VirtualTextureBorderSize").AsInt32();
	OutBuildSettings.bVirtualTextureEnableCompressZlib = Object.FindView("bVirtualTextureEnableCompressZlib").AsBool();
	OutBuildSettings.bVirtualTextureEnableCompressCrunch = Object.FindView("bVirtualTextureEnableCompressCrunch").AsBool();
	OutBuildSettings.bHasEditorOnlyData = Object.FindView("bHasEditorOnlyData").AsBool();
}

static void ReadOutputSettingsFromCompactBinary(const FCbObject& Object, int32& NumInlineMips, FString& MipKeyPrefix)
{
	NumInlineMips = Object.FindView("NumInlineMips").AsInt32();
	MipKeyPrefix = FString(Object.FindView("MipKeyPrefix").AsString());
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

static void ReadTextureSourceFromCompactBinary(const FCbObject& Object, UE::DerivedData::FBuildContext& Context, TArray<FImage>& OutMips)
{
	FAnsiStringView InputKey = Object.FindView("Input").AsString();
	FString TempInputKey(InputKey);
	FSharedBuffer InputBuffer = Context.GetInput(*TempInputKey);

	ETextureSourceCompressionFormat CompressionFormat = (ETextureSourceCompressionFormat)Object.FindView("CompressionFormat").AsUInt8();
	ETextureSourceFormat SourceFormat = (ETextureSourceFormat)Object.FindView("SourceFormat").AsUInt8();

	ERawImageFormat::Type RawImageFormat = ComputeRawImageFormat(SourceFormat);

	EGammaSpace GammaSpace = (EGammaSpace)Object.FindView("GammaSpace").AsUInt8();
	int32 NumSlices = Object.FindView("NumSlices").AsInt32();
	int32 SizeX = Object.FindView("SizeX").AsInt32();
	int32 SizeY = Object.FindView("SizeY").AsInt32();
	int32 MipSizeX = SizeX;
	int32 MipSizeY = SizeY;
	FCbArrayView MipsCbArrayView = Object.FindView("Mips").AsArrayView();
	OutMips.Reserve(MipsCbArrayView.Num());
	for (FCbFieldViewIterator MipsCbArrayIt = MipsCbArrayView.CreateViewIterator(); MipsCbArrayIt; ++MipsCbArrayIt)
	{
		FCbObjectView MipCbObjectView = MipsCbArrayIt->AsObjectView();
		int64 MipOffset = MipCbObjectView.FindView("Offset").AsInt64();
		int64 MipSize = MipCbObjectView.FindView("Size").AsInt64();

		FImage* SourceMip = new(OutMips) FImage(
			MipSizeX, MipSizeY,
			NumSlices,
			RawImageFormat,
			GammaSpace
		);

		switch (CompressionFormat)
		{
		case TSCF_JPEG:
		{
			TSharedPtr<IImageWrapper> ImageWrapper = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper")).CreateImageWrapper(EImageFormat::JPEG);
			ImageWrapper->SetCompressed((const uint8*)InputBuffer.GetData() + MipOffset, MipSize);
			ImageWrapper->GetRaw(SourceFormat == TSF_G8 ? ERGBFormat::Gray : ERGBFormat::BGRA, 8, SourceMip->RawData);
		}
		break;
		case TSCF_PNG:
		{
			TSharedPtr<IImageWrapper> ImageWrapper = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper")).CreateImageWrapper(EImageFormat::PNG);
			ImageWrapper->SetCompressed((const uint8*)InputBuffer.GetData() + MipOffset, MipSize);
			ERGBFormat RawFormat = (SourceFormat == TSF_G8 || SourceFormat == TSF_G16) ? ERGBFormat::Gray : ERGBFormat::RGBA;
			ImageWrapper->GetRaw(RawFormat, (SourceFormat == TSF_G16 || SourceFormat == TSF_RGBA16) ? 16 : 8, SourceMip->RawData);
		}
		break;
		default:
			SourceMip->RawData.AddUninitialized(MipSize);
			FMemory::Memcpy(
				SourceMip->RawData.GetData(),
				(const uint8*)InputBuffer.GetData() + MipOffset,
				MipSize
			);
			break;
		}

		MipSizeX = FMath::Max(MipSizeX / 2, 1);
		MipSizeY = FMath::Max(MipSizeY / 2, 1);
	}

}

FTextureBuildFunction::FTextureBuildFunction()
: Compressor(FModuleManager::LoadModuleChecked<ITextureCompressorModule>(TEXTURE_COMPRESSOR_MODULENAME))
{
}

void FTextureBuildFunction::Build(UE::DerivedData::FBuildContext& Context) const
{
	const FCbObject BuildSettingsCbObj = Context.GetConstant(TEXT("TextureBuildSettings"));
	FTextureBuildSettings BuildSettings;
	ReadBuildSettingsFromCompactBinary(Context.GetConstant(TEXT("TextureBuildSettings")), BuildSettings);
	int32 NumInlineMips;
	FString MipKeyPrefix;
	ReadOutputSettingsFromCompactBinary(Context.GetConstant(TEXT("TextureOutputSettings")), NumInlineMips, MipKeyPrefix);

	TArray<FImage> SourceMips;
	ReadTextureSourceFromCompactBinary(Context.GetConstant(TEXT("TextureSource")), Context, SourceMips);

	TArray<FImage> AssociatedNormalSourceMips;
	const FCbObject AssociatedNormalSourceMipsCbObj = Context.GetConstant(TEXT("CompositeTextureSource"));
	if (AssociatedNormalSourceMipsCbObj)
	{
		ReadTextureSourceFromCompactBinary(AssociatedNormalSourceMipsCbObj, Context, AssociatedNormalSourceMips);
	}

	UE_LOG(LogTextureBuildWorker, Display, TEXT("Compressing %d source mip(s) (%dx%d) to %s..."), SourceMips.Num(), SourceMips[0].SizeX, SourceMips[0].SizeY, *BuildSettings.TextureFormatName.ToString());

	TArray<FCompressedImage2D> CompressedMips;
	uint32 NumMipsInTail;
	uint32 ExtData;

	Compressor.BuildTexture(SourceMips,
		AssociatedNormalSourceMips,
		BuildSettings,
		CompressedMips,
		NumMipsInTail,
		ExtData);

	// Write out streaming mips as payloads.
	int32 MipCount = CompressedMips.Num();
	const bool bForceAllMipsToBeInlined = BuildSettings.bCubemap || (BuildSettings.bVolume && !BuildSettings.bStreamable) || (BuildSettings.bTextureArray && !BuildSettings.bStreamable);
	const int32 FirstInlineMip = bForceAllMipsToBeInlined ? 0 : FMath::Max(0, MipCount - FMath::Max(NumInlineMips, (int32)NumMipsInTail));
	const int32 WritableMipCount = MipCount - ((NumMipsInTail > 0) ? ((int32)NumMipsInTail - 1) : 0);
	for (int32 MipIndex = 0; MipIndex < WritableMipCount; ++MipIndex)
	{
		if (MipIndex < FirstInlineMip)
		{
			const FCompressedImage2D& CompressedMip = CompressedMips[MipIndex];
			TStringBuilder<32> PayloadName;
			PayloadName << "Mip" << MipIndex;

			int32 MipSize = CompressedMip.RawData.Num();
			FSharedBuffer MipHeader = FSharedBuffer::MakeView(&MipSize, sizeof(int32));
			FCompositeBuffer CompositeMipBuffer(MipHeader, FSharedBuffer::MakeView(CompressedMips[MipIndex].RawData.GetData(), CompressedMips[MipIndex].RawData.Num()));

			Context.AddPayload(UE::DerivedData::FPayloadId::FromName(*PayloadName), FCompressedBuffer::Compress(NAME_None, CompositeMipBuffer));
		}
	}

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

		TArray<TArray<uint8>> MipHeaderArray;
		MipHeaderArray.AddDefaulted(MipCount);

		TArray<TArray<uint8>> MipFooterArray;
		MipFooterArray.AddDefaulted(MipCount);

		int64 CurrentOffset = TextureHeaderArray.Num();
		for (int32 MipIndex = 0; MipIndex < MipCount; ++MipIndex)
		{
			bool bIsInlineMip = MipIndex >= FirstInlineMip;
			FCompressedImage2D& CompressedMip = CompressedMips[MipIndex];

			FMemoryWriter MipHeaderWriter(MipHeaderArray[MipIndex], /*bIsPersistent=*/ true);
			bool bCooked = false;
			MipHeaderWriter << bCooked;

			uint32 BulkDataFlags = 0;
			MipHeaderWriter << BulkDataFlags;

			int32 BulkDataElementCount = bIsInlineMip ? CompressedMip.RawData.Num() : 0;
			MipHeaderWriter << BulkDataElementCount;

			int32 BulkDataBytesOnDisk = bIsInlineMip ? CompressedMip.RawData.Num() : 0;
			MipHeaderWriter << BulkDataBytesOnDisk;

			int64 BulkDataOffset = CurrentOffset + MipHeaderArray[MipIndex].Num() + sizeof(int64);
			MipHeaderWriter << BulkDataOffset;

			OrderedBuffers.Add(FSharedBuffer::MakeView(MipHeaderArray[MipIndex].GetData(), MipHeaderArray[MipIndex].Num()));
			CurrentOffset += MipHeaderArray[MipIndex].Num();

			if (bIsInlineMip)
			{
				OrderedBuffers.Add(FSharedBuffer::MakeView(CompressedMip.RawData.GetData(), CompressedMip.RawData.Num()));
				CurrentOffset += CompressedMip.RawData.Num();
			}


			FMemoryWriter MipFooterWriter(MipFooterArray[MipIndex], /*bIsPersistent=*/ true);

			MipFooterWriter << CompressedMip.SizeX;
			MipFooterWriter << CompressedMip.SizeY;
			MipFooterWriter << CompressedMip.SizeZ;

			EFileRegionType FileRegion = FFileRegion::SelectType(EPixelFormat(CompressedMip.PixelFormat));
			MipFooterWriter << FileRegion;

			TStringBuilder<128> DerivedDataKey;
			if (!bIsInlineMip)
			{
				// This has to match the behavior in GetTextureDerivedMipKey in TextureDerivedData.cpp
				DerivedDataKey << MipKeyPrefix;
				DerivedDataKey.Appendf(TEXT("_MIP%u_%dx%d"), MipIndex, CompressedMip.SizeX, CompressedMip.SizeY);
			}
			FString DerivedDataKeyFinal (DerivedDataKey.ToString());
			MipFooterWriter << DerivedDataKeyFinal;

			OrderedBuffers.Add(FSharedBuffer::MakeView(MipFooterArray[MipIndex].GetData(), MipFooterArray[MipIndex].Num()));
			CurrentOffset += MipFooterArray[MipIndex].Num();
		}

		// Bool serialized as 32-bit int is the footer  
		uint32 IsVirtual = 0;
		OrderedBuffers.Add(FSharedBuffer::MakeView(&IsVirtual, sizeof(IsVirtual)));


		FCompositeBuffer CompositeResult(OrderedBuffers);
		Context.AddPayload(UE::DerivedData::FPayloadId::FromName("Texture"), FCompressedBuffer::Compress(NAME_None, CompositeResult));
	}
}
