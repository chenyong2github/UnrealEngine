// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MoviePipelineExecutor.h"
#include "MovieRenderPipelineCoreModule.h"
#include "MoviePipelinePythonHostExecutor.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMoviePipelineSocketMessageRecieved, const FString&, Message);

/**
* This is a dummy executor that is designed to host a executor implemented in
* python. Python defined UClasses are not available when the executor is initialized
* and not all callbacks are available in Python. By inheriting from this in Python
* and overriding which UClass to latently spawn, this class can just forward certain
* events onto Python (by overriding the relevant function).
*/
UCLASS(Blueprintable)
class MOVIERENDERPIPELINECORE_API UMoviePipelinePythonHostExecutor : public UMoviePipelineExecutorBase
{
	GENERATED_BODY()

public:
	UMoviePipelinePythonHostExecutor()
		: UMoviePipelineExecutorBase()
	{
		ExternalSocket  = nullptr;
		bSocketConnected = false;
	}

	virtual void Execute_Implementation(UMoviePipelineQueue* InPipelineQueue) override;

	// Python/Blueprint API
	UFUNCTION(BlueprintNativeEvent, Category = "Movie Render Pipeline")
	void ExecuteDelayed(UMoviePipelineQueue* InPipelineQueue);
	virtual void ExecuteDelayed_Implementation(UMoviePipelineQueue* InPipelineQueue) {}

	UFUNCTION(BlueprintNativeEvent, Category = "Movie Render Pipeline")
	void OnTick();
	virtual void OnTick_Implementation() {}

	UFUNCTION(BlueprintNativeEvent, Category = "Movie Render Pipeline")
	void OnMapLoad(UWorld* InWorld);
	virtual void OnMapLoad_Implementation(UWorld* InWorld) {}
	// ~Python/Blueprint API

protected:
	UFUNCTION(BlueprintCallable, Category = "Movie Render Pipeline")
	void SendSocketMessage(const FString& InMessage);
	
	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	UWorld* GetLastLoadedWorld() const { return LastLoadedWorld; }

private:
	void OnMapLoadFinished(UWorld* NewWorld);
	void OnBeginFrame();
	
public:
	UPROPERTY(BlueprintReadWrite, Category = "Movie Render Pipeline")
	TSubclassOf<UMoviePipelinePythonHostExecutor> ExecutorClass;
	
	UPROPERTY(BlueprintReadWrite, Transient, Category = "Movie Render Pipeline")
	UMoviePipelineQueue* PipelineQueue;
	
private:
	UPROPERTY(BlueprintAssignable)
	FMoviePipelineSocketMessageRecieved OnSocketMessageRecieved;

	UPROPERTY(Transient)
	UWorld* LastLoadedWorld;
	
	class FSocket* ExternalSocket;
	bool bSocketConnected;
};