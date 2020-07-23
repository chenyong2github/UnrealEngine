// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLink/LiveStreamAnimationLiveLinkFrameData.h"

FLiveStreamAnimationLiveLinkStaticData::FLiveStreamAnimationLiveLinkStaticData()
{
}

FLiveStreamAnimationLiveLinkStaticData::FLiveStreamAnimationLiveLinkStaticData(FLiveLinkSkeletonStaticData&& SkeletonData)
	: FLiveLinkSkeletonStaticData(MoveTemp(SkeletonData))
{
}

FLiveStreamAnimationLiveLinkFrameData::FLiveStreamAnimationLiveLinkFrameData()
{
}

FLiveStreamAnimationLiveLinkFrameData::FLiveStreamAnimationLiveLinkFrameData(
	FLiveLinkAnimationFrameData&& AnimFrameData,
	const FLiveStreamAnimationLiveLinkSourceOptions& InOptions,
	const FLiveStreamAnimationHandle& InTranslationProfileHandle)

	: FLiveLinkAnimationFrameData(MoveTemp(AnimFrameData))
	, Options(InOptions)
	, TranslationProfileHandle(InTranslationProfileHandle)
{
}