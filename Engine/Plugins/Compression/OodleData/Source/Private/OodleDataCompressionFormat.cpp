// Copyright Epic Games, Inc. All Rights Reserved.

/*****************
* 
* Oodle Data compression plugin
* provides Oodle compression for Pak files & iostore
* is not for generic compression usage
* 
* The Oodle LZ codecs are extremely fast to decode and almost always speed up load times
* 
* The codecs are :
* Kraken  : high compression with good decode speed, the usual default
* Mermaid : less compression and faster decode speed; good when CPU bound or on platforms with less CPU power
* Selkie  : even less compression and faster that Mermaid
* Leviathan : more compression and slower to decode than Kraken
* 
* The encode time is mostly independent of the codec.  Use the codec to choose decode speed, and the encoder effort
* level to control encode time.
* 
* For daily iteration you might want level 3 ("Fast").  For shipping packages you might want level 6 ("optimal2") or higher.
* The valid level range is -4 to 9
* 
* This plugin reads its options on the command line via "compressmethod" and "compresslevel"
* eg. "-compressmethod=Kraken -compresslevel=4"
* 
* The Oodle decoder can decode any codec used, it doesn't need to know which one was used.
* 
* Compression options should be set up in your Game.ini ; for example :
* 

[/Script/UnrealEd.ProjectPackagingSettings]
bCompressed=True
bForceUseProjectCompressionFormat=False
PakFileCompressionFormats=Oodle
PakFileAdditionalCompressionOptions=-compressionblocksize=1MB -asynccompression
PakFileCompressionMethod=Mermaid
PakFileCompressionLevel_Distribution=8
PakFileCompressionLevel_TestShipping=5
PakFileCompressionLevel_DebugDevelopment=3

* This can be set in DefaultGame.ini then overrides set up per-platform.
* 
* The Engine also has a veto compressionformat set up in the DataDrivenPlatformInfo.ini for each platform in the field
* "HardwareCompressionFormat"
* eg. platforms that don't want any software compressor can set "HardwareCompressionFormat=None" and this will override what you
* set in "PakFileCompressionFormats".
* 
* The idea is in typical use, you set "PakFileCompressionFormats" for your Game, and you get that compressor on most platforms, but on
* some platforms that don't want compression, it automatically turns off.
* 
* If you want to force use of your Game.ini compressor (ignore the HardwareCompressionFormat) you can set bForceUseProjectCompressionFormat
* in ProjectPackagingSettings.
* 
* 
* When using Oodle we recommend "-compressionblocksize=1MB -asynccompression" which can be set with PakFileAdditionalCompressionOptions.
* 
* ***************************/

#include "CoreMinimal.h"
#include "CoreTypes.h"
#include "Misc/Compression.h"
#include "Misc/ICompressionFormat.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Stats/Stats.h"
#include "Stats/Stats2.h"

#if WITH_EDITOR
#include "Settings/ProjectPackagingSettings.h"
#endif

#include "OodleDataCompressionFormatPCH.h"

DEFINE_LOG_CATEGORY_STATIC(OodleDataCompression, Log, All);

#define OODLE_DERIVEDDATA_VER TEXT("BA7AA26CD1C3498787A3F3AA53895042")

// Pre-allocates this many decode temporary buffers. 
// More means less dynamic allocation, but more static memory overhead. 
// optimal number may vary depending on Platform,OS,etc...
#define NUM_OODLE_DECODE_BUFFERS 2

// if the Decoder object is <= this size, just put it on the stack
// MAX_OODLE_DECODER_SIZE_ON_STACK needs to be at least 64k to ever be used
// the benefit of this could be more parallel decodes if the 2 pre-allocated buffers are in use
//	without resorting to a heap alloc
#define MAX_OODLE_DECODER_SIZE_ON_STACK  0
// if you can guarantee this much stack available :
//#define MAX_OODLE_DECODER_SIZE_ON_STACK	 100000


#if STATS
DECLARE_STATS_GROUP( TEXT( "Compression" ), STATGROUP_Compression, STATCAT_Advanced );
DECLARE_FLOAT_ACCUMULATOR_STAT(TEXT("Total oodle encode time"), STAT_Compression_Oodle_Encode, STATGROUP_Compression);
DECLARE_FLOAT_ACCUMULATOR_STAT(TEXT("Total oodle decode time"), STAT_Compression_Oodle_Decode, STATGROUP_Compression);
#endif


struct FOodleDataCompressionFormat : ICompressionFormat
{
	OodleLZ_Compressor Compressor;
	OodleLZ_CompressionLevel CompressionLevel;
	OodleLZ_CompressOptions CompressionOptions;
	int32 OodleDecoderMemorySize;

