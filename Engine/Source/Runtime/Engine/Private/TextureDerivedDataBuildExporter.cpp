// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureDerivedDataBuildExporter.h"

#if WITH_EDITOR

#include "DerivedDataBuild.h"
#include "DerivedDataBuildAction.h"
#include "DerivedDataBuildOutput.h"
#include "DerivedDataPayload.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "Interfaces/ITextureFormat.h"
#include "Interfaces/ITextureFormatManagerModule.h"
#include "Misc/CoreMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/StringBuilder.h"
#include "Serialization/CompactBinaryPackage.h"
#include "Serialization/CompactBinaryWriter.h"
#include "TextureDerivedDataTask.h"
#include "TextureFormatManager.h"


void GetTextureDerivedDataKeyFromSuffix(const FString& KeySuffix, FString& OutKey);
void GetTextureDerivedMipKey(int32 MipIndex, const FTexture2DMipMap& Mip, const FString& KeySuffix, FString& OutKey);

static bool ShortenKey(const TCHAR* CacheKey, FString& Result)
{
	const int32 MaxKeyLength = 120;
	Result = FString(CacheKey);
	if (Result.Len() <= MaxKeyLength)
	{
		return false;
	}

	FSHA1 HashState;
	int32 Length = Result.Len();
	HashState.Update((const uint8*)&Length, sizeof(int32));

	auto ResultSrc = StringCast<UCS2CHAR>(*Result);

	// This is pretty redundant. Incorporating the CRC of the name into the hash
	// which also ends up computing SHA1 of the name is not really going to make 
	// any meaningful difference to the strength of the key so it's just a waste
	// of CPU. However it's difficult to get rid of without invalidating the DDC
	// contents so here we are.
	const uint32 CRCofPayload(FCrc::MemCrc32(ResultSrc.Get(), Length * sizeof(UCS2CHAR)));
	HashState.Update((const uint8*)&CRCofPayload, sizeof(uint32));

	HashState.Update((const uint8*)ResultSrc.Get(), Length * sizeof(UCS2CHAR));

	HashState.Final();
	uint8 Hash[FSHA1::DigestSize];
	HashState.GetHash(Hash);
	const FString HashString = BytesToHex(Hash, FSHA1::DigestSize);

	int32 HashStringSize = HashString.Len();
	int32 OriginalPart = MaxKeyLength - HashStringSize - 2;
	Result = Result.Left(OriginalPart) + TEXT("__") + HashString;
	check(Result.Len() == MaxKeyLength && Result.Len() > 0);
	return true;
}

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

	Writer.AddString("Input", TextureSource.GetIdString());
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

static FIoHash HashAndWriteToCompressedBufferFile(const FString& Directory, const void* InData, uint64 InDataSize)
{
	FIoHash DataHash = FIoHash::HashBuffer(InData, InDataSize);
	TStringBuilder<128> DataHashStringBuilder;
	DataHashStringBuilder << DataHash;

	FCompressedBuffer CompressedBufferContents = FCompressedBuffer::Compress(NAME_Default, FSharedBuffer::MakeView(InData, InDataSize));
	if (TUniquePtr<FArchive> FileAr{IFileManager::Get().CreateFileWriter(*(Directory / *DataHashStringBuilder), FILEWRITE_NoReplaceExisting)})
	{
		*FileAr << CompressedBufferContents;
	}
	return DataHash;
}

static FIoHash ExportTextureBulkDataAttachment(const FString& ExportRoot, FTextureSource& TextureSource, uint64& OutSize)
{
	const FString BuildInputPath = ExportRoot / TEXT("Inputs");

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	bool bExporting = PlatformFile.CreateDirectoryTree(*BuildInputPath);

	if (!bExporting)
	{
		OutSize = 0;
		return FIoHash();
	}

	FIoHash BulkDataHash;
	TextureSource.OperateOnLoadedBulkData([&BuildInputPath, &BulkDataHash, &OutSize] (const FSharedBuffer& BulkDataBuffer) 
	{
		OutSize = BulkDataBuffer.GetSize();
		BulkDataHash = HashAndWriteToCompressedBufferFile(BuildInputPath, BulkDataBuffer.GetData(), BulkDataBuffer.GetSize());
	});

	return BulkDataHash;
}

