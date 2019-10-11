// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LiveLinkRole.h"
#include "LiveLinkTypes.h"
#include "LiveLinkBasicRole.generated.h"

/**
 * Role associated for no specific role data.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Basic Role"))
class LIVELINKINTERFACE_API ULiveLinkBasicRole : public ULiveLinkRole
{
	GENERATED_BODY()

public:
	virtual UScriptStruct* GetStaticDataStruct() const override;
	virtual UScriptStruct* GetFrameDataStruct() const override;
	virtual UScriptStruct* GetBlueprintDataStruct() const override;
	
	virtual bool InitializeBlueprintData(const FLiveLinkSubjectFrameData& InSourceData, FLiveLinkBlueprintDataStruct& OutBlueprintData) const override;

	virtual FText GetDisplayName() const override;
	virtual bool IsFrameDataValid(const FLiveLinkStaticDataStruct& InStaticData, const FLiveLinkFrameDataStruct& InFrameData, bool& bOutShouldLogWarning) const override;
};