	FCriticalSection OodleDecoderMutex[NUM_OODLE_DECODE_BUFFERS];
	void * OodleDecoderMemory[NUM_OODLE_DECODE_BUFFERS];

	FOodleDataCompressionFormat(OodleLZ_Compressor InCompressor, OodleLZ_CompressionLevel InCompressionLevel, int InSpaceSpeedTradeoffBytes)
	{
		Compressor = InCompressor;
		CompressionLevel = InCompressionLevel;
		CompressionOptions = *OodleLZ_CompressOptions_GetDefault(Compressor, CompressionLevel);
		CompressionOptions.spaceSpeedTradeoffBytes = InSpaceSpeedTradeoffBytes;
		// we're usually doing limited chunks, no need for LRM :
		CompressionOptions.makeLongRangeMatcher = false;

		// enough decoder scratch for any compressor & buffer size.
		// note "InCompressor" is what we want to Encode with but we may be asked to decode other compressors!
		OodleDecoderMemorySize = (int32) OodleLZDecoder_MemorySizeNeeded(OodleLZ_Compressor_Invalid, -1);  

		for (int i = 0; i < NUM_OODLE_DECODE_BUFFERS; ++i)
		{
			OodleDecoderMemory[i] = FMemory::Malloc(OodleDecoderMemorySize);
			if (OodleDecoderMemory[i] == NULL) 
			{
				UE_LOG(OodleDataCompression, Error, TEXT("FOodleDataCompressionFormat - Failed to allocate %d!"), OodleDecoderMemorySize);
			}
		}
	}

	virtual ~FOodleDataCompressionFormat() 
	{
		for (int i = 0; i < NUM_OODLE_DECODE_BUFFERS; ++i)
		{
			if (OodleDecoderMutex[i].TryLock())
			{
				if (OodleDecoderMemory[i])
				{
					FMemory::Free(OodleDecoderMemory[i]);
					OodleDecoderMemory[i] = 0;
				}
				OodleDecoderMutex[i].Unlock();
			}
			else
			{
				UE_LOG(OodleDataCompression, Error, TEXT("FOodleDataCompressionFormat - shutting down while in use?"));
			}
		}
	}

	FString GetCompressorString() const
	{
		// convert values to enums
		switch (Compressor)
		{
			case OodleLZ_Compressor_Selkie:				return TEXT("Selkie");
			case OodleLZ_Compressor_Mermaid:			return TEXT("Mermaid");
			case OodleLZ_Compressor_Kraken: 			return TEXT("Kraken");
			case OodleLZ_Compressor_Leviathan:			return TEXT("Leviathan");
			case OodleLZ_Compressor_Hydra:				return TEXT("Hydra");
			default: break;
		}
		return TEXT("Unknown");
	}

	FString GetCompressionLevelString() const
	{
		switch (CompressionLevel)
		{
			case OodleLZ_CompressionLevel_HyperFast4:	return TEXT("HyperFast4");
			case OodleLZ_CompressionLevel_HyperFast3:	return TEXT("HyperFast3");
			case OodleLZ_CompressionLevel_HyperFast2:	return TEXT("HyperFast2");
			case OodleLZ_CompressionLevel_HyperFast1:	return TEXT("HyperFast1");
			case OodleLZ_CompressionLevel_None:			return TEXT("None");
			case OodleLZ_CompressionLevel_SuperFast:	return TEXT("SuperFast");
			case OodleLZ_CompressionLevel_VeryFast:		return TEXT("VeryFast");
			case OodleLZ_CompressionLevel_Fast:			return TEXT("Fast");
			case OodleLZ_CompressionLevel_Normal:		return TEXT("Normal");
			case OodleLZ_CompressionLevel_Optimal1:		return TEXT("Optimal1");
			case OodleLZ_CompressionLevel_Optimal2:		return TEXT("Optimal2");
			case OodleLZ_CompressionLevel_Optimal3:		return TEXT("Optimal3");
			case OodleLZ_CompressionLevel_Optimal4:		return TEXT("Optimal4");
			case OodleLZ_CompressionLevel_Optimal5:		return TEXT("Optimal5");
			default: break;
		}
		return TEXT("Unknown");
	}

	virtual FName GetCompressionFormatName() override
	{
		static FName OodleName( TEXT("Oodle") );
		return OodleName;
	}

	virtual uint32 GetVersion() override
	{
		return 20000 + OODLE2_VERSION_MAJOR*100 + OODLE2_VERSION_MINOR;
	}
	
