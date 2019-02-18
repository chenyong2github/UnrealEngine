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

FDerivedDataAnimationCompression::FDerivedDataAnimationCompression(UAnimSequence* InAnimSequence, TSharedPtr<FAnimCompressContext> InCompressContext, bool bInDoCompressionInPlace, bool bInTryFrameStripping, bool bTryStrippingOnOddFramedAnims)
	: OriginalAnimSequence(InAnimSequence)
	, DuplicateSequence(nullptr)
	, CompressContext(InCompressContext)
	, bDoCompressionInPlace(bInDoCompressionInPlace)
{
	check(InAnimSequence != nullptr && InAnimSequence->GetSkeleton() != nullptr);
	InAnimSequence->AddToRoot(); //Keep this around until we are finished

	// Can only do stripping on animations that have an even number of frames once the end frame is removed)
	bIsEvenFramed = ((OriginalAnimSequence->GetRawNumberOfFrames() - 1) % 2) == 0;
	const bool bIsValidForStripping = bIsEvenFramed || bTryStrippingOnOddFramedAnims;

	const bool bStripCandidate = (OriginalAnimSequence->GetRawNumberOfFrames() > 10) && bIsValidForStripping;
	
	bPerformStripping = bStripCandidate && bInTryFrameStripping;
}

FDerivedDataAnimationCompression::~FDerivedDataAnimationCompression()
{
	OriginalAnimSequence->RemoveFromRoot();
	if (DuplicateSequence)
	{
		DuplicateSequence->ClearFlags(RF_Standalone | RF_Public);
		DuplicateSequence->RemoveFromRoot();
		DuplicateSequence->MarkPendingKill();
	}
}

FString FDerivedDataAnimationCompression::GetPluginSpecificCacheKeySuffix() const
{
	enum { UE_ANIMCOMPRESSION_DERIVEDDATA_VER = 1 };

	const bool bCanBakeAdditive = OriginalAnimSequence->CanBakeAdditive();
	UAnimSequence* AdditiveBase = OriginalAnimSequence->RefPoseSeq;

	//Make up our content key consisting of:
	//	* Our plugin verison
	//	* Global animation compression version
	//	* Our raw data GUID
	//	* Our skeleton GUID: If our skeleton changes our compressed data may now be stale
	//	* Baked Additive Flag
	//	* Additive ref pose GUID or hardcoded string if not available
	//	* Compression Settings
	//	* Curve compression settings

	uint8 AdditiveSettings = bCanBakeAdditive ? (OriginalAnimSequence->RefPoseType << 4) + OriginalAnimSequence->AdditiveAnimType : 0;

	char AdditiveType = bCanBakeAdditive ? NibbleToTChar(OriginalAnimSequence->AdditiveAnimType) : '0';
	char RefType = bCanBakeAdditive ? NibbleToTChar(OriginalAnimSequence->RefPoseType) : '0';

	const int32 StripFrame = bPerformStripping ? 1 : 0;

	FString Ret = FString::Printf(TEXT("%i_%i_%i_%i_%s%s%s_%c%c%i_%s_%s_%s"),
		(int32)UE_ANIMCOMPRESSION_DERIVEDDATA_VER,
		(int32)CURRENT_ANIMATION_ENCODING_PACKAGE_VERSION,
		OriginalAnimSequence->CompressCommandletVersion,
		StripFrame,
		*OriginalAnimSequence->GetRawDataGuid().ToString(),
		*OriginalAnimSequence->GetSkeleton()->GetGuid().ToString(),
		*OriginalAnimSequence->GetSkeleton()->GetVirtualBoneGuid().ToString(),
		AdditiveType,
		RefType,
		OriginalAnimSequence->RefFrameIndex,
		(bCanBakeAdditive && AdditiveBase) ? *AdditiveBase->GetRawDataGuid().ToString() : TEXT("NoAdditiveBase"),
		*OriginalAnimSequence->CompressionScheme->MakeDDCKey(),
		*OriginalAnimSequence->CurveCompressionSettings->MakeDDCKey()
		);

	return Ret;
}

