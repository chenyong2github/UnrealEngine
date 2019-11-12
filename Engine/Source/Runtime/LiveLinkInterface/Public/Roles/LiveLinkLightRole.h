// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Roles/LiveLinkTransformRole.h"
#include "LiveLinkLightRole.generated.h"

/**
 * Role associated for Light data.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Light Role"))
class LIVELINKINTERFACE_API ULiveLinkLightRole : public ULiveLinkTransformRole
{
	GENERATED_BODY()

public:
	virtual UScriptStruct* GetStaticDataStruct() const override;
	virtual UScriptStruct* GetFrameDataStruct() const override;
	virtual UScriptStruct* GetBlueprintDataStruct() const override;

	bool InitializeBlueprintData(const FLiveLinkSubjectFrameData& InSourceData, FLiveLinkBlueprintDataStruct& OutBlueprintData) const override;

	virtual FText GetDisplayName() const override;
};
