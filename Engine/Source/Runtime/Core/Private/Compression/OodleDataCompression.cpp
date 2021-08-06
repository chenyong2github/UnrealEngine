// Copyright Epic Games, Inc. All Rights Reserved.

#include "Compression/OodleDataCompression.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ICompressionFormat.h"
#include "Templates/CheckValueCast.h"

#include "oodle2.h"

DEFINE_LOG_CATEGORY(OodleDataCompression);

extern ICompressionFormat * CreateOodleDataCompressionFormat();

namespace FOodleDataCompression
{

struct OodleDataCompressionDecoders
{
	OO_SINTa OodleDecoderMemorySize = 0;
	int32 OodleDecoderBufferCount =0;

	struct OodleDataCompressionDecoder
	{
		FCriticalSection OodleDecoderMutex;
		void * OodleDecoderMemory = nullptr;
		char pad[64];

		OodleDataCompressionDecoder()
		{
		}
	};

	OodleDataCompressionDecoder* OodleDecoders = nullptr;

	OodleDataCompressionDecoders()
	{
		// enough decoder scratch for any compressor & buffer size.
		// note "InCompressor" is what we want to Encode with but we may be asked to decode other compressors!
		OodleDecoderMemorySize = OodleLZDecoder_MemorySizeNeeded(OodleLZ_Compressor_Invalid, -1);

		int32 BufferCount = 2;

		// be wary of possible init order problem
		//  if we init Oodle before config might not exist yet?
		//  can we just check GConfig vs nullptr ?
		//  (eg. if Oodle is used to unpak ini filed?)
		if ( GConfig )
		{
			GConfig->GetInt(TEXT("OodleDataCompressionFormat"), TEXT("PreallocatedBufferCount"), BufferCount, GEngineIni);
			if (BufferCount < 0)
			{
				BufferCount = 0;
			}
		}

		OodleDecoderBufferCount = BufferCount;
		if (OodleDecoderBufferCount)
		{
			OodleDecoders = new OodleDataCompressionDecoder[OodleDecoderBufferCount];
		}
	}
	