bool FDerivedDataAnimationCompression::Build( TArray<uint8>& OutData )
{
	SCOPE_CYCLE_COUNTER(STAT_AnimCompressionDerivedData);
	UE_LOG(LogAnimationCompression, Log, TEXT("Building Anim DDC data for %s"), *OriginalAnimSequence->GetFullName());
	check(OriginalAnimSequence != NULL);

	UAnimSequence* AnimToOperateOn;

	if (!bDoCompressionInPlace)
	{
		DuplicateSequence = DuplicateObject<UAnimSequence>(OriginalAnimSequence, GetTransientPackage(), OriginalAnimSequence->GetFName());
		DuplicateSequence->AddToRoot();
		AnimToOperateOn = DuplicateSequence;
	}
	else
	{
		AnimToOperateOn = OriginalAnimSequence;
	}

	bool bCompressionSuccessful = false;
	{
		FScopedAnimSequenceRawDataCache RawDataCache;
		const bool bHasVirtualBones = AnimToOperateOn->GetSkeleton()->GetVirtualBones().Num() > 0;
		const bool bNeedToModifyRawData = AnimToOperateOn->CanBakeAdditive() || bHasVirtualBones || bPerformStripping;
		if (bDoCompressionInPlace && bNeedToModifyRawData)
		{
			//Cache original raw data before we mess with it
			RawDataCache.InitFrom(AnimToOperateOn);
		}

		if (AnimToOperateOn->CanBakeAdditive())
		{
			AnimToOperateOn->BakeOutAdditiveIntoRawData();
		}
		else if(bHasVirtualBones)// If we aren't additive we must bake virtual bones
		{
			AnimToOperateOn->BakeOutVirtualBoneTracks();
		}

		if (bPerformStripping)
		{
			const int32 NumFrames = AnimToOperateOn->GetRawNumberOfFrames();
			const int32 NumTracks = AnimToOperateOn->GetRawAnimationData().Num();

			//Strip every other frame from tracks
			if(bIsEvenFramed)
			{
				for (int32 TrackIndex = 0; TrackIndex < NumTracks; ++TrackIndex)
				{
					FRawAnimSequenceTrack& Track = AnimToOperateOn->GetRawAnimationTrack(TrackIndex);

					StripFramesEven(Track.PosKeys, NumFrames);
					StripFramesEven(Track.RotKeys, NumFrames);
					StripFramesEven(Track.ScaleKeys, NumFrames);
				}

				const int32 ActualFrames = AnimToOperateOn->GetRawNumberOfFrames() - 1; // strip bookmark end frame
				AnimToOperateOn->SetRawNumberOfFrame((ActualFrames / 2) + 1);
			}
			else
			{
				for (int32 TrackIndex = 0; TrackIndex < NumTracks; ++TrackIndex)
				{
					FRawAnimSequenceTrack& Track = AnimToOperateOn->GetRawAnimationTrack(TrackIndex);

					StripFramesOdd(Track.PosKeys, NumFrames);
					StripFramesOdd(Track.RotKeys, NumFrames);
					StripFramesOdd(Track.ScaleKeys, NumFrames);
				}

				const int32 ActualFrames = AnimToOperateOn->GetRawNumberOfFrames(); // strip bookmark end frame
				AnimToOperateOn->SetRawNumberOfFrame((ActualFrames / 2));
			}
		}

		AnimToOperateOn->UpdateCompressedTrackMapFromRaw();
		AnimToOperateOn->UpdateCompressedCurveNames();

		bool bCurveCompressionSuccess = FAnimationUtils::CompressAnimCurves(*AnimToOperateOn);

#if DO_CHECK
		FString CompressionName = AnimToOperateOn->CompressionScheme->GetFullName();
		const TCHAR* AAC = CompressContext.Get()->bAllowAlternateCompressor ? TEXT("true") : TEXT("false");
		const TCHAR* OutputStr = CompressContext.Get()->bOutput ? TEXT("true") : TEXT("false");
#endif

		AnimToOperateOn->UpdateCompressedNumFramesFromRaw(); //Do this before compression so compress code can read the correct value

		FAnimationUtils::CompressAnimSequence(AnimToOperateOn, *CompressContext.Get());
		bCompressionSuccessful = AnimToOperateOn->IsCompressedDataValid() && bCurveCompressionSuccess;

		ensureMsgf(bCompressionSuccessful, TEXT("Anim Compression failed for Sequence '%s' with compression scheme '%s': compressed data empty\n\tAnimIndex: %i\n\tMaxAnim:%i\n\tAllowAltCompressor:%s\n\tOutput:%s"), 
											*AnimToOperateOn->GetFullName(), 
											*CompressionName,
											CompressContext.Get()->AnimIndex,
											CompressContext.Get()->MaxAnimations,
											AAC,
											OutputStr);

		AnimToOperateOn->CompressedRawDataSize = AnimToOperateOn->GetApproxRawSize();
		AnimToOperateOn->TestEvalauteAnimation(); //Validate that compressed data is readable. 
	}

	//Our compression scheme may change so copy the new one back
	if (OriginalAnimSequence != AnimToOperateOn)
	{
		CA_SUPPRESS(6011); // See https://connect.microsoft.com/VisualStudio/feedback/details/3007725
		OriginalAnimSequence->CompressionScheme = static_cast<UAnimCompress*>(StaticDuplicateObject(AnimToOperateOn->CompressionScheme, OriginalAnimSequence));
		OriginalAnimSequence->CurveCompressionSettings = AnimToOperateOn->CurveCompressionSettings;
	}

	if (bCompressionSuccessful)
	{
		AnimToOperateOn->SetSkeletonVirtualBoneGuid(AnimToOperateOn->GetSkeleton()->GetVirtualBoneGuid());
		FMemoryWriter Ar(OutData, true);
		AnimToOperateOn->SerializeCompressedData(Ar, true); //Save out compressed
	}

	return bCompressionSuccessful;
}
#endif	//WITH_EDITOR
