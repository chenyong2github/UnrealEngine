// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IExecution.h"

#include <memory>

namespace grpc
{
	class Channel;
	class ClientContext;
}

class FBazelCompletionQueueRunnable;
class FExecutionStub;


class FExecution : public IExecution
{
private:
	TUniquePtr<FExecutionStub> ExecutionStub;
	TSharedPtr<FBazelCompletionQueueRunnable> CompletionQueueRunnable;
	TMap<FString, FString> Headers;

public:
	FExecution(const std::shared_ptr<grpc::Channel>& Channel, TSharedPtr<FBazelCompletionQueueRunnable> CompletionQueueRunnable, const TMap<FString, FString>& Headers);
	~FExecution();

	bool Execute(const FExecuteRequest& Request, FExecuteResponse& Response, int64 TimeoutMs = 0) override;

	TFuture<FExecuteResponse> ExecuteAsync(const FExecuteRequest& Request, TUniqueFunction<void()> CompletionCallback = nullptr, int64 TimeoutMs = 0) override;

private:
	void PrepareContext(grpc::ClientContext& ClientContext, int64 TimeoutMs = 0) const;
};