	int32 OodleDecode(const void * InCompBuf, int32 InCompBufSize, void * OutRawBuf, int32 InRawLen) 
	{
		// find the minimum size needed for this decode, OodleDecoderMemorySize may be larger
		OodleLZ_Compressor CurCompressor = OodleLZ_GetChunkCompressor(InCompBuf, InCompBufSize, NULL);
		SSIZE_T DecoderMemorySize = OodleLZDecoder_MemorySizeNeeded(CurCompressor, InRawLen);
		void * DecoderMemory = NULL;
		bool DoFreeDecoderMemory = false;

		#if MAX_OODLE_DECODER_SIZE_ON_STACK > 0 
		if ( DecoderMemorySize <= MAX_OODLE_DECODER_SIZE_ON_STACK )
		{
			// InRawLen is small
			//  just use the stack for our needed decoded memory scrtach

			DecoderMemory = alloca(DecoderMemorySize);
			
			//UE_LOG(OodleDataCompression, Display, TEXT("Decode on stack : %d -> %d"),InCompBufSize,InRawLen );
		}
		else
		#endif
		{

			// try to take a mutex for one of the pre-allocated decode buffers
			for (int i = 0; i < NUM_OODLE_DECODE_BUFFERS; ++i) 
			{
				if (OodleDecoderMutex[i].TryLock()) 
				{
					if (OodleDecoderMemory[i])
					{
						//UE_LOG(OodleDataCompression, Display, TEXT("Decode with lock : %d -> %d"),InCompBufSize,InRawLen );

						int Result = OodleLZ_Decompress(InCompBuf, InCompBufSize, OutRawBuf, InRawLen,
							OodleLZ_FuzzSafe_Yes, OodleLZ_CheckCRC_Yes, OodleLZ_Verbosity_None,
							NULL, 0, NULL, NULL,
							OodleDecoderMemory[i], OodleDecoderMemorySize);

						OodleDecoderMutex[i].Unlock();

						return Result;
					}

					OodleDecoderMutex[i].Unlock();
				}
			}
			
			//UE_LOG(OodleDataCompression, Display, TEXT("Decode with malloc : %d -> %d"),InCompBufSize,InRawLen );

			// allocate memory for the decoder so that Oodle doesn't allocate anything internally
			DecoderMemory = FMemory::Malloc(DecoderMemorySize);
			if (DecoderMemory == NULL) 
			{
				UE_LOG(OodleDataCompression, Error, TEXT("FOodleDataCompressionFormat::OodleDecode - Failed to allocate %d!"), DecoderMemorySize);
				return 0;
			}
			DoFreeDecoderMemory = true;
		}

		int Result = OodleLZ_Decompress(InCompBuf, InCompBufSize, OutRawBuf, InRawLen, 
			OodleLZ_FuzzSafe_Yes,OodleLZ_CheckCRC_Yes,OodleLZ_Verbosity_None,
			NULL, 0, NULL, NULL,
			DecoderMemory, DecoderMemorySize);

		if ( DoFreeDecoderMemory )
		{
			FMemory::Free(DecoderMemory);
		}

		return Result;
	}

	virtual FString GetDDCKeySuffix() override
	{
		// DerivedDataCache key string
		//  ideally this should be unique for any settings changed
		return FString::Printf(TEXT("C_%s_CL_%s_%s"), *GetCompressorString(), *GetCompressionLevelString(), OODLE_DERIVEDDATA_VER);
	}

	virtual bool Compress(void* OutCompressedBuffer, int32& OutCompressedSize, const void* InUncompressedBuffer, int32 InUncompressedSize, int32 InCompressionData) override
	{
#if STATS
		DECLARE_SCOPE_CYCLE_COUNTER(TEXT("Compress Memory Oodle"), STAT_Compression_Oodle_Encode_Cycles, STATGROUP_Compression);
		SCOPE_SECONDS_ACCUMULATOR(STAT_Compression_Oodle_Encode);
#endif

		// OutCompressedSize is read-write
		int32 CompressedBufferSize = OutCompressedSize;

		// CompressedSize should be >= GetCompressedBufferSize(UncompressedSize, CompressionData)
		check(CompressedBufferSize >= GetCompressedBufferSize(InUncompressedSize, InCompressionData));

		OO_SINTa Result = OodleLZ_Compress(Compressor, InUncompressedBuffer, InUncompressedSize, OutCompressedBuffer, CompressionLevel, &CompressionOptions);

		// verbose log all compresses :
		//UE_LOG(OodleDataCompression, Display, TEXT("Oodle Compress : %d -> %d"), UncompressedSize, Result);
		
		if (Result <= 0)
		{
			OutCompressedSize = -1;
			return false;
		}
		else
		{
			OutCompressedSize = (int32) Result;
			return true;
		}
	}