void FTextureDerivedDataBuildExporter::Init(const FString& InKeySuffix)
{
	static const bool bExportsEnabled = FParse::Param(FCommandLine::Get(), TEXT("ExportTextureBuilds"));
	bEnabled = bExportsEnabled;
	if (!bEnabled)
	{
		return;
	}

	DerivedDataBuild = GetDerivedDataBuild();
	if (!DerivedDataBuild)
	{
		bEnabled = false;
		return;
	}

	KeySuffix = InKeySuffix;

	FString DerivedDataKeyLong;
	FString DerivedDataKey;
	GetTextureDerivedDataKeyFromSuffix(KeySuffix, DerivedDataKeyLong);
	ShortenKey(*DerivedDataKeyLong, DerivedDataKey);
	ExportRoot = FPaths::ProjectSavedDir() / TEXT("TextureBuildActions") / DerivedDataKey;
}

void FTextureDerivedDataBuildExporter::ExportTextureSourceBulkData(FTextureSource& TextureSource)
{
	if (bEnabled)
	{
		ExportedTextureBulkDataHash = ExportTextureBulkDataAttachment(ExportRoot, TextureSource, ExportedTextureBulkDataSize);
	}
}

void FTextureDerivedDataBuildExporter::ExportCompositeTextureSourceBulkData(FTextureSource& TextureSource)
{
	if (bEnabled)
	{
		ExportedCompositeTextureBulkDataHash = ExportTextureBulkDataAttachment(ExportRoot, TextureSource, ExportedCompositeTextureBulkDataSize);
	}
}

