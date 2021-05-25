// Copyright Epic Games, Inc. All Rights Reserved.

#include "Execution.h"
#include "BazelExecutorModule.h"
#include "ProtoConverter.h"
#include "Messages.h"

THIRD_PARTY_INCLUDES_START
UE_PUSH_MACRO("TEXT")
#undef TEXT
#include <grpcpp/grpcpp.h>
UE_POP_MACRO("TEXT");
THIRD_PARTY_INCLUDES_END


FExecution::FExecution(const std::shared_ptr<grpc::Channel>& Channel, const TMap<FString, FString>& Headers) :
	Stub(build::bazel::remote::execution::v2::Execution::NewStub(Channel)), Headers(Headers)
{
}

FExecution::~FExecution()
{
}

bool FExecution::Execute(const FExecuteRequest& Request, FExecuteResponse& Response)
{
	grpc::ClientContext Context;
	for (const TPair<FString, FString>& Header : Headers)
	{
		Context.AddMetadata(TCHAR_TO_UTF8(*Header.Key.ToLower()), TCHAR_TO_UTF8(*Header.Value));
	}

	build::bazel::remote::execution::v2::ExecuteRequest ProtoRequest;
	ProtoConverter::ToProto(Request, ProtoRequest);

	std::unique_ptr<grpc::ClientReader<google::longrunning::Operation>> Call = Stub->Execute(&Context, ProtoRequest);
	Call->WaitForInitialMetadata();
	while (true)
	{
		google::longrunning::Operation Operation;
		Call->Read(&Operation);

		build::bazel::remote::execution::v2::ExecuteOperationMetadata Metadata;
		if (!Operation.metadata().UnpackTo(&Metadata))
		{
			UE_LOG(LogBazelExecutor, Error, TEXT("Unable to decode metadata"));
			break;
		}

		UE_LOG(LogBazelExecutor, Display, TEXT("Execution state: %s"), UTF8_TO_TCHAR(build::bazel::remote::execution::v2::ExecutionStage_Value_Name(Metadata.stage()).c_str()));

		if (Operation.done())
		{
			build::bazel::remote::execution::v2::ExecuteResponse ProtoResponse;
			if (!Operation.response().UnpackTo(&ProtoResponse))
			{
				UE_LOG(LogBazelExecutor, Error, TEXT("Unable to decode response"));
				break;
			}
			if ((grpc::StatusCode)ProtoResponse.status().code() != grpc::StatusCode::OK) {
				UE_LOG(LogBazelExecutor, Error, TEXT("%s"), UTF8_TO_TCHAR(ProtoResponse.status().message().c_str()));
				break;
			}
			ProtoConverter::FromProto(ProtoResponse, Response);
			return true;
		}
	}

	return false;
}
