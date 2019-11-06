// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MoviePipelineLinearExecutor.h"
#include "MoviePipelineNewProcessExecutor.generated.h"

/**
* This is the implementation responsible for executing the rendering of
* multiple movie pipelines on the local machine in an external process.
* This simply handles launching and managing the external processes and 
* acts as a proxy to them where possible. This internally uses the
* UMoviePipelineInProcessExecutor on the launched instances.
*/
UCLASS(Blueprintable)
class MOVIERENDERPIPELINEEDITOR_API UMoviePipelineNewProcessExecutor : public UMoviePipelineLinearExecutorBase
{
	GENERATED_BODY()
	
public:
	UMoviePipelineNewProcessExecutor()
		: UMoviePipelineLinearExecutorBase()
	{
	}

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