// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InterchangePipelineConfigurationBase.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "InterchangePipelineConfigurationGeneric.generated.h"

UCLASS(BlueprintType, Blueprintable)
class INTERCHANGEEDITORPIPELINES_API UInterchangePipelineConfigurationGeneric : public UInterchangePipelineConfigurationBase
{
	GENERATED_BODY()

public:

protected:

	virtual EInterchangePipelineConfigurationDialogResult ShowPipelineConfigurationDialog() override;
};
