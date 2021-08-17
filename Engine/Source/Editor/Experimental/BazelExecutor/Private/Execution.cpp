// Copyright Epic Games, Inc. All Rights Reserved.

#include "Execution.h"
#include "BazelExecutorModule.h"
#include "ProtoConverter.h"
#include "BazelCompletionQueueRunnable.h"
#include "Messages.h"

#include <memory>

THIRD_PARTY_INCLUDES_START
UE_PUSH_MACRO("TEXT")
#undef TEXT
#if PLATFORM_WINDOWS
#include "Windows/PreWindowsApi.h"
#endif
#include <grpcpp/grpcpp.h>
#include "build/bazel/remote/execution/v2/remote_execution.grpc.pb.h"
#if PLATFORM_WINDOWS
#include "Windows/PostWindowsApi.h"
#endif
UE_POP_MACRO("TEXT");
THIRD_PARTY_INCLUDES_END

#include "BazelCompletionQueueRunnable.inl"


// Stub class so grpc isn't included in the header and everything can be forward declared
class FExecutionStub
{
public:
	TUniquePtr<build::bazel::remote::execution::v2::Execution::Stub> Stub;
	FExecutionStub(build::bazel::remote::execution::v2::Execution::Stub* Stub)
		: Stub(Stub)
	{
	}
};


FExecution::FExecution(const std::shared_ptr<grpc::Channel>& Channel, TSharedPtr<FBazelCompletionQueueRunnable> CompletionQueueRunnable, const TMap<FString, FString>& Headers) :
	ExecutionStub(new FExecutionStub(build::bazel::remote::execution::v2::Execution::NewStub(Channel).release())),
	CompletionQueueRunnable(CompletionQueueRunnable),
	Headers(Headers)
{
}

FExecution::~FExecution()
{
}

bool FExecution::Execute(const FExecuteRequest& Request, FExecuteResponse& Response, int64 TimeoutMs)
{
	grpc::ClientContext ClientContext;
	PrepareContext(ClientContext, TimeoutMs);

	build::bazel::remote::execution::v2::ExecuteRequest ProtoRequest;
	ProtoConverter::ToProto(Request, ProtoRequest);

	std::unique_ptr<grpc::ClientReader<google::longrunning::Operation>> Call = ExecutionStub->Stub->Execute(&ClientContext, ProtoRequest);
	Call->WaitForInitialMetadata();
	while (true)
	{
		google::longrunning::Operation Operation;
		if (!Call->Read(&Operation))
		{
			UE_LOG(LogBazelExecutor, Verbose, TEXT("Execute: Unable to read operation"));
			break;
		}

		build::bazel::remote::execution::v2::ExecuteOperationMetadata Metadata;
		if (!Operation.metadata().UnpackTo(&Metadata))
		{
			UE_LOG(LogBazelExecutor, Verbose, TEXT("Execute: %s Unable to decode metadata"), UTF8_TO_TCHAR(Operation.name().c_str()));
			break;
		}

		UE_LOG(LogBazelExecutor, Verbose, TEXT("Execute: %s Execution state: %s"),
			UTF8_TO_TCHAR(Operation.name().c_str()),
			UTF8_TO_TCHAR(build::bazel::remote::execution::v2::ExecutionStage_Value_Name(Metadata.stage()).c_str()));

		if (Operation.done())
		{
			build::bazel::remote::execution::v2::ExecuteResponse ProtoResponse;
			if (!Operation.response().UnpackTo(&ProtoResponse))
			{
				UE_LOG(LogBazelExecutor, Verbose, TEXT("Execute: %s Unable to decode response"), UTF8_TO_TCHAR(Operation.name().c_str()));
				break;
			}
			ProtoConverter::FromProto(ProtoResponse, Response);
			if (!Response.Status.Ok())
			{
				UE_LOG(LogBazelExecutor, Verbose, TEXT("Execute: %s Info: %s"),
					UTF8_TO_TCHAR(Operation.name().c_str()),
					*Response.Status.Message);
				break;
			}
			return true;
		}
	}

	return false;
}

