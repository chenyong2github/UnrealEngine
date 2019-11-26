// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MoviePipelineConfigBase.h"

#include "MoviePipelineShotConfig.generated.h"


// Forward Declares
class UMoviePipelineRenderPass;

UCLASS(Blueprintable)
class MOVIERENDERPIPELINECORE_API UMoviePipelineShotConfig : public UMoviePipelineConfigBase
{
	GENERATED_BODY()
	
public:
	UMoviePipelineShotConfig()
	{
	}
	
	TArray<UMoviePipelineRenderPass*> GetRenderPasses() const;

protected:
	virtual bool CanSettingBeAdded(const UMoviePipelineSetting* InSetting) const override
	{
		check(InSetting);
		return InSetting->IsValidOnShots();
	}

};