	virtual bool Uncompress(void* OutUncompressedBuffer, int32& OutUncompressedSize, const void* InCompressedBuffer, int32 InCompressedSize, int32 CompressionData) override
	{
#if STATS
		DECLARE_SCOPE_CYCLE_COUNTER(TEXT("Uncompress Memory Oodle"), STAT_Compression_Oodle_Decode_Cycles, STATGROUP_Compression);
		SCOPE_SECONDS_ACCUMULATOR(STAT_Compression_Oodle_Decode);
#endif
		// OutUncompressedSize is read-write
		int32 UncompressedSize = OutUncompressedSize;
		int Result = OodleDecode(InCompressedBuffer, InCompressedSize, OutUncompressedBuffer, UncompressedSize);
		if (Result > 0)
		{
			// Result should == UncompressedSize
			check(Result == UncompressedSize);
			OutUncompressedSize = Result;
			return true;
		}
		return false;
	}

	virtual int32 GetCompressedBufferSize(int32 UncompressedSize, int32 CompressionData) override
	{
		// CompressionData is not used
		int32 Needed = (int32)OodleLZ_GetCompressedBufferSizeNeeded(Compressor, UncompressedSize);
		return Needed;
	}
};


class FOodleDataCompressionFormatModuleInterface : public IModuleInterface
{

