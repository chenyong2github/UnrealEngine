// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MovieSceneCameraShakeSection.h"
#include "MovieSceneCameraShakeSourceShakeSection.generated.h"

UCLASS(MinimalAPI)
class UMovieSceneCameraShakeSourceShakeSection : public UMovieSceneSection
{
	GENERATED_BODY()

public:
	UMovieSceneCameraShakeSourceShakeSection(const FObjectInitializer& ObjectInitializer);
	
	UPROPERTY(EditAnywhere, Category="Camera Shake", meta=(ShowOnlyInnerProperties))
	FMovieSceneCameraShakeSectionData ShakeData;
};

