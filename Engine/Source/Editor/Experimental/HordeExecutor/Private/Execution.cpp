// Copyright Epic Games, Inc. All Rights Reserved.

#include "Execution.h"
#include "HordeExecutorModule.h"
#include "ProtoConverter.h"

THIRD_PARTY_INCLUDES_START
UE_PUSH_MACRO("TEXT")
#undef TEXT
#include <grpcpp/grpcpp.h>
UE_POP_MACRO("TEXT");
THIRD_PARTY_INCLUDES_END


FExecution::FExecution(const std::shared_ptr<grpc::Channel>& Channel) :
	Stub(build::bazel::remote::execution::v2::Execution::NewStub(Channel))
{
}

FExecution::~FExecution()
{
}

bool FExecution::Execute(const FExecuteRequest& Request, FExecuteResponse& Response)
{
	grpc::ClientContext Context;

	build::bazel::remote::execution::v2::ExecuteRequest ProtoRequest;
	ProtoConverter::ToProto(Request, ProtoRequest);

	std::unique_ptr<grpc::ClientReader<google::longrunning::Operation>> Call = Stub->Execute(&Context, ProtoRequest);
	Call->WaitForInitialMetadata();
	while (true)
	{
		google::longrunning::Operation Operation;
		Call->Read(&Operation);

		build::bazel::remote::execution::v2::ExecuteOperationMetadata Metadata;
		Operation.metadata().UnpackTo(&Metadata);

		UE_LOG(LogHordeExecutor, Display, TEXT("Execution state: %s"), UTF8_TO_TCHAR(build::bazel::remote::execution::v2::ExecutionStage_Value_Name(Metadata.stage()).c_str()));

		if (Operation.done())
		{
			build::bazel::remote::execution::v2::ExecuteResponse ProtoResponse;
			if (!Operation.response().UnpackTo(&ProtoResponse))
			{
				UE_LOG(LogHordeExecutor, Error, TEXT("Unable to decode response"));
				break;
			}
			if ((grpc::StatusCode)ProtoResponse.status().code() != grpc::StatusCode::OK) {
				UE_LOG(LogHordeExecutor, Error, TEXT("%s"), UTF8_TO_TCHAR(ProtoResponse.status().message().c_str()));
				break;
			}
			ProtoConverter::FromProto(ProtoResponse, Response);
			return true;
		}
	}

	return false;
}
