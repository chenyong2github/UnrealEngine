// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"
#include "Misc/Compression.h"
#include "Misc/ICompressionFormat.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#if HAS_OODLE_DATA_SDK

#include "oodle2.h"

DEFINE_LOG_CATEGORY_STATIC(OodleCompression, Log, All);


#define OODLE_DERIVEDDATA_VER TEXT("BA7AA26CD1C3498787A3F3AA53895042")

struct FOodleCustomCompressor : ICompressionFormat
{
	bool bInitialized;
	OodleLZ_Compressor Compressor;
	OodleLZ_CompressionLevel CompressionLevel;
	OodleLZ_CompressOptions CompressionOptions;
	int SpaceSpeedTradeoffBytes;


	FOodleCustomCompressor(OodleLZ_Compressor InCompressor, OodleLZ_CompressionLevel InCompressionLevel, int InSpaceSpeedTradeoffBytes)
		: bInitialized(false)
		, SpaceSpeedTradeoffBytes(InSpaceSpeedTradeoffBytes)
	{
		Compressor = InCompressor;
		CompressionLevel = InCompressionLevel;
	}

	inline void ConditionalInitialize()
	{
		if (bInitialized)
		{
			return;
		}

		CompressionOptions = *OodleLZ_CompressOptions_GetDefault(Compressor, CompressionLevel);
		CompressionOptions.spaceSpeedTradeoffBytes = SpaceSpeedTradeoffBytes;

		bInitialized = true;
	}

	FString GetCompressorString()
	{

		// convert values to enums
		switch (Compressor)
		{
		case OodleLZ_Compressor_Mermaid:
			return TEXT("Mermaid");
		case OodleLZ_Compressor_Kraken:
			return TEXT("Kraken");
		case OodleLZ_Compressor_Selkie:
			return TEXT("Selkie");
#if UE4_OODLE_VER >= 270
		case OodleLZ_Compressor_Leviathan:
			return TEXT("Leviathan");
#else
		case OodleLZ_Compressor_LZNA:
			return TEXT("LZNA");
		case OodleLZ_Compressor_BitKnit:
			return TEXT("BitKnit");
		case OodleLZ_Compressor_LZB16:
			return TEXT("LZB16");
#endif
		}
		return TEXT("Unknown");
	}

	FString GetCompressionLevelString()
	{
		switch (CompressionLevel)
		{
		case OodleLZ_CompressionLevel_None:
			return TEXT("None");
		case OodleLZ_CompressionLevel_VeryFast:
			return TEXT("VeryFast");
		case OodleLZ_CompressionLevel_Fast:
			return TEXT("Fast");
		case OodleLZ_CompressionLevel_Normal:
			return TEXT("Normal");
		case OodleLZ_CompressionLevel_Optimal1:
			return TEXT("Optimal1");
		case OodleLZ_CompressionLevel_Optimal2:
			return TEXT("Optimal2");
		case OodleLZ_CompressionLevel_Optimal3:
			return TEXT("Optimal3");
		}
		return TEXT("Unknown");
	}

	virtual FName GetCompressionFormatName()
	{
		return TEXT("Oodle");
	}

	virtual uint32 GetVersion()
	{
		return uint32(UE4_OODLE_VER);
	}

	virtual FString GetDDCKeySuffix()
	{
		return FString::Printf(TEXT("C_%s_CL_%s_%s"), *GetCompressorString(), *GetCompressionLevelString(), OODLE_DERIVEDDATA_VER);
	}

	virtual bool Compress(void* CompressedBuffer, int32& CompressedSize, const void* UncompressedBuffer, int32 UncompressedSize, int32 CompressionData)
	{
		ConditionalInitialize();

		int32 Result = (int32)OodleLZ_Compress(Compressor, UncompressedBuffer, UncompressedSize, CompressedBuffer, CompressionLevel, &CompressionOptions);
		if (Result > 0)
		{
			if (Result > GetCompressedBufferSize(UncompressedSize, CompressionData))
			{
				FPlatformMisc::LowLevelOutputDebugStringf(TEXT("%d < %d"), Result, GetCompressedBufferSize(UncompressedSize, CompressionData));
				// we cannot safely go over the BufferSize needed!
				return false;
			}
			CompressedSize = Result;
			return true;
		}
		return false;
	}

