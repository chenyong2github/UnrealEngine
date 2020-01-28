// Copyright Epic Games, Inc. All Rights Reserved.
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
class MOVIERENDERPIPELINEEDITOR_API UMoviePipelineNewProcessExecutor : public UMoviePipelineExecutorBase
{
	GENERATED_BODY()

	// UMoviePipelineExecutorBase Interface
	virtual void ExecuteImpl(UMoviePipelineQueue* InPipelineQueue) override;
	virtual bool IsRenderingImpl() const override { return ProcessHandle.IsValid(); }
	// ~UMoviePipelineExecutorBase Interface

protected:
	void CheckForProcessFinished();

protected:
	/** A handle to the currently running process (if any) for the active job. */
	FProcHandle ProcessHandle;

};