// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IExecution.h"
#include "Messages.h"

THIRD_PARTY_INCLUDES_START
UE_PUSH_MACRO("TEXT")
#undef TEXT
#include <memory>
#include "build\bazel\remote\execution\v2\remote_execution.grpc.pb.h"
UE_POP_MACRO("TEXT");
THIRD_PARTY_INCLUDES_END

namespace grpc
{
	class Channel;
}


class FExecution : public IExecution
{
private:
	std::unique_ptr<build::bazel::remote::execution::v2::Execution::Stub> Stub;

public:
	FExecution(const std::shared_ptr<grpc::Channel>& Channel);
	~FExecution();

	bool Execute(const FExecuteRequest& Request, FExecuteResponse& Response) override;
};
