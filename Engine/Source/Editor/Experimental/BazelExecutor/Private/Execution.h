// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IExecution.h"

THIRD_PARTY_INCLUDES_START
UE_PUSH_MACRO("TEXT")
#undef TEXT
#include "build\bazel\remote\execution\v2\remote_execution.grpc.pb.h"
UE_POP_MACRO("TEXT");
THIRD_PARTY_INCLUDES_END

class FBazelCompletionQueueRunnable;


class FExecution : public IExecution
{
private:
	TUniquePtr<build::bazel::remote::execution::v2::Execution::Stub> Stub;
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