	~OodleDataCompressionDecoders()
	{
		// NOTE: currently never freed

		if (OodleDecoderBufferCount)
		{
			for (int i = 0; i < OodleDecoderBufferCount; ++i)
			{
				if (OodleDecoders[i].OodleDecoderMutex.TryLock())
				{
					FMemory::Free(OodleDecoders[i].OodleDecoderMemory);
					OodleDecoders[i].OodleDecoderMemory = nullptr;
					OodleDecoders[i].OodleDecoderMutex.Unlock();
				}
				else
				{
					UE_LOG(OodleDataCompression, Error, TEXT("FOodleDataCompressionFormat - shutting down while in use?"));
				}
			}

			delete [] OodleDecoders;
		}
	}

	
	int64 OodleDecode(const void * InCompBuf, int64 InCompBufSize64, void * OutRawBuf, int64 InRawLen64) 
	{
		OO_SINTa InCompBufSize = TCheckValueCast<OO_SINTa>(InCompBufSize64);
		OO_SINTa InRawLen = TCheckValueCast<OO_SINTa>(InRawLen64);

		// find the minimum size needed for this decode, OodleDecoderMemorySize may be larger
		OodleLZ_Compressor CurCompressor = OodleLZ_GetChunkCompressor(InCompBuf, InCompBufSize, NULL);
		if( CurCompressor == OodleLZ_Compressor_Invalid )
		{
			UE_LOG(OodleDataCompression, Error, TEXT("FOodleDataCompressionFormat - no Oodle compressor found!"));
			return OODLELZ_FAILED;
		}
			
		OO_SINTa DecoderMemorySize = OodleLZDecoder_MemorySizeNeeded(CurCompressor, InRawLen);
		void * DecoderMemory = NULL;

		// try to take a mutex for one of the pre-allocated decode buffers
		for (int i = 0; i < OodleDecoderBufferCount; ++i)
		{
			if (OodleDecoders[i].OodleDecoderMutex.TryLock()) 
			{
				if (OodleDecoders[i].OodleDecoderMemory == nullptr)
				{
					// Haven't allocated yet (we allocate on demand)
					OodleDecoders[i].OodleDecoderMemory = FMemory::Malloc(OodleDecoderMemorySize);
					if (OodleDecoders[i].OodleDecoderMemory == nullptr)
					{
						UE_LOG(OodleDataCompression, Error, TEXT("FOodleDataCompressionFormat - Failed to allocate preallocated	buffer %d bytes!"), OodleDecoderMemorySize);
						return OODLELZ_FAILED;
					}
				}

				if (OodleDecoders[i].OodleDecoderMemory)
				{
					//UE_LOG(OodleDataCompression, Display, TEXT("Decode with lock : %d -> %d"),InCompBufSize,InRawLen );

					OO_SINTa Result = OodleLZ_Decompress(InCompBuf, InCompBufSize, OutRawBuf, InRawLen,
						OodleLZ_FuzzSafe_Yes, OodleLZ_CheckCRC_Yes, OodleLZ_Verbosity_None,
						NULL, 0, NULL, NULL,
						OodleDecoders[i].OodleDecoderMemory, OodleDecoderMemorySize);

					OodleDecoders[i].OodleDecoderMutex.Unlock();

					return (int64) Result;
				}

				OodleDecoders[i].OodleDecoderMutex.Unlock();
			}
		}
			
		//UE_LOG(OodleDataCompression, Display, TEXT("Decode with malloc : %d -> %d"),InCompBufSize,InRawLen );

		// allocate memory for the decoder so that Oodle doesn't allocate anything internally
		DecoderMemory = FMemory::Malloc(DecoderMemorySize);
		if (DecoderMemory == NULL) 
		{
			UE_LOG(OodleDataCompression, Error, TEXT("FOodleDataCompressionFormat::OodleDecode - Failed to allocate %d!"), DecoderMemorySize);
			return OODLELZ_FAILED;
		}

		OO_SINTa Result = OodleLZ_Decompress(InCompBuf, InCompBufSize, OutRawBuf, InRawLen, 
			OodleLZ_FuzzSafe_Yes,OodleLZ_CheckCRC_Yes,OodleLZ_Verbosity_None,
			NULL, 0, NULL, NULL,
			DecoderMemory, DecoderMemorySize);

		FMemory::Free(DecoderMemory);

		return (int64) Result;
	}

};



static OodleLZ_Compressor CompressorToOodleLZ_Compressor(ECompressor Compressor)
{
	switch(Compressor)
	{
	case ECompressor::Selkie:
		return OodleLZ_Compressor_Selkie;
	case ECompressor::Mermaid:
		return OodleLZ_Compressor_Mermaid;
	case ECompressor::Kraken:
		return OodleLZ_Compressor_Kraken;
	case ECompressor::Leviathan:
		return OodleLZ_Compressor_Leviathan;
	case ECompressor::NotSet:
		return OodleLZ_Compressor_Invalid;
	default:
		UE_LOG(OodleDataCompression,Error,TEXT("Invalid Compressor: %d\n"),(int)Compressor);
		return OodleLZ_Compressor_Invalid;
	}
}

static OodleLZ_CompressionLevel CompressionLevelToOodleLZ_CompressionLevel(ECompressionLevel Level)
{
	int IntLevel = (int)Level;
	
	if ( IntLevel < (int)OodleLZ_CompressionLevel_Min || IntLevel > (int)OodleLZ_CompressionLevel_Max )
	{
		UE_LOG(OodleDataCompression,Error,TEXT("Invalid Level: %d\n"),IntLevel);
		return OodleLZ_CompressionLevel_Invalid;
	}

	return (OodleLZ_CompressionLevel) IntLevel;
}

ECompressionCommonUsage CORE_API GetCommonUsageFromLegacyCompressionFlags(ECompressionFlags Flags)
{
	switch(Flags)
	{
		case 0:
			return ECompressionCommonUsage::Default;
		case COMPRESS_BiasSpeed:
			return ECompressionCommonUsage::FastRealtimeEncode;
		case COMPRESS_BiasSize:
			return ECompressionCommonUsage::SlowerSmallerEncode;
		case COMPRESS_ForPackaging:
			return ECompressionCommonUsage::SlowestOfflineDistributionEncode;

		default:
			UE_LOG(OodleDataCompression,Error,TEXT("Invalid ECompressionFlags : %04X\n"),Flags);
			return ECompressionCommonUsage::Default;
	}
}

void CORE_API GetCompressorAndLevelForCommonUsage(ECompressionCommonUsage Usage,ECompressor & OutCompressor,ECompressionLevel & OutLevel)
{
	switch(Usage)
	{
		case ECompressionCommonUsage::Default:
			OutCompressor = ECompressor::Kraken;
			OutLevel = ECompressionLevel::Fast;
			break;
		case ECompressionCommonUsage::FastRealtimeEncode:
			OutCompressor = ECompressor::Mermaid;
			OutLevel = ECompressionLevel::HyperFast2;
			break;
		case ECompressionCommonUsage::SlowerSmallerEncode:
			OutCompressor = ECompressor::Kraken;
			OutLevel = ECompressionLevel::Normal;
			break;
		case ECompressionCommonUsage::SlowestOfflineDistributionEncode:
			OutCompressor = ECompressor::Kraken;
			OutLevel = ECompressionLevel::Optimal2;
			break;
		default:
			UE_LOG(OodleDataCompression,Error,TEXT("Invalid ECompressionFlags : %d\n"),(int)Usage);
			OutCompressor = ECompressor::Selkie;
			OutLevel = ECompressionLevel::None;
			return;
	}
}


int64 CORE_API CompressedBufferSizeNeeded(int64 InUncompressedSize)
{
	// size needed is the same for all newlz's
	//	so don't bother with a compressor arg here
	return OodleLZ_GetCompressedBufferSizeNeeded(OodleLZ_Compressor_Kraken, TCheckValueCast<OO_SINTa>(InUncompressedSize));
}

int64 CORE_API GetMaximumCompressedSize(int64 InUncompressedSize)
{
	int64 NumBlocks = (InUncompressedSize+ OODLELZ_BLOCK_LEN-1)/OODLELZ_BLOCK_LEN;
	int64 MaxCompressedSize = InUncompressedSize + NumBlocks * OODLELZ_BLOCK_MAXIMUM_EXPANSION;
	return MaxCompressedSize;
}

int64 CORE_API Compress(
							void * OutCompressedData, int64 InCompressedBufferSize,
							const void * InUncompressedData, int64 InUncompressedSize,
							ECompressor Compressor,
							ECompressionLevel Level)
{
	OodleLZ_Compressor LZCompressor = CompressorToOodleLZ_Compressor(Compressor);
	OodleLZ_CompressionLevel LZLevel = CompressionLevelToOodleLZ_CompressionLevel(Level);

	if ( InCompressedBufferSize < (int64) OodleLZ_GetCompressedBufferSizeNeeded(LZCompressor,TCheckValueCast<OO_SINTa>(InUncompressedSize)) )
	{
		UE_LOG(OodleDataCompression,Error,TEXT("OutCompressedSize too small\n"));		
		return OODLELZ_FAILED;
	}

	// OodleLZ_Compress will alloc internally using installed CorePlugins
	//	 (currently default plugins, no plugins installed)

	OO_SINTa CompressedSize = OodleLZ_Compress(LZCompressor,InUncompressedData,InUncompressedSize,OutCompressedData,LZLevel);
	return (int64) CompressedSize;
}

static OodleDataCompressionDecoders * GetGlobalOodleDataCompressionDecoders()
{
	static OodleDataCompressionDecoders GlobalOodleDataCompressionDecoders;
	// init on first use, never freed
	return &GlobalOodleDataCompressionDecoders;
}


static ICompressionFormat * GlobalOodleDataCompressionFormat = nullptr;

void CORE_API CompressionFormatInitOnFirstUseFromLock()
{
	// called from inside a critical section lock 
	//	from Compression.cpp / GetCompressionFormat
	if ( GlobalOodleDataCompressionFormat != nullptr )
		return;

	GlobalOodleDataCompressionFormat = CreateOodleDataCompressionFormat();
}


bool CORE_API Decompress(
						void * OutUncompressedData, int64 InUncompressedSize,
						const void * InCompressedData, int64 InCompressedSize)
{
	OodleDataCompressionDecoders * Decoders = GetGlobalOodleDataCompressionDecoders();

	int64 DecodeSize = Decoders->OodleDecode(InCompressedData,InCompressedSize,OutUncompressedData,InUncompressedSize);

	if ( DecodeSize == OODLELZ_FAILED )
	{
		return false;
	}

	check( DecodeSize == InUncompressedSize );
	return true;
}
					
void CORE_API StartupPreInit(void)
{
	// called from LaunchEngineLoop at "PreInit" time
	// not all Engine services may be set up yet, be careful what you use
	
	// @todo Oodle could install CorePlugins here for log/alloc/etc.

	// OodleConfig set global options for Oodle :
	OodleConfigValues OodleConfig = { };
	Oodle_GetConfigValues(&OodleConfig);
	// UE5 will always read/write Oodle v9 binary data :
	OodleConfig.m_OodleLZ_BackwardsCompatible_MajorVersion = 9;
	Oodle_SetConfigValues(&OodleConfig);
}

};
