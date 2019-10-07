// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LiveLinkTypes.h"
#include "Roles/LiveLinkBasicRole.h"
#include "LiveLinkTransformRole.generated.h"

/**
 * Role associated for Camera data.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Transform Role"))
class LIVELINKINTERFACE_API ULiveLinkTransformRole : public ULiveLinkBasicRole
{
	GENERATED_BODY()

public:
	virtual UScriptStruct* GetStaticDataStruct() const override;
	virtual UScriptStruct* GetFrameDataStruct() const override;
	virtual UScriptStruct* GetBlueprintDataStruct() const override;
	
	virtual bool InitializeBlueprintData(const FLiveLinkSubjectFrameData& InSourceData, FLiveLinkBlueprintDataStruct& OutBlueprintData) const override;

	virtual FText GetDisplayName() const override;
};

