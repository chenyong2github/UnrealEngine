// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MoviePipelineConfigBase.h"
#include "MoviePipelineRenderPass.h"

#include "MoviePipelineShotConfig.generated.h"


// Forward Declares
class ULevelSequence;
class UMoviePipelineSetting;
class UMoviePipelineRenderPass;
class UMoviePipelineOutputBase;

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
	virtual bool CanSettingBeAdded(UMoviePipelineSetting* InSetting) override
	{
		check(InSetting);
		return InSetting->IsValidOnShots();
	}

};