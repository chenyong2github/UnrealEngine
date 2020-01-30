// Copyright Epic Games, Inc. All Rights Reserved.


#include "CrunchCompression.h"
#include "Modules/ModuleManager.h"
#include "HAL/IConsoleManager.h"
#include "Stats/Stats.h"
#include "Stats/Stats2.h"

const bool GAdaptiveBlockSizes = true;
static TAutoConsoleVariable<int32> CVarCrunchQuality(
	TEXT("crn.quality"),
	128,
	TEXT("Set the quality of the crunch texture compression. [0, 255], default: 128"));

class FCrunchCompressionModule : public IModuleInterface
{
};
IMPLEMENT_MODULE(FCrunchCompressionModule, CrunchCompression);

#if WITH_CRUNCH

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4018) // '<': signed/unsigned mismatch
#endif

//Crunch contains functions that are called 'check' and so they conflict with the UE check macro.
#undef check
#if WITH_EDITOR
#include "crnlib.h"
#endif
#include "crn_decomp.h"


#ifdef _MSC_VER
#pragma warning(pop)
#endif

#if WITH_CRUNCH_COMPRESSION
static FName NameDXT1(TEXT("DXT1"));
static FName NameDXT5(TEXT("DXT5"));
static FName NameBC4(TEXT("BC4"));
static FName NameBC5(TEXT("BC5"));

static crn_format GetCrnFormat(const FName& Format)
{
	if (Format == NameDXT1)			return cCRNFmtDXT1;
	else if (Format == NameDXT5)	return cCRNFmtDXT5;
	else if (Format == NameBC4)     return cCRNFmtDXT5A;
	else if (Format == NameBC5)     return cCRNFmtDXN_XY;
	else							return cCRNFmtInvalid;
}

bool CrunchCompression::IsValidFormat(const FName& Format)
{
	return GetCrnFormat(Format) != cCRNFmtInvalid;
}

bool CrunchCompression::Encode(const FCrunchEncodeParameters& Parameters, TArray<uint8>& OutCodecPayload, TArray<TArray<uint8>>& OutTilePayload)
{
	crn_comp_params CrunchParams;
	CrunchParams.clear();

	CrunchParams.m_width = Parameters.ImageWidth;
	CrunchParams.m_height = Parameters.ImageHeight;
	CrunchParams.m_levels = Parameters.RawImagesRGBA.Num();
	CrunchParams.set_flag(cCRNCompFlagPerceptual, Parameters.bIsGammaCorrected);
	CrunchParams.set_flag(cCRNCompFlagHierarchical, GAdaptiveBlockSizes);
	CrunchParams.set_flag(cCrnCompFlagUniformMips, true);
	CrunchParams.m_format = GetCrnFormat(Parameters.OutputFormat);
	CrunchParams.m_quality_level = FMath::Clamp<int32>((int32)((1.0f - Parameters.CompressionAmmount) * cCRNMaxQualityLevel), cCRNMinQualityLevel, cCRNMaxQualityLevel);
	CrunchParams.m_num_helper_threads = FMath::Min<uint32>(cCRNMaxHelperThreads, Parameters.NumWorkerThreads);
	CrunchParams.m_pProgress_func = nullptr;

	TArray<const uint32*> ConvertedImagePointers;
	ConvertedImagePointers.AddUninitialized(Parameters.RawImagesRGBA.Num());
	for (int SubImageIdx = 0; SubImageIdx < Parameters.RawImagesRGBA.Num(); ++SubImageIdx)
	{
		ConvertedImagePointers[SubImageIdx] = Parameters.RawImagesRGBA[SubImageIdx].GetData();
	}

	CrunchParams.m_pImages[0] = ConvertedImagePointers.GetData();

	crn_uint32 ActualQualityLevel;
	crn_uint32 OutputSize;
	float BitRate = 0;
	void* RawOutput = crn_compress(CrunchParams, OutputSize, &ActualQualityLevel, &BitRate);
	if (!RawOutput)
	{
		return false;
	}

	auto Cleanup = [RawOutput]() {
		crn_free_block(RawOutput);
	};
	
	crnd::crn_texture_info TexInfo;
	if (!crnd::crnd_get_texture_info(RawOutput, OutputSize, &TexInfo))
	{
		Cleanup();
		return false;
	}

	const uint32 headerSize = crnd::crnd_get_segmented_file_size(RawOutput, OutputSize);
	OutCodecPayload.AddUninitialized(headerSize);
	if (!crnd::crnd_create_segmented_file(RawOutput, OutputSize, OutCodecPayload.GetData(), headerSize))
	{
		Cleanup();
		return false;
	}
	
	const void* PixelData = crnd::crnd_get_level_data(RawOutput, OutputSize, 0, nullptr);
	if (!PixelData)
	{
		Cleanup();
		return false;
	}
	
	OutTilePayload.Reset(Parameters.RawImagesRGBA.Num());
	for (int SubImageIdx = 0; SubImageIdx < Parameters.RawImagesRGBA.Num(); ++SubImageIdx)
	{
		crnd::uint32 DataSize = 0;
		const void* LevelPixelData = crnd::crnd_get_level_data(RawOutput, OutputSize, SubImageIdx, &DataSize);
		OutTilePayload.Emplace((uint8*)LevelPixelData, DataSize);
	}

	Cleanup();

	return true;
}
#endif // WITH_CRUNCH_COMPRESSION