	virtual bool Uncompress(void* UncompressedBuffer, int32& UncompressedSize, const void* CompressedBuffer, int32 CompressedSize, int32 CompressionData)
	{
		ConditionalInitialize();

		int32 Result = (int32)OodleLZ_Decompress(CompressedBuffer, CompressedSize, UncompressedBuffer, UncompressedSize, OodleLZ_FuzzSafe_Yes);
		if (Result > 0)
		{
			UncompressedSize = Result;
			return true;
		}
		return false;
	}

	virtual int32 GetCompressedBufferSize(int32 UncompressedSize, int32 CompressionData)
	{
		ConditionalInitialize();

#if UE4_OODLE_VER > 255
		int32 Needed = (int32)OodleLZ_GetCompressedBufferSizeNeeded(Compressor, UncompressedSize);
#else
		int32 Needed = (int32)OodleLZ_GetCompressedBufferSizeNeeded(UncompressedSize);
#endif
		return Needed;
	}
};

#endif

#include "Misc/ICompressionFormat.h"

class FOodleCompressionFormatModuleInterface : public IModuleInterface
{
	virtual void StartupModule() override
	{
#if HAS_OODLE_DATA_SDK

		FString Method = TEXT("Mermaid");
		FString Level = TEXT("Normal");
		int32 SpaceSpeedTradeoff = 256;

		// let commandline override
		FParse::Value(FCommandLine::Get(), TEXT("OodleMethod="), Method);
		FParse::Value(FCommandLine::Get(), TEXT("OodleLevel="), Level);
		FParse::Value(FCommandLine::Get(), TEXT("OodleSpaceSpeedTradeoff="), SpaceSpeedTradeoff);

		// convert values to enums
		TMap<FString, OodleLZ_Compressor> MethodMap = { 
			{ TEXT("Mermaid"), OodleLZ_Compressor_Mermaid },
			{ TEXT("Kraken"), OodleLZ_Compressor_Kraken },
			{ TEXT("Selkie"), OodleLZ_Compressor_Selkie },
#if UE4_OODLE_VER >= 270
			{ TEXT("Leviathan"), OodleLZ_Compressor_Leviathan },
#else
			{ TEXT("LZNA"), OodleLZ_Compressor_LZNA },
			{ TEXT("BitKnit"), OodleLZ_Compressor_BitKnit },
			{ TEXT("LZB16"), OodleLZ_Compressor_LZB16 },
#endif
			// when adding here remember to update FOodleCustomCompressor::GetCompressorString()
		};
		TMap<FString, OodleLZ_CompressionLevel> LevelMap = { 
			{ TEXT("None"), OodleLZ_CompressionLevel_None },
			{ TEXT("VeryFast"), OodleLZ_CompressionLevel_VeryFast },
			{ TEXT("Fast"), OodleLZ_CompressionLevel_Fast },
			{ TEXT("Normal"), OodleLZ_CompressionLevel_Normal },
			{ TEXT("Optimal1"), OodleLZ_CompressionLevel_Optimal1 },
			{ TEXT("Optimal2"), OodleLZ_CompressionLevel_Optimal2 },
			{ TEXT("Optimal3"), OodleLZ_CompressionLevel_Optimal3 },
			// when adding here remember to update FOodleCustomCompressor::GetCompressionLevelString()
		};

		OodleLZ_Compressor UsedCompressor = MethodMap.FindRef(Method);
		OodleLZ_CompressionLevel UsedLevel = LevelMap.FindRef(Level);

		UE_LOG(OodleCompression, Display, TEXT("Oodle initializing compressor with %s, level %s, SpaceSpeed tradeoff %d"), **MethodMap.FindKey(UsedCompressor), **LevelMap.FindKey(UsedLevel), SpaceSpeedTradeoff, *GEngineIni);

		CompressionFormat = new FOodleCustomCompressor(MethodMap.FindRef(Method), LevelMap.FindRef(Level), SpaceSpeedTradeoff);

		IModularFeatures::Get().RegisterModularFeature(COMPRESSION_FORMAT_FEATURE_NAME, CompressionFormat);

#if UE4_OODLE_VER > 255
		OodleCore_Plugins_SetPrintf(OodleCore_Plugin_Printf_Verbose);
#endif

#endif
	}

	virtual void ShutdownModule() override
	{
#if HAS_OODLE_DATA_SDK
		IModularFeatures::Get().UnregisterModularFeature(COMPRESSION_FORMAT_FEATURE_NAME, CompressionFormat);
		delete CompressionFormat;
#endif
	}

	ICompressionFormat* CompressionFormat = nullptr;
};

IMPLEMENT_MODULE(FOodleCompressionFormatModuleInterface, OodleCompressionFormat);
