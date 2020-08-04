// Copyright Epic Games, Inc. All Rights Reserved.

#include "LSALiveLinkFrameData.h"

FLSALiveLinkStaticData::FLSALiveLinkStaticData()
{
}

FLSALiveLinkStaticData::FLSALiveLinkStaticData(FLiveLinkSkeletonStaticData&& SkeletonData)
	: FLiveLinkSkeletonStaticData(MoveTemp(SkeletonData))
{
}

FLSALiveLinkFrameData::FLSALiveLinkFrameData()
{
}

FLSALiveLinkFrameData::FLSALiveLinkFrameData(
	FLiveLinkAnimationFrameData&& AnimFrameData,
	const FLSALiveLinkSourceOptions& InOptions,
	const FLiveStreamAnimationHandle& InTranslationProfileHandle)

	: FLiveLinkAnimationFrameData(MoveTemp(AnimFrameData))
	, Options(InOptions)
	, TranslationProfileHandle(InTranslationProfileHandle)
{
}