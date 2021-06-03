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
#include "Serialization/CompactBinaryWriter.h"
#include "TextureDerivedDataBuildUtils.h"
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

	BuildFunctionName = GetTextureBuildFunctionName(BuildSettings);

	if (BuildFunctionName.IsEmpty())
	{
		bExporting = false;
	}

	if (!bExporting)
	{
		return;
	}

	TexturePath = Texture.GetPathName();

	UE::DerivedData::FBuildActionBuilder ActionBuilder = DerivedDataBuild->CreateAction(TexturePath, BuildFunctionName);

	ComposeTextureBuildFunctionConstants(KeySuffix, Texture, BuildSettings, LayerIndex, NumInlineMips, 
		[&ActionBuilder] (FStringView Key, const FCbObject& Value)
		{
			ActionBuilder.AddConstant(Key, Value);
		});

	ActionBuilder.AddInput(Texture.Source.GetId().ToString(), ExportedTextureBulkDataHash, ExportedTextureBulkDataSize);
	if ((bool)Texture.CompositeTexture && !ExportedCompositeTextureBulkDataHash.IsZero())
	{
		ActionBuilder.AddInput(Texture.CompositeTexture->Source.GetId().ToString(), ExportedCompositeTextureBulkDataHash, ExportedCompositeTextureBulkDataSize);
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
