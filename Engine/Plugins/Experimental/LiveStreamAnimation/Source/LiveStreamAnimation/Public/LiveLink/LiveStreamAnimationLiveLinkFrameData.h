// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Roles/LiveLinkAnimationTypes.h"
#include "LiveLink/LiveStreamAnimationLiveLinkSourceOptions.h"
#include "LiveStreamAnimationHandle.h"
#include "LiveStreamAnimationLiveLinkFrameData.generated.h"

USTRUCT(BlueprintType)
struct LIVESTREAMANIMATION_API FLiveStreamAnimationLiveLinkStaticData : public FLiveLinkSkeletonStaticData
{
	GENERATED_BODY()

public:

	FLiveStreamAnimationLiveLinkStaticData();

	FLiveStreamAnimationLiveLinkStaticData(FLiveLinkSkeletonStaticData&& SkeletonData);
};

USTRUCT(BlueprintType)
struct LIVESTREAMANIMATION_API FLiveStreamAnimationLiveLinkFrameData : public FLiveLinkAnimationFrameData
{
	GENERATED_BODY()

public:

	FLiveStreamAnimationLiveLinkFrameData();

	FLiveStreamAnimationLiveLinkFrameData(
		FLiveLinkAnimationFrameData&& AnimFrameData,
		const FLiveStreamAnimationLiveLinkSourceOptions& InOptions,
		const FLiveStreamAnimationHandle& InTranslationProfileHandle);

	UPROPERTY()
	FLiveStreamAnimationLiveLinkSourceOptions Options;

	UPROPERTY()
	FLiveStreamAnimationHandle TranslationProfileHandle;
};