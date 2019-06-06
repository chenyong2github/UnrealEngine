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

template<typename ArrayValue>
void StripFramesEven(TArray<ArrayValue>& Keys, const int32 NumFrames)
{
	if (Keys.Num() > 1)
	{
		check(Keys.Num() == NumFrames);

		for (int32 DstKey = 1, SrcKey = 2; SrcKey < NumFrames; ++DstKey, SrcKey+=2)
		{
			Keys[DstKey] = Keys[SrcKey];
		}

		const int32 HalfSize = (NumFrames - 1) / 2;
		const int32 StartRemoval = HalfSize + 1;

		Keys.RemoveAt(StartRemoval, NumFrames - StartRemoval);
	}
}

template<typename ArrayValue>
void StripFramesOdd(TArray<ArrayValue>& Keys, const int32 NumFrames)
{
	if (Keys.Num() > 1)
	{
		const int32 NewNumFrames = NumFrames / 2;

		TArray<ArrayValue> NewKeys;
		NewKeys.Reserve(NewNumFrames);

		check(Keys.Num() == NumFrames);

		NewKeys.Add(Keys[0]); //Always keep first 

		//Always keep first and last
		const int32 NumFramesToCalculate = NewNumFrames - 2;

		// Frame increment is ratio of old frame spaces vs new frame spaces 
		const double FrameIncrement = (double)(NumFrames - 1) / (double)(NewNumFrames - 1);
			
		for(int32 Frame=0; Frame < NumFramesToCalculate; ++Frame)
		{
			const double NextFramePosition = FrameIncrement * (Frame + 1);
			const int32 Frame1 = (int32)NextFramePosition;
			const float Alpha = (NextFramePosition - (double)Frame1);

			NewKeys.Add(AnimationCompressionUtils::Interpolate(Keys[Frame1], Keys[Frame1 + 1], Alpha));

		}

		NewKeys.Add(Keys.Last()); // Always Keep Last

		const int32 HalfSize = (NumFrames - 1) / 2;
		const int32 StartRemoval = HalfSize + 1;

		Keys = MoveTemp(NewKeys);
	}
}

FDerivedDataAnimationCompression::FDerivedDataAnimationCompression(const FCompressibleAnimData& InDataToCompress, TSharedPtr<FAnimCompressContext> InCompressContext, int32 InPreviousCompressedSize, bool bInTryFrameStripping, bool bTryStrippingOnOddFramedAnims)
	: DataToCompress(InDataToCompress)
	, CompressContext(InCompressContext)
	, PreviousCompressedSize(InPreviousCompressedSize)
{
	check(DataToCompress.Skeleton != nullptr);

	// Can only do stripping on animations that have an even number of frames once the end frame is removed)
	bIsEvenFramed = ((DataToCompress.NumFrames - 1) % 2) == 0;
	const bool bIsValidForStripping = bIsEvenFramed || bTryStrippingOnOddFramedAnims;

	const bool bStripCandidate = (DataToCompress.NumFrames > 10) && bIsValidForStripping;
	
	bPerformStripping = bStripCandidate && bInTryFrameStripping;
}

FDerivedDataAnimationCompression::~FDerivedDataAnimationCompression()
{
}

FString FDerivedDataAnimationCompression::GetPluginSpecificCacheKeySuffix() const
{
	enum { UE_ANIMCOMPRESSION_DERIVEDDATA_VER = 1 };

	//Make up our content key consisting of:
	//	* Our plugin version
	//	* Global animation compression version
	//	* Our raw data GUID
	//	* Our skeleton GUID: If our skeleton changes our compressed data may now be stale
	//	* Baked Additive Flag
	//	* Additive ref pose GUID or hardcoded string if not available
	//	* Compression Settings
	//	* Curve compression settings

	char AdditiveType = DataToCompress.bIsValidAdditive ? NibbleToTChar(DataToCompress.AdditiveAnimType) : '0';
	char RefType = DataToCompress.bIsValidAdditive ? NibbleToTChar(DataToCompress.RefPoseType) : '0';

	const int32 StripFrame = bPerformStripping ? 1 : 0;

	FString Ret = FString::Printf(TEXT("%i_%i_%i_%i_%s%s%s_%c%c%i_%s_%s_%s"),
		(int32)UE_ANIMCOMPRESSION_DERIVEDDATA_VER,
		(int32)CURRENT_ANIMATION_ENCODING_PACKAGE_VERSION,
		DataToCompress.CompressCommandletVersion,
		StripFrame,
		*DataToCompress.RawDataGuid.ToString(),
		*DataToCompress.Skeleton->GetGuid().ToString(),
		*DataToCompress.Skeleton->GetVirtualBoneGuid().ToString(),
		AdditiveType,
		RefType,
		DataToCompress.RefFrameIndex,
		(DataToCompress.bIsValidAdditive) ? *DataToCompress.AdditiveDataGuid.ToString() : TEXT("NotAdditive"),
		*DataToCompress.RequestedCompressionScheme->MakeDDCKey(),
		*DataToCompress.CurveCompressionSettings->MakeDDCKey()
		);

	return Ret;
}

bool FDerivedDataAnimationCompression::Build( TArray<uint8>& OutDataArray )
{
	FCompressedAnimSequence OutData;

	SCOPE_CYCLE_COUNTER(STAT_AnimCompressionDerivedData);
	UE_LOG(LogAnimationCompression, Log, TEXT("Building Anim DDC data for %s"), *DataToCompress.FullName);

	FCompressibleAnimDataResult CompressionResult;

	bool bCompressionSuccessful = false;
	{
		if (bPerformStripping)
		{
			const int32 NumFrames = DataToCompress.NumFrames;
			const int32 NumTracks = DataToCompress.RawAnimationData.Num();

			//Strip every other frame from tracks
			if(bIsEvenFramed)
			{
				for (FRawAnimSequenceTrack& Track : DataToCompress.RawAnimationData)
				{
					StripFramesEven(Track.PosKeys, NumFrames);
					StripFramesEven(Track.RotKeys, NumFrames);
					StripFramesEven(Track.ScaleKeys, NumFrames);
				}

				const int32 ActualFrames = DataToCompress.NumFrames - 1; // strip bookmark end frame
				DataToCompress.NumFrames = (ActualFrames / 2) + 1;
			}
			else
			{
				for (FRawAnimSequenceTrack& Track : DataToCompress.RawAnimationData)
				{
					StripFramesOdd(Track.PosKeys, NumFrames);
					StripFramesOdd(Track.RotKeys, NumFrames);
					StripFramesOdd(Track.ScaleKeys, NumFrames);
				}

				const int32 ActualFrames = DataToCompress.NumFrames;
				DataToCompress.NumFrames = (ActualFrames / 2);
			}
		}

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
		OutData.SerializeCompressedData(Ar, true, nullptr, DataToCompress.CurveCompressionSettings); //Save out compressed
	}

	return bCompressionSuccessful;
}
#endif	//WITH_EDITOR
