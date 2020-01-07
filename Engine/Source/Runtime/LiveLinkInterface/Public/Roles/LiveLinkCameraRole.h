// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Roles/LiveLinkTransformRole.h"
#include "LiveLinkCameraRole.generated.h"

/**
 * Role associated for Camera data.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Camera Role"))
class LIVELINKINTERFACE_API ULiveLinkCameraRole : public ULiveLinkTransformRole
{
	GENERATED_BODY()

public:
	virtual UScriptStruct* GetStaticDataStruct() const override;
	virtual UScriptStruct* GetFrameDataStruct() const override;
	virtual UScriptStruct* GetBlueprintDataStruct() const override;

	bool InitializeBlueprintData(const FLiveLinkSubjectFrameData& InSourceData, FLiveLinkBlueprintDataStruct& OutBlueprintData) const override;

	virtual FText GetDisplayName() const override;
};