void FTextureDerivedDataBuildExporter::ExportTextureBuild(const UTexture& Texture, const FTextureBuildSettings& BuildSettings, int32 LayerIndex, int32 NumInlineMips)
{
	if (!bEnabled)
	{
		return;
	}
	
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	bool bExporting = PlatformFile.CreateDirectoryTree(*ExportRoot);
	
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
		bExporting = false;
	}

	if (!bExporting)
	{
		return;
	}
	// Texture format modules are inconsistent in their naming.  eg: TextureFormatUncompressed, PS5TextureFormat
	// We attempt to  unify the naming here when specifying build function names.
	TStringBuilder<64> BuildFunctionNameBuilder;
	BuildFunctionNameBuilder << TextureFormatModuleName.ToString().Replace(TEXT("TextureFormat"), TEXT(""));
	BuildFunctionNameBuilder << TEXT("Texture");

	BuildFunctionName = BuildFunctionNameBuilder;
	TexturePath = Texture.GetPathName();

	UE::DerivedData::FBuildActionBuilder ActionBuilder = DerivedDataBuild->CreateAction(TexturePath, BuildFunctionName);

	ActionBuilder.AddConstant(TEXT("TextureBuildSettings"), WriteBuildSettingsToCompactBinary(BuildSettings, TextureFormat));
	ActionBuilder.AddConstant(TEXT("TextureOutputSettings"), WriteOutputSettingsToCompactBinary(NumInlineMips, KeySuffix));

	FTextureFormatSettings TextureFormatSettings;
	Texture.GetLayerFormatSettings(LayerIndex, TextureFormatSettings);
	EGammaSpace TextureGammaSpace = TextureFormatSettings.SRGB ? (Texture.bUseLegacyGamma ? EGammaSpace::Pow22 : EGammaSpace::sRGB) : EGammaSpace::Linear;
	ActionBuilder.AddConstant(TEXT("TextureSource"), WriteTextureSourceToCompactBinary(Texture.Source, TextureGammaSpace));
	if ((bool)Texture.CompositeTexture && !ExportedCompositeTextureBulkDataHash.IsZero())
	{
		FTextureFormatSettings CompositeTextureFormatSettings;
		Texture.CompositeTexture->GetLayerFormatSettings(LayerIndex, CompositeTextureFormatSettings);
		EGammaSpace CompositeTextureGammaSpace = CompositeTextureFormatSettings.SRGB ? (Texture.CompositeTexture->bUseLegacyGamma ? EGammaSpace::Pow22 : EGammaSpace::sRGB) : EGammaSpace::Linear;

		ActionBuilder.AddConstant(TEXT("CompositeTextureSource"), WriteTextureSourceToCompactBinary(Texture.CompositeTexture->Source, CompositeTextureGammaSpace));
	}

	ActionBuilder.AddInput(Texture.Source.GetIdString(), ExportedTextureBulkDataHash, ExportedTextureBulkDataSize);
	if ((bool)Texture.CompositeTexture && !ExportedCompositeTextureBulkDataHash.IsZero())
	{
		ActionBuilder.AddInput(Texture.CompositeTexture->Source.GetIdString(), ExportedCompositeTextureBulkDataHash, ExportedCompositeTextureBulkDataSize);
	}

	if (TUniquePtr<FArchive> Ar{IFileManager::Get().CreateFileWriter(*(ExportRoot / TEXT("build.uddba")))})
	{
		FCbWriter BuildWriter;
		ActionBuilder.Build().Save(BuildWriter);
		BuildWriter.Save(*Ar);
	}

	TArray<FString> MetaStringArray;
	MetaStringArray.Add(*WriteToString<128>(TEXT("TexturePath="), TexturePath));
	MetaStringArray.Add(*WriteToString<128>(TEXT("SourceCompression="), Texture.Source.GetSourceCompressionAsString()));
	MetaStringArray.Add(*WriteToString<128>(TEXT("SourceNumMips="), Texture.Source.GetNumMips()));
	MetaStringArray.Add(*WriteToString<128>(TEXT("SourceNumSlices="), Texture.Source.GetNumSlices()));
	MetaStringArray.Add(*WriteToString<128>(TEXT("SourceSizeX="), Texture.Source.GetSizeX()));
	MetaStringArray.Add(*WriteToString<128>(TEXT("SourceSizeY="), Texture.Source.GetSizeY()));
	FFileHelper::SaveStringArrayToFile(MetaStringArray, *(ExportRoot / TEXT("Meta.txt")));
}

