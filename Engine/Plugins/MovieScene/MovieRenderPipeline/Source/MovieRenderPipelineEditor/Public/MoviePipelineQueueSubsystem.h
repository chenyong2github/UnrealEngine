// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "EditorSubsystem.h"
#include "MoviePipelineQueue.h"
#include "MoviePipelineQueueSubsystem.generated.h"

UCLASS(BlueprintType)
class MOVIERENDERPIPELINEEDITOR_API UMoviePipelineQueueSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()
	
public:

	UMoviePipelineQueueSubsystem()
	{
		CurrentQueue = CreateDefaultSubobject<UMoviePipelineQueue>("EditorMoviePipelineQueue");
	}
	
	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	UMoviePipelineQueue* GetQueue() const
	{
		return CurrentQueue;
	}

private:
	UPROPERTY(Transient, Instanced)
	UMoviePipelineQueue* CurrentQueue;
};