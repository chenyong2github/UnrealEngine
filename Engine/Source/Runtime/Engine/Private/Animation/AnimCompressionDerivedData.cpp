// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Animation/AnimCompressionDerivedData.h"
#include "Stats/Stats.h"
#include "Animation/AnimSequence.h"
#include "Serialization/MemoryWriter.h"
#include "AnimationUtils.h"
#include "AnimEncoding.h"
#include "Animation/AnimCompress.h"
#include "Animation/AnimCurveCompressionSettings.h"
#include "AnimationCompression.h"
#include "UObject/Package.h"

#if WITH_EDITOR

DECLARE_CYCLE_STAT(TEXT("Anim Compression (Derived Data)"), STAT_AnimCompressionDerivedData, STATGROUP_Anim);

FDerivedDataAnimationCompression::FDerivedDataAnimationCompression(const TCHAR* InTypeName, const FString& InAssetDDCKey, TSharedPtr<FAnimCompressContext> InCompressContext, int32 InPreviousCompressedSize)
	: TypeName(InTypeName)
	, AssetDDCKey(InAssetDDCKey)
	, CompressContext(InCompressContext)
	, PreviousCompressedSize(InPreviousCompressedSize)
{

}

FDerivedDataAnimationCompression::~FDerivedDataAnimationCompression()
{
}

const TCHAR* FDerivedDataAnimationCompression::GetVersionString() const
{
	// This is a version string that mimics the old versioning scheme. If you
	// want to bump this version, generate a new guid using VS->Tools->Create GUID and
	// return it here. Ex.
	return TEXT("2E79BF10172A48FDACA76883B8951538");
}

bool FDerivedDataAnimationCompression::Build( TArray<uint8>& OutDataArray )
{
	check(DataToCompressPtr.IsValid());
	FCompressibleAnimData& DataToCompress = *DataToCompressPtr.Get();
	FCompressedAnimSequence OutData;

	SCOPE_CYCLE_COUNTER(STAT_AnimCompressionDerivedData);
	UE_LOG(LogAnimationCompression, Log, TEXT("Building Anim DDC data for %s"), *DataToCompress.FullName);

	FCompressibleAnimDataResult CompressionResult;

	bool bCompressionSuccessful = false;
	{
		DataToCompress.Update(OutData);

		bool bCurveCompressionSuccess = FAnimationUtils::CompressAnimCurves(DataToCompress, OutData);

#if DO_CHECK
		FString CompressionName = DataToCompress.RequestedCompressionScheme->GetFullName();
		const TCHAR* AAC = CompressContext.Get()->bAllowAlternateCompressor ? TEXT("true") : TEXT("false");
		const TCHAR* OutputStr = CompressContext.Get()->bOutput ? TEXT("true") : TEXT("false");
#endif

		CompressionResult.CompressedNumberOfFrames = DataToCompress.NumFrames; //Do this before compression so compress code can read the correct value

		CompressContext->GatherPreCompressionStats(DataToCompress, PreviousCompressedSize);

		FAnimationUtils::CompressAnimSequence(DataToCompress, CompressionResult, *CompressContext.Get());
		bCompressionSuccessful = (CompressionResult.IsCompressedDataValid() || DataToCompress.RawAnimationData.Num() == 0) && bCurveCompressionSuccess;

		ensureMsgf(bCompressionSuccessful, TEXT("Anim Compression failed for Sequence '%s' with compression scheme '%s': compressed data empty\n\tAnimIndex: %i\n\tMaxAnim:%i\n\tAllowAltCompressor:%s\n\tOutput:%s"), 
											*DataToCompress.FullName,
											*CompressionName,
											CompressContext.Get()->AnimIndex,
											CompressContext.Get()->MaxAnimations,
											AAC,
											OutputStr);

		if (CompressionResult.IsCompressedDataValid())
		{
			CompressionResult.BuildFinalBuffer(OutData.CompressedByteStream); // Build final compressed data buffer

			OutData.CompressedDataStructure.CopyFrom(CompressionResult); //Copy header info
			OutData.CompressedDataStructure.InitViewsFromBuffer(OutData.CompressedByteStream); //Init views to CompressedByteStream
			
		}
	}

	if (bCompressionSuccessful)
	{
		FMemoryWriter Ar(OutDataArray, true);
		OutData.SerializeCompressedData(Ar, true, nullptr, DataToCompress.Skeleton, DataToCompress.CurveCompressionSettings); //Save out compressed
	}

	return bCompressionSuccessful;
}
#endif	//WITH_EDITOR
