// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureDerivedDataBuildUtils.h"

#if WITH_EDITOR
#include "Engine/Texture.h"
#include "Interfaces/ITextureFormat.h"
#include "Interfaces/ITextureFormat.h"
#include "Interfaces/ITextureFormatManagerModule.h"
#include "Interfaces/ITextureFormatModule.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinaryWriter.h"
#include "TextureCompressorModule.h"
#include "TextureFormatManager.h"
#include "TextureResource.h"

void GetTextureDerivedDataKeyFromSuffix(const FString& KeySuffix, FString& OutKey);
void GetTextureDerivedMipKey(int32 MipIndex, const FTexture2DMipMap& Mip, const FString& KeySuffix, FString& OutKey);

static void WriteCbField(FCbWriter& Writer, const FAnsiStringView& Name, const FColor& Color)
{
	// Choosing the big endian ordering
	Writer.BeginArray(Name);
	Writer.AddInteger(Color.A);
	Writer.AddInteger(Color.R);
	Writer.AddInteger(Color.G);
	Writer.AddInteger(Color.B);
	Writer.EndArray();
}

static void WriteCbField(FCbWriter& Writer, const FAnsiStringView& Name, const FVector4& Vec4)
{
	Writer.BeginArray(Name);
	Writer.AddFloat(Vec4.X);
	Writer.AddFloat(Vec4.Y);
	Writer.AddFloat(Vec4.Z);
	Writer.AddFloat(Vec4.W);
	Writer.EndArray();
}

static void WriteCbField(FCbWriter& Writer, const FAnsiStringView& Name, const FIntPoint& IntPoint)
{
	Writer.BeginArray(Name);
	Writer.AddInteger(IntPoint.X);
	Writer.AddInteger(IntPoint.Y);
	Writer.EndArray();
}