	virtual void StartupModule() override
	{
		// settings to use in non-tools context (eg. runtime game encoding) :
		// (SetDefaultOodleOptionsForPackaging sets options for pak compression & iostore)
		OodleLZ_Compressor UsedCompressor = OodleLZ_Compressor_Kraken;
		OodleLZ_CompressionLevel UsedLevel = OodleLZ_CompressionLevel_Fast;
		int32 SpaceSpeedTradeoff = 0;

		#if ! UE_BUILD_SHIPPING
		{

		// parse the command line to get compressor & level settings

		// this Startup is done in various different contexts;
		// when the editor loads up
		// when the game loads (we will be used to decompress only and encode settings are not used)
		// when the package cooking tool loads up <- this is when we set the relevant encode settings
		
		// is_program is true for cooker & UnrealPak (not Editor or Game)
		bool IsProgram = IS_PROGRAM;

		bool IsCommandlet = IsRunningCommandlet();
		bool IsIOStore = IsCommandlet && FCString::Strifind(FCommandLine::Get(), TEXT("-run=iostore")) != NULL;
				
		// we only need to be doing all this when run as UnrealPak or iostore commandlet
		//	(IsProgram also picks up cooker and a few other things, that's okay)
		if ( IsIOStore || IsProgram )
		{
			// defaults if no options set :
			UsedCompressor = OodleLZ_Compressor_Kraken;
			// Kraken is a good compromise of compression ratio & speed

			UsedLevel = OodleLZ_CompressionLevel_Normal;
			// for faster iteration time during development

			SpaceSpeedTradeoff = 0; // 0 means use default
			// SpaceSpeedTradeoff is mainly for tuning the Hydra compressor
			//	it can also be used to skew your compression towards higher ratio vs faster decode

			// convert values to enums
			TMap<FString, OodleLZ_Compressor> MethodMap =
			{ 
				{ TEXT("Selkie"), OodleLZ_Compressor_Selkie },
				{ TEXT("Mermaid"), OodleLZ_Compressor_Mermaid },
				{ TEXT("Kraken"), OodleLZ_Compressor_Kraken },
				{ TEXT("Leviathan"), OodleLZ_Compressor_Leviathan },
				{ TEXT("Hydra"), OodleLZ_Compressor_Hydra },
				// when adding here remember to update FOodleDataCompressionFormat::GetCompressorString()
			};
			TMap<FString, OodleLZ_CompressionLevel> LevelMap = 
			{ 
				{ TEXT("HyperFast4"), OodleLZ_CompressionLevel_HyperFast4 },
				{ TEXT("HyperFast3"), OodleLZ_CompressionLevel_HyperFast3 },
				{ TEXT("HyperFast2"), OodleLZ_CompressionLevel_HyperFast2 },
				{ TEXT("HyperFast1"), OodleLZ_CompressionLevel_HyperFast1 },
				{ TEXT("HyperFast"), OodleLZ_CompressionLevel_HyperFast1 },
				{ TEXT("None")     , OodleLZ_CompressionLevel_None },
				{ TEXT("SuperFast"), OodleLZ_CompressionLevel_SuperFast },
				{ TEXT("VeryFast"), OodleLZ_CompressionLevel_VeryFast },
				{ TEXT("Fast")  , OodleLZ_CompressionLevel_Fast },
				{ TEXT("Normal"), OodleLZ_CompressionLevel_Normal },
				{ TEXT("Optimal1"), OodleLZ_CompressionLevel_Optimal1 },
				{ TEXT("Optimal2"), OodleLZ_CompressionLevel_Optimal2 },
				{ TEXT("Optimal") , OodleLZ_CompressionLevel_Optimal2 },
				{ TEXT("Optimal3"), OodleLZ_CompressionLevel_Optimal3 },
				{ TEXT("Optimal4"), OodleLZ_CompressionLevel_Optimal4 },
				{ TEXT("Optimal5"), OodleLZ_CompressionLevel_Optimal5 },
				
				{ TEXT("-4"), OodleLZ_CompressionLevel_HyperFast4 },
				{ TEXT("-3"), OodleLZ_CompressionLevel_HyperFast3 },
				{ TEXT("-2"), OodleLZ_CompressionLevel_HyperFast2 },
				{ TEXT("-1"), OodleLZ_CompressionLevel_HyperFast1 },
				{ TEXT("0"), OodleLZ_CompressionLevel_None },
				{ TEXT("1"), OodleLZ_CompressionLevel_SuperFast },
				{ TEXT("2"), OodleLZ_CompressionLevel_VeryFast },
				{ TEXT("3"), OodleLZ_CompressionLevel_Fast },
				{ TEXT("4"), OodleLZ_CompressionLevel_Normal },
				{ TEXT("5"), OodleLZ_CompressionLevel_Optimal1 },
				{ TEXT("6"), OodleLZ_CompressionLevel_Optimal2 },
				{ TEXT("7"), OodleLZ_CompressionLevel_Optimal3 },
				{ TEXT("8"), OodleLZ_CompressionLevel_Optimal4 },
				{ TEXT("9"), OodleLZ_CompressionLevel_Optimal5 },
				// when adding here remember to update FOodleDataCompressionFormat::GetCompressionLevelString()
			};

			// override from command line :
			FString Method = "";
			FString Level = "";

			// let commandline override
			//FParse::Value does not change output fields if they are not found
			FParse::Value(FCommandLine::Get(), TEXT("compressmethod="), Method);
			FParse::Value(FCommandLine::Get(), TEXT("compresslevel="), Level);
			FParse::Value(FCommandLine::Get(), TEXT("OodleSpaceSpeedTradeoff="), SpaceSpeedTradeoff);
		
			OodleLZ_Compressor * pUsedCompressor = MethodMap.Find(Method);
			OodleLZ_CompressionLevel * pUsedLevel = LevelMap.Find(Level);

			if (pUsedCompressor) UsedCompressor = *pUsedCompressor;
			if (pUsedLevel)	UsedLevel = *pUsedLevel;

			// no init log line if we're not enabled :
			bool bUseCompressionFormatOodle = FCString::Strifind(FCommandLine::Get(), TEXT("-compressionformats=oodle")) != NULL;
			if ( bUseCompressionFormatOodle )			
			{
				UE_LOG(OodleDataCompression, Display, TEXT("Oodle v%s initializing with method=%s, level=%d=%s"), TEXT(OodleVersion), **MethodMap.FindKey(UsedCompressor), (int)UsedLevel, **LevelMap.FindKey(UsedLevel) );
			}
		}

		}
		#endif // SHIPPING

		//-----------------------------------
		// register the compression format :
		//  this is used by the shipping game to decode any paks compressed with Oodle :

		CompressionFormat = new FOodleDataCompressionFormat(UsedCompressor, UsedLevel, SpaceSpeedTradeoff);

		IModularFeatures::Get().RegisterModularFeature(COMPRESSION_FORMAT_FEATURE_NAME, CompressionFormat);
	}

	virtual void ShutdownModule() override
	{
		IModularFeatures::Get().UnregisterModularFeature(COMPRESSION_FORMAT_FEATURE_NAME, CompressionFormat);
		delete CompressionFormat;
		CompressionFormat = nullptr;
	}

	ICompressionFormat* CompressionFormat = nullptr;
};

IMPLEMENT_MODULE(FOodleDataCompressionFormatModuleInterface, OodleDataCompressionFormat);