TFuture<FExecuteResponse> FExecution::ExecuteAsync(const FExecuteRequest& Request, TUniqueFunction<void()> CompletionCallback, int64 TimeoutMs)
{
	TUniquePtr<grpc::ClientContext> ClientContext(new grpc::ClientContext());
	PrepareContext(*ClientContext.Get(), TimeoutMs);

	build::bazel::remote::execution::v2::ExecuteRequest ProtoRequest;
	ProtoConverter::ToProto(Request, ProtoRequest);

	TUniquePtr<grpc::ClientAsyncReader<google::longrunning::Operation>> OperationReader(
		ExecutionStub->Stub->PrepareAsyncExecute(ClientContext.Get(), ProtoRequest, CompletionQueueRunnable->GetCompletionQueue()).release());

	TSharedPtr<TPromise<FExecuteResponse>> ReturnPromise = MakeShared<TPromise<FExecuteResponse>>(MoveTemp(CompletionCallback));

	TStartCallFunction StartCall = [](void* Tag, bool Ok)
	{
		if (!Ok)
		{
			UE_LOG(LogBazelExecutor, Verbose, TEXT("ExecuteAsync: Call Started !Ok"));
			return;
		}
	};

	TReadFunction Read = [](void* Tag, bool Ok, const google::longrunning::Operation& Operation)
	{
		if (!Ok)
		{
			UE_LOG(LogBazelExecutor, Verbose, TEXT("ExecuteAsync: Read !Ok"));
			return;
		}

		build::bazel::remote::execution::v2::ExecuteOperationMetadata Metadata;
		if (!Operation.metadata().UnpackTo(&Metadata))
		{
			UE_LOG(LogBazelExecutor, Verbose, TEXT("ExecuteAsync: %s Unable to decode metadata"), UTF8_TO_TCHAR(Operation.name().c_str()));
			return;
		}

		UE_LOG(LogBazelExecutor, Verbose, TEXT("ExecuteAsync: %s Execution state: %s"),
			UTF8_TO_TCHAR(Operation.name().c_str()),
			UTF8_TO_TCHAR(build::bazel::remote::execution::v2::ExecutionStage_Value_Name(Metadata.stage()).c_str()));
	};

	TFinishFunction Finish = [ReturnPromise](void* Tag, bool Ok, const grpc::Status& ProtoStatus, const google::protobuf::Message& Message)
	{
		const google::longrunning::Operation& Operation = (const google::longrunning::Operation&)Message;
		FStatus Status;
		if (Ok)
		{
			ProtoConverter::FromProto(ProtoStatus, Status);
		}
		else
		{
			UE_LOG(LogBazelExecutor, Verbose, TEXT("ExecuteAsync: %s Finish !Ok"), UTF8_TO_TCHAR(Operation.name().c_str()));
			Status.Code = EStatusCode::ABORTED;
		}


		if (!Status.Ok())
		{
			FExecuteResponse Response;
			Response.Status = Status;
			ReturnPromise->EmplaceValue(MoveTemp(Response));
			return;
		}

		build::bazel::remote::execution::v2::ExecuteResponse ProtoResponse;
		if (!Operation.response().UnpackTo(&ProtoResponse))
		{
			UE_LOG(LogBazelExecutor, Verbose, TEXT("ExecuteAsync: %s Unable to decode response"), UTF8_TO_TCHAR(Operation.name().c_str()));
			FExecuteResponse Response;
			Response.Status.Code = EStatusCode::INTERNAL;
			Response.Status.Message = TEXT("Unable to decode response");
			ReturnPromise->EmplaceValue(MoveTemp(Response));
			return;
		}

		FExecuteResponse Response;
		ProtoConverter::FromProto(ProtoResponse, Response);
		if (!Response.Status.Ok())
		{
			UE_LOG(LogBazelExecutor, Verbose, TEXT("ExecuteAsync: %s Info: %s"),
				UTF8_TO_TCHAR(Operation.name().c_str()),
				*Response.Status.Message);
		}

		ReturnPromise->EmplaceValue(MoveTemp(Response));
	};

	if (!CompletionQueueRunnable->AddAsyncOperation(MoveTemp(ClientContext),
		MoveTemp(OperationReader),
		MoveTemp(StartCall),
		MoveTemp(Read),
		MoveTemp(Finish)))
	{
		FExecuteResponse Response;
		Response.Status.Code = EStatusCode::UNAVAILABLE;
		Response.Status.Message = TEXT("FBazelCompletionQueueRunnable not running");
		ReturnPromise->EmplaceValue(MoveTemp(Response));
	}

	return ReturnPromise->GetFuture();
}

void FExecution::PrepareContext(grpc::ClientContext& ClientContext, int64 TimeoutMs) const
{
	for (const TPair<FString, FString>& Header : Headers)
	{
		ClientContext.AddMetadata(TCHAR_TO_UTF8(*Header.Key.ToLower()), TCHAR_TO_UTF8(*Header.Value));
	}
	if (TimeoutMs > 0)
	{
		ClientContext.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(TimeoutMs));
	}
}
