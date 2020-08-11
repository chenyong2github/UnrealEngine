// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Roles/LiveLinkBasicRole.h"
#include "LiveLink/LiveStreamAnimationLiveLinkFrameData.h"
#include "Internationalization/Internationalization.h"
#include "LiveStreamAnimationLiveLinkRole.generated.h"

UCLASS(BlueprintType, meta = (DisplayName = "Live Stream Animation Role"))
class LIVESTREAMANIMATION_API ULiveStreamAnimationLiveLinkRole : public ULiveLinkBasicRole
{
	GENERATED_BODY()

public:

	virtual UScriptStruct* GetStaticDataStruct() const override
	{
		return FLiveStreamAnimationLiveLinkStaticData::StaticStruct();
	}

	virtual UScriptStruct* GetFrameDataStruct() const override
	{
		return FLiveStreamAnimationLiveLinkFrameData::StaticStruct();
	}

	virtual FText GetDisplayName() const override
	{
		return NSLOCTEXT("LiveStreamAnimation", "LSA_DisplayName", "Live Stream Animation Live Link Role");
	}
};