DECLARE_STATS_GROUP(TEXT("Crunch Memory"), STATGROUP_CrunchMemory, STATCAT_Advanced);
DECLARE_MEMORY_STAT(TEXT("Total Memory"), STAT_TotalMemory, STATGROUP_CrunchMemory);
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Total Allocations"), STAT_TotalAllocations, STATGROUP_CrunchMemory);

// Value exposed by crunch headers is inconsistent
static const uint32 CRUNCH_MIN_ALLOC_ALIGNMENT = 2 * sizeof(SIZE_T);

template<bool bEnableStats>
static void* CrunchReallocFunc(void* p, size_t size, size_t* pActual_size, bool movable, void* pUser_data)
{
	void* Result = nullptr;
	SIZE_T ResultSize = 0u;
	
	if (!p)
	{
		ensure(size > 0u);
		if (bEnableStats)
		{
			INC_DWORD_STAT(STAT_TotalAllocations);
		}
		Result = FMemory::Malloc(size, CRUNCH_MIN_ALLOC_ALIGNMENT);
		ResultSize = FMemory::GetAllocSize(Result);
	}
	else if (size == 0u)
	{
		if (bEnableStats)
		{
			DEC_DWORD_STAT(STAT_TotalAllocations);
			DEC_MEMORY_STAT_BY(STAT_TotalMemory, FMemory::GetAllocSize(p));
		}
		FMemory::Free(p);
	}
	else if (movable)
	{
		if (bEnableStats)
		{
			DEC_MEMORY_STAT_BY(STAT_TotalMemory, FMemory::GetAllocSize(p));
		}
		Result = FMemory::Realloc(p, size, CRUNCH_MIN_ALLOC_ALIGNMENT);
		ResultSize = FMemory::GetAllocSize(Result);
	}

	if (bEnableStats)
	{
		INC_MEMORY_STAT_BY(STAT_TotalMemory, ResultSize);
	}

	if (pActual_size)
	{
		*pActual_size = ResultSize;
	}

	return Result;
}

static size_t CrunchMSizeFunc(void* p, void* pUser_data)
{
	return p ? FMemory::GetAllocSize(p) : 0u;
}

struct FCrunchRegisterAllocators
{
	FCrunchRegisterAllocators()
	{
#if WITH_CRUNCH_COMPRESSION
		// Don't track stats for Crunch memory used by compressor, only interested in memory used at runtime by decompression
		crn_set_memory_callbacks(&CrunchReallocFunc<false>, &CrunchMSizeFunc, nullptr);
#endif // WITH_CRUNCH_COMPRESSION
		crnd::crnd_set_memory_callbacks(&CrunchReallocFunc<true>, &CrunchMSizeFunc, nullptr);
	}
};
static FCrunchRegisterAllocators gCrunchRegisterAllocators;

void* CrunchCompression::InitializeDecoderContext(const void* HeaderData, size_t HeaderDataSize)
{
	crnd::crnd_unpack_context CrunchContext = crnd::crnd_unpack_begin(HeaderData, HeaderDataSize);
	ensure(CrunchContext);
	return CrunchContext;
}

bool CrunchCompression::Decode(void* Context, const void* CompressedPixelData, uint32 Slice, void* OutUncompressedData, size_t DataSize, size_t UncompressedDataPitch)
{
	crnd::crnd_unpack_context CrunchContext = (crnd::crnd_unpack_context)Context;
	const bool bResult = crnd::crnd_unpack_level_segmented(CrunchContext, CompressedPixelData, Slice, (void**)&OutUncompressedData, DataSize, UncompressedDataPitch, 0);
	return bResult;
}

void CrunchCompression::DestroyDecoderContext(void* Context)
{
	crnd::crnd_unpack_context CrunchContext = (crnd::crnd_unpack_context)Context;
	crnd::crnd_unpack_end(CrunchContext);
}

#endif // WITH_CRUNCH
