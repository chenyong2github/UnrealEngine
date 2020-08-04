// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Roles/LiveLinkBasicRole.h"
#include "LSALiveLinkFrameData.h"
#include "Internationalization/Internationalization.h"
#include "LSALiveLinkRole.generated.h"

UCLASS(BlueprintType, meta = (DisplayName = "Live Stream Animation Role"))
class LSALIVELINK_API ULSALiveLinkRole : public ULiveLinkBasicRole
{
	GENERATED_BODY()

public:

	virtual UScriptStruct* GetStaticDataStruct() const override
	{
		return FLSALiveLinkStaticData::StaticStruct();
	}

	virtual UScriptStruct* GetFrameDataStruct() const override
	{
		return FLSALiveLinkFrameData::StaticStruct();
	}

	virtual FText GetDisplayName() const override
	{
		return NSLOCTEXT("LSALiveLink", "LSA_DisplayName", "Live Stream Animation Live Link Role");
	}
};