static FCbObject WriteBuildSettingsToCompactBinary(const FTextureBuildSettings& BuildSettings, const ITextureFormat* TextureFormat)
{
	FCbWriter Writer;
	Writer.BeginObject();

	if (BuildSettings.FormatConfigOverride)
	{
		Writer.AddObject("FormatConfigOverride", BuildSettings.FormatConfigOverride);
	}
	else if (FCbObject TextureFormatConfig = TextureFormat->ExportGlobalFormatConfig(BuildSettings))
	{
		Writer.AddObject("FormatConfigOverride", TextureFormatConfig);
	}
	Writer.BeginObject("ColorAdjustment");
	Writer.AddFloat("AdjustBrightness", BuildSettings.ColorAdjustment.AdjustBrightness);
	Writer.AddFloat("AdjustBrightnessCurve", BuildSettings.ColorAdjustment.AdjustBrightnessCurve);
	Writer.AddFloat("AdjustSaturation", BuildSettings.ColorAdjustment.AdjustSaturation);
	Writer.AddFloat("AdjustVibrance", BuildSettings.ColorAdjustment.AdjustVibrance);
	Writer.AddFloat("AdjustRGBCurve", BuildSettings.ColorAdjustment.AdjustRGBCurve);
	Writer.AddFloat("AdjustHue", BuildSettings.ColorAdjustment.AdjustHue);
	Writer.AddFloat("AdjustMinAlpha", BuildSettings.ColorAdjustment.AdjustMinAlpha);
	Writer.AddFloat("AdjustMaxAlpha", BuildSettings.ColorAdjustment.AdjustMaxAlpha);
	Writer.EndObject();

	WriteCbField(Writer, "AlphaCoverageThresholds", BuildSettings.AlphaCoverageThresholds);

	Writer.AddFloat("MipSharpening", BuildSettings.MipSharpening);
	Writer.AddInteger("DiffuseConvolveMipLevel", BuildSettings.DiffuseConvolveMipLevel);
	Writer.AddInteger("SharpenMipKernelSize", BuildSettings.SharpenMipKernelSize);
	Writer.AddInteger("MaxTextureResolution", BuildSettings.MaxTextureResolution);
	Writer.AddString("TextureFormatName", WriteToString<64>(BuildSettings.TextureFormatName));
	Writer.AddBool("bHDRSource", BuildSettings.bHDRSource);
	Writer.AddInteger("MipGenSettings", BuildSettings.MipGenSettings);
	Writer.AddBool("bCubemap", BuildSettings.bCubemap);
	Writer.AddBool("bTextureArray", BuildSettings.bTextureArray);
	Writer.AddBool("bVolume", BuildSettings.bVolume);
	Writer.AddBool("bLongLatSource", BuildSettings.bLongLatSource);
	Writer.AddBool("bSRGB", BuildSettings.bSRGB);
	Writer.AddBool("bUseLegacyGamma", BuildSettings.bUseLegacyGamma);
	Writer.AddBool("bPreserveBorder", BuildSettings.bPreserveBorder);
	Writer.AddBool("bForceNoAlphaChannel", BuildSettings.bForceNoAlphaChannel);
	Writer.AddBool("bForceAlphaChannel", BuildSettings.bForceAlphaChannel);
	Writer.AddBool("bDitherMipMapAlpha", BuildSettings.bDitherMipMapAlpha);
	Writer.AddBool("bComputeBokehAlpha", BuildSettings.bComputeBokehAlpha);
	Writer.AddBool("bReplicateRed", BuildSettings.bReplicateRed);
	Writer.AddBool("bReplicateAlpha", BuildSettings.bReplicateAlpha);
	Writer.AddBool("bDownsampleWithAverage", BuildSettings.bDownsampleWithAverage);
	Writer.AddBool("bSharpenWithoutColorShift", BuildSettings.bSharpenWithoutColorShift);
	Writer.AddBool("bBorderColorBlack", BuildSettings.bBorderColorBlack);
	Writer.AddBool("bFlipGreenChannel", BuildSettings.bFlipGreenChannel);
	Writer.AddBool("bApplyYCoCgBlockScale", BuildSettings.bApplyYCoCgBlockScale);
	Writer.AddBool("bApplyKernelToTopMip", BuildSettings.bApplyKernelToTopMip);
	Writer.AddBool("bRenormalizeTopMip", BuildSettings.bRenormalizeTopMip);
	Writer.AddInteger("CompositeTextureMode", BuildSettings.CompositeTextureMode);
	Writer.AddFloat("CompositePower", BuildSettings.CompositePower);
	Writer.AddInteger("LODBias", BuildSettings.LODBias);
	Writer.AddInteger("LODBiasWithCinematicMips", BuildSettings.LODBiasWithCinematicMips);

	WriteCbField(Writer, "TopMipSize", BuildSettings.TopMipSize);

	Writer.AddInteger("VolumeSizeZ", BuildSettings.VolumeSizeZ);
	Writer.AddInteger("ArraySlices", BuildSettings.ArraySlices);
	Writer.AddBool("bStreamable", BuildSettings.bStreamable);
	Writer.AddBool("bVirtualStreamable", BuildSettings.bVirtualStreamable);
	Writer.AddBool("bChromaKeyTexture", BuildSettings.bChromaKeyTexture);
	Writer.AddInteger("PowerOfTwoMode", BuildSettings.PowerOfTwoMode);
	WriteCbField(Writer, "PaddingColor", BuildSettings.PaddingColor);
	WriteCbField(Writer, "ChromaKeyColor", BuildSettings.ChromaKeyColor);
	Writer.AddFloat("ChromaKeyThreshold", BuildSettings.ChromaKeyThreshold);
	Writer.AddInteger("CompressionQuality", BuildSettings.CompressionQuality);
	Writer.AddInteger("LossyCompressionAmount", BuildSettings.LossyCompressionAmount);
	Writer.AddFloat("Downscale", BuildSettings.Downscale);
	Writer.AddInteger("DownscaleOptions", BuildSettings.DownscaleOptions);
	Writer.AddInteger("VirtualAddressingModeX", BuildSettings.VirtualAddressingModeX);
	Writer.AddInteger("VirtualAddressingModeY", BuildSettings.VirtualAddressingModeY);
	Writer.AddInteger("VirtualTextureTileSize", BuildSettings.VirtualTextureTileSize);
	Writer.AddInteger("VirtualTextureBorderSize", BuildSettings.VirtualTextureBorderSize);
	Writer.AddBool("bVirtualTextureEnableCompressZlib", BuildSettings.bVirtualTextureEnableCompressZlib);
	Writer.AddBool("bVirtualTextureEnableCompressCrunch", BuildSettings.bVirtualTextureEnableCompressCrunch);
	Writer.AddBool("bHasEditorOnlyData", BuildSettings.bHasEditorOnlyData);

	Writer.EndObject();
	return Writer.Save().AsObject();
}

static FCbObject WriteOutputSettingsToCompactBinary(int32 NumInlineMips, const FString& KeySuffix)
{
	FCbWriter Writer;
	Writer.BeginObject();

	Writer.AddInteger("NumInlineMips", NumInlineMips);
	
	FString MipDerivedDataKey;
	FTexture2DMipMap DummyMip;
	DummyMip.SizeX = 0;
	DummyMip.SizeY = 0;
	GetTextureDerivedMipKey(0, DummyMip, KeySuffix, MipDerivedDataKey);
	int32 PrefixEndIndex = MipDerivedDataKey.Find(TEXT("_MIP0_"), ESearchCase::CaseSensitive);
	check(PrefixEndIndex != -1);
	MipDerivedDataKey.LeftInline(PrefixEndIndex);
	check(!MipDerivedDataKey.IsEmpty());
	Writer.AddString("MipKeyPrefix",*MipDerivedDataKey);

	Writer.EndObject();
	return Writer.Save().AsObject();
}

