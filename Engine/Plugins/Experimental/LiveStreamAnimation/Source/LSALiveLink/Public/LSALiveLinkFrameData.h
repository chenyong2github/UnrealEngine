// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Roles/LiveLinkAnimationTypes.h"
#include "LSALiveLinkSourceOptions.h"
#include "LiveStreamAnimationHandle.h"
#include "LSALiveLinkFrameData.generated.h"

USTRUCT(BlueprintType)
struct LSALIVELINK_API FLSALiveLinkStaticData : public FLiveLinkSkeletonStaticData
{
	GENERATED_BODY()

public:

	FLSALiveLinkStaticData();

	FLSALiveLinkStaticData(FLiveLinkSkeletonStaticData&& SkeletonData);
};

USTRUCT(BlueprintType)
struct LSALIVELINK_API FLSALiveLinkFrameData : public FLiveLinkAnimationFrameData
{
	GENERATED_BODY()

public:

	FLSALiveLinkFrameData();

	FLSALiveLinkFrameData(
		FLiveLinkAnimationFrameData&& AnimFrameData,
		const FLSALiveLinkSourceOptions& InOptions,
		const FLiveStreamAnimationHandle& InTranslationProfileHandle);

	FLSALiveLinkSourceOptions Options;
	FLiveStreamAnimationHandle TranslationProfileHandle;
};