void FTextureDerivedDataBuildExporter::ExportTextureOutput(FTexturePlatformData& PlatformData, const FTextureBuildSettings& BuildSettings)
{
	if (!bEnabled)
	{
		return;
	}
	FString DerivedDataKeyLong;
	GetTextureDerivedDataKeyFromSuffix(KeySuffix, DerivedDataKeyLong);

	const bool bForceAllMipsToBeInlined = BuildSettings.bCubemap || (BuildSettings.bVolume && !BuildSettings.bStreamable) || (BuildSettings.bTextureArray && !BuildSettings.bStreamable);
	const FString OutputPath = ExportRoot / TEXT("ReferenceOutputs");
	struct DDCReferenceRecord
	{
		FString PayloadName;
		FIoHash PayloadHash;
		FString DDCKey;

		DDCReferenceRecord(const FString& InPayloadName, const FIoHash& InPayloadHash, const FString& InDDCKey)
			: PayloadName(InPayloadName)
			, PayloadHash(InPayloadHash)
			, DDCKey(InDDCKey)
		{
		}
	};

	TArray<DDCReferenceRecord> DDCReferences;

	UE::DerivedData::FBuildOutputBuilder OutputBuilder = DerivedDataBuild->CreateOutput(TexturePath, BuildFunctionName);

	const int32 MipCount = PlatformData.Mips.Num();
	const int32 FirstInlineMip = bForceAllMipsToBeInlined ? 0 : FMath::Max(0, MipCount - FMath::Max((int32)NUM_INLINE_DERIVED_MIPS, (int32)PlatformData.GetNumMipsInTail()));
	for (int32 MipIndex = 0; MipIndex < FirstInlineMip; ++MipIndex)
	{
		FTexture2DMipMap& Mip = PlatformData.Mips[MipIndex];

		int32 BulkDataSizeInBytes = Mip.BulkData.GetBulkDataSize();
		check(BulkDataSizeInBytes > 0);

		TArray<uint8> DerivedData;
		FMemoryWriter Ar(DerivedData, /*bIsPersistent=*/ true);
		Ar << BulkDataSizeInBytes;
		{
			void* BulkMipData = Mip.BulkData.Lock(LOCK_READ_ONLY);
			Ar.Serialize(BulkMipData, BulkDataSizeInBytes);
			Mip.BulkData.Unlock();
		}

		TStringBuilder<32> PayloadName;
		PayloadName << "Mip" << MipIndex;

		FIoHash DerivedDataHash = HashAndWriteToCompressedBufferFile(OutputPath, DerivedData.GetData(), DerivedData.Num());
		UE::DerivedData::FPayload MipPayload(UE::DerivedData::FPayloadId::FromName(PayloadName), DerivedDataHash, DerivedData.Num());

		OutputBuilder.AddPayload(MipPayload);

		check(Mip.DerivedDataKey.IsEmpty());

		FString MipDerivedDataKeyLong;
		FString MipDerivedDataKey;
		GetTextureDerivedMipKey(MipIndex, Mip, KeySuffix, MipDerivedDataKeyLong);
		ShortenKey(*MipDerivedDataKeyLong, MipDerivedDataKey);
		DDCReferences.Emplace(PayloadName.ToString(), DerivedDataHash, MipDerivedDataKey);
		Mip.DerivedDataKey = MipDerivedDataKeyLong;
	}

	FTexturePlatformData StrippedPlatformData;

	TArray<uint8> RawDerivedData;
	FMemoryWriter Ar(RawDerivedData, /*bIsPersistent=*/ true);
	PlatformData.SerializeWithConditionalBulkData(Ar, NULL);

	for (int32 MipIndex = FirstInlineMip; MipIndex < MipCount; ++MipIndex)
	{
		PlatformData.Mips[MipIndex].DerivedDataKey.Empty();
	}

	FIoHash DerivedDataHash = HashAndWriteToCompressedBufferFile(OutputPath, RawDerivedData.GetData(), RawDerivedData.Num());
	UE::DerivedData::FPayload TexturePayload(UE::DerivedData::FPayloadId::FromName("Texture"), DerivedDataHash, RawDerivedData.Num());

	OutputBuilder.AddPayload(TexturePayload);

	if (TUniquePtr<FArchive> FileAr{IFileManager::Get().CreateFileWriter(*(ExportRoot / TEXT("ReferenceOutput.uddbo")))})
	{
		FCbWriter OutputWriter;
		OutputBuilder.Build().Save(OutputWriter);
		OutputWriter.Save(*FileAr);
	}

	FString DerivedDataKey;
	ShortenKey(*DerivedDataKeyLong, DerivedDataKey);
	DDCReferences.Emplace(TEXT("Texture"), DerivedDataHash, DerivedDataKey);

	TArray<FString> DDCRefStringArray;
	FString Separator(TEXT(","));
	for (DDCReferenceRecord& DDCReference : DDCReferences)
	{
		TStringBuilder<256> LineBuilder;
		LineBuilder << DDCReference.PayloadName << Separator << DDCReference.PayloadHash << Separator << DDCReference.DDCKey;
		DDCRefStringArray.Add(LineBuilder.ToString());
	}
	FFileHelper::SaveStringArrayToFile(DDCRefStringArray, *(ExportRoot / TEXT("DDCReferences.txt")));
}


#endif // WITH_EDITOR