static FCbObject WriteTextureSourceToCompactBinary(const FTextureSource& TextureSource, EGammaSpace GammaSpace)
{
	FCbWriter Writer;
	Writer.BeginObject();

	Writer.AddString("Input", TextureSource.GetId().ToString());
	Writer.AddInteger("CompressionFormat", TextureSource.GetSourceCompression());
	Writer.AddInteger("SourceFormat", TextureSource.GetFormat());
	Writer.AddInteger("GammaSpace", (int32)GammaSpace);
	Writer.AddInteger("NumSlices", TextureSource.GetNumSlices());
	Writer.AddInteger("SizeX", TextureSource.GetSizeX());
	Writer.AddInteger("SizeY", TextureSource.GetSizeY());
	Writer.BeginArray("Mips");
	const int32 NumMips = TextureSource.GetNumMips();
	int64 Offset = 0;
	for (int32 MipIndex = 0; MipIndex < NumMips; ++MipIndex)
	{
		Writer.BeginObject();
		Writer.AddInteger("Offset", Offset);
		const int64 MipSize = TextureSource.CalcMipSize(MipIndex);
		Writer.AddInteger("Size", MipSize);
		Offset += MipSize;
		Writer.EndObject();
	}
	Writer.EndArray();

	Writer.EndObject();
	return Writer.Save().AsObject();
}

FString GetTextureBuildFunctionName(const FTextureBuildSettings& BuildSettings)
{
	const ITextureFormat* TextureFormat = nullptr;
	FName TextureFormatModuleName;
	ITextureFormatModule* TextureFormatModule = nullptr;

	ITextureFormatManagerModule* TFM = GetTextureFormatManager();
	if (TFM)
	{
		TextureFormat = TFM->FindTextureFormatAndModule(BuildSettings.TextureFormatName, TextureFormatModuleName, TextureFormatModule);
	}

	if (TextureFormat == nullptr)
	{
		return FString();
	}

	// Texture format modules are inconsistent in their naming.  eg: TextureFormatUncompressed, PS5TextureFormat
	// We attempt to  unify the naming here when specifying build function names.
	TStringBuilder<64> BuildFunctionNameBuilder;
	BuildFunctionNameBuilder << TextureFormatModuleName.ToString().Replace(TEXT("TextureFormat"), TEXT(""));
	BuildFunctionNameBuilder << TEXT("Texture");

	return BuildFunctionNameBuilder.ToString();
}

void ComposeTextureBuildFunctionConstants(const FString& KeySuffix, const UTexture& Texture, const FTextureBuildSettings& BuildSettings, int32 LayerIndex, int32 NumInlineMips, FTextureConstantOperator Operator)
{
	const ITextureFormat* TextureFormat = nullptr;
	FName TextureFormatModuleName;
	ITextureFormatModule* TextureFormatModule = nullptr;

	ITextureFormatManagerModule* TFM = GetTextureFormatManager();
	if (TFM)
	{
		TextureFormat = TFM->FindTextureFormatAndModule(BuildSettings.TextureFormatName, TextureFormatModuleName, TextureFormatModule);
	}

	if (TextureFormat == nullptr)
	{
		return;
	}

	Operator(TEXT("TextureBuildSettings"), WriteBuildSettingsToCompactBinary(BuildSettings, TextureFormat));
	Operator(TEXT("TextureOutputSettings"), WriteOutputSettingsToCompactBinary(NumInlineMips, KeySuffix));

	FTextureFormatSettings TextureFormatSettings;
	Texture.GetLayerFormatSettings(LayerIndex, TextureFormatSettings);
	EGammaSpace TextureGammaSpace = TextureFormatSettings.SRGB ? (Texture.bUseLegacyGamma ? EGammaSpace::Pow22 : EGammaSpace::sRGB) : EGammaSpace::Linear;
	Operator(TEXT("TextureSource"), WriteTextureSourceToCompactBinary(Texture.Source, TextureGammaSpace));
	if ((bool)Texture.CompositeTexture)
	{
		FTextureFormatSettings CompositeTextureFormatSettings;
		Texture.CompositeTexture->GetLayerFormatSettings(LayerIndex, CompositeTextureFormatSettings);
		EGammaSpace CompositeTextureGammaSpace = CompositeTextureFormatSettings.SRGB ? (Texture.CompositeTexture->bUseLegacyGamma ? EGammaSpace::Pow22 : EGammaSpace::sRGB) : EGammaSpace::Linear;

		Operator(TEXT("CompositeTextureSource"), WriteTextureSourceToCompactBinary(Texture.CompositeTexture->Source, CompositeTextureGammaSpace));
	}
}

#endif // WITH_EDITOR
