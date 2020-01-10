// Copyright Epic Games, Inc. All Rights Reserved.


#include "Animation/AnimCompress_Automatic.h"
#include "Animation/AnimationSettings.h"

UAnimCompress_Automatic::UAnimCompress_Automatic(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Description = TEXT("Automatic");
	UAnimationSettings* AnimationSettings = UAnimationSettings::Get();
	MaxEndEffectorError = AnimationSettings->AlternativeCompressionThreshold;
	bRunCurrentDefaultCompressor = AnimationSettings->bFirstRecompressUsingCurrentOrDefault;
	bAutoReplaceIfExistingErrorTooGreat = AnimationSettings->bForceBelowThreshold;
	bRaiseMaxErrorToExisting = AnimationSettings->bRaiseMaxErrorToExisting;
	bTryExhaustiveSearch = AnimationSettings->bTryExhaustiveSearch;
}

#if WITH_EDITOR
void UAnimCompress_Automatic::DoReduction(const FCompressibleAnimData& CompressibleAnimData, FCompressibleAnimDataResult& OutResult)
{
	FAnimCompressContext CompressContext(MaxEndEffectorError > 0.0f, false);
#if WITH_EDITORONLY_DATA
	FAnimationUtils::CompressAnimSequenceExplicit(
		CompressibleAnimData,
		OutResult,
		CompressContext,
		MaxEndEffectorError,
		bRunCurrentDefaultCompressor,
		bAutoReplaceIfExistingErrorTooGreat,
		bRaiseMaxErrorToExisting,
		bTryExhaustiveSearch,
		bEnableSegmenting,
		IdealNumFramesPerSegment,
		MaxNumFramesPerSegment);
#endif // WITH_EDITORONLY_DATA
}

void UAnimCompress_Automatic::PopulateDDCKey(FArchive& Ar)
{
	Super::PopulateDDCKey(Ar);

	Ar << MaxEndEffectorError;

	uint8 Flags =	MakeBitForFlag(bRunCurrentDefaultCompressor, 0) +
					MakeBitForFlag(bAutoReplaceIfExistingErrorTooGreat, 1) +
					MakeBitForFlag(bRaiseMaxErrorToExisting, 2) +
					MakeBitForFlag(bTryExhaustiveSearch, 3);
	Ar << Flags;
}

#endif // WITH_EDITOR
