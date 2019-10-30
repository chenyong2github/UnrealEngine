// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MoviePipelineSetting.h"
#include "LevelSequenceActor.h"
#include "MoviePipelineBurnInSetting.generated.h"

UCLASS(Blueprintable, meta=(MovieRenderPipelineDisplayName="Burn In"))
class UMoviePipelineBurnInSetting : public UMoviePipelineSetting
{
	GENERATED_BODY()
public:
	virtual FText GetDisplayText() const override { return NSLOCTEXT("MovieRenderPipeline", "BurnInSettingDisplayName", "Burn In"); }
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(MetaClass="LevelSequenceBurnIn"), Category = "Movie Pipeline")
	FSoftClassPath BurnInClass;

	UPROPERTY(Instanced, EditAnywhere, BlueprintReadWrite, Category="Movie Pipeline")
	ULevelSequenceBurnInInitSettings* Settings;
};