// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MoviePipelineSetting.h"
#include "MoviePipelineSeparateProcessSetting.generated.h"

UCLASS(Blueprintable, MinimalAPI)
class UMoviePipelineSeparateProcessSetting : public UMoviePipelineSetting
{
	GENERATED_BODY()
public:
	
	/** If enabled the editor will close itself when a new process is started. This can be used to gain some performance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movie Pipeline")
	bool bCloseEditor;
	
	/** A list of additional command line arguments to be appended to the new process startup. Can be useful if your game requires certain arguments to start such as disabling log-in screens. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movie Pipeline")
	FString AdditionalCommandLineArguments;
	
	/** A list of command line arguments which are inherited from the currently running Editor instance that will be automatically appended to the new process. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movie Pipeline")
	FString InheritedCommandLineArguments;
};