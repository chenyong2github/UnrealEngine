// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"

struct FExecuteRequest;
struct FExecuteResponse;


class IExecution
{
public:
	/** Virtual destructor */
	virtual ~IExecution() {}

	virtual bool Execute(const FExecuteRequest& Request, FExecuteResponse& Response, int64 TimeoutMs = 0) = 0;

	virtual TFuture<FExecuteResponse> ExecuteAsync(const FExecuteRequest& Request, TUniqueFunction<void()> CompletionCallback = nullptr, int64 TimeoutMs = 0) = 0;
};
