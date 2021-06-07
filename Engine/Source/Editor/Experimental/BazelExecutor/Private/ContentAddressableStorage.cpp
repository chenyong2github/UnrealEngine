// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContentAddressableStorage.h"
#include "BazelExecutorModule.h"
#include "ProtoConverter.h"
#include "BazelCompletionQueueRunnable.h"
#include "Messages.h"

#include <memory>

THIRD_PARTY_INCLUDES_START
UE_PUSH_MACRO("TEXT")
#undef TEXT
#include <grpcpp/grpcpp.h>
UE_POP_MACRO("TEXT");
THIRD_PARTY_INCLUDES_END


FContentAddressableStorage::FContentAddressableStorage(const std::shared_ptr<grpc::Channel>& Channel, TSharedPtr<FBazelCompletionQueueRunnable> CompletionQueueRunnable, const TMap<FString, FString>& Headers) :
	Stub(build::bazel::remote::execution::v2::ContentAddressableStorage::NewStub(Channel).release()),
	CompletionQueueRunnable(CompletionQueueRunnable),
	Headers(Headers)
{
}

FContentAddressableStorage::~FContentAddressableStorage()
{
}

bool FContentAddressableStorage::ToDigest(const TArray<char>& InData, FDigest& OutDigest)
{
	return ProtoConverter::ToDigest(InData, OutDigest);
}

bool FContentAddressableStorage::ToBlob(const FDirectory& InDirectory, TArray<char>& OutData, FDigest& OutDigest)
{
	return ProtoConverter::ToBlob(InDirectory, OutData, OutDigest);
}

bool FContentAddressableStorage::ToBlob(const FCommand& InCommand, TArray<char>& OutData, FDigest& OutDigest)
{
	return ProtoConverter::ToBlob(InCommand, OutData, OutDigest);
}

bool FContentAddressableStorage::ToBlob(const FAction& InAction, TArray<char>& OutData, FDigest& OutDigest)
{
	return ProtoConverter::ToBlob(InAction, OutData, OutDigest);
}

FStatus FContentAddressableStorage::FindMissingBlobs(const FFindMissingBlobsRequest& Request, FFindMissingBlobsResponse& Response, int64 TimeoutMs)
{
	grpc::ClientContext ClientContext;
	PrepareContext(ClientContext, TimeoutMs);

	build::bazel::remote::execution::v2::FindMissingBlobsRequest ProtoRequest;
	ProtoConverter::ToProto(Request, ProtoRequest);

	build::bazel::remote::execution::v2::FindMissingBlobsResponse ProtoResponse;
	grpc::Status ProtoStatus = Stub->FindMissingBlobs(&ClientContext, ProtoRequest, &ProtoResponse);
	FStatus Status;
	ProtoConverter::FromProto(ProtoStatus, Status);
	ProtoConverter::FromProto(ProtoResponse, Response);

	return MoveTemp(Status);
}

FStatus FContentAddressableStorage::BatchUpdateBlobs(const FBatchUpdateBlobsRequest& Request, FBatchUpdateBlobsResponse& Response, int64 TimeoutMs)
{
	grpc::ClientContext ClientContext;
	PrepareContext(ClientContext, TimeoutMs);

	build::bazel::remote::execution::v2::BatchUpdateBlobsRequest ProtoRequest;
	ProtoConverter::ToProto(Request, ProtoRequest);

	build::bazel::remote::execution::v2::BatchUpdateBlobsResponse ProtoResponse;
	grpc::Status ProtoStatus = Stub->BatchUpdateBlobs(&ClientContext, ProtoRequest, &ProtoResponse);
	FStatus Status;
	ProtoConverter::FromProto(ProtoStatus, Status);
	ProtoConverter::FromProto(ProtoResponse, Response);

	return MoveTemp(Status);
}

FStatus FContentAddressableStorage::BatchReadBlobs(const FBatchReadBlobsRequest& Request, FBatchReadBlobsResponse& Response, int64 TimeoutMs)
{
	grpc::ClientContext ClientContext;
	PrepareContext(ClientContext, TimeoutMs);

	build::bazel::remote::execution::v2::BatchReadBlobsRequest ProtoRequest;
	ProtoConverter::ToProto(Request, ProtoRequest);

	build::bazel::remote::execution::v2::BatchReadBlobsResponse ProtoResponse;
	grpc::Status ProtoStatus = Stub->BatchReadBlobs(&ClientContext, ProtoRequest, &ProtoResponse);
	FStatus Status;
	ProtoConverter::FromProto(ProtoStatus, Status);
	ProtoConverter::FromProto(ProtoResponse, Response);

	return MoveTemp(Status);
}

TFuture<TPair<FStatus, FFindMissingBlobsResponse>> FContentAddressableStorage::FindMissingBlobsAsync(const FFindMissingBlobsRequest& Request, TUniqueFunction<void()> CompletionCallback, int64 TimeoutMs)
{
	TUniquePtr<grpc::ClientContext> ClientContext(new grpc::ClientContext());
	PrepareContext(*ClientContext.Get(), TimeoutMs);

	build::bazel::remote::execution::v2::FindMissingBlobsRequest ProtoRequest;
	ProtoConverter::ToProto(Request, ProtoRequest);

	TUniquePtr<grpc::ClientAsyncResponseReader<build::bazel::remote::execution::v2::FindMissingBlobsResponse>> ProtoReader(
		Stub->PrepareAsyncFindMissingBlobs(ClientContext.Get(), ProtoRequest, CompletionQueueRunnable->GetCompletionQueue()).release());

	TSharedPtr<TPromise<TPair<FStatus, FFindMissingBlobsResponse>>> ReturnPromise = MakeShared<TPromise<TPair<FStatus, FFindMissingBlobsResponse>>>(MoveTemp(CompletionCallback));

	TFinishFunction Finish = [ReturnPromise](void* Tag, bool Ok, const grpc::Status& ProtoStatus, const google::protobuf::Message& ProtoMessage)
	{
		const build::bazel::remote::execution::v2::FindMissingBlobsResponse& ProtoResponse = (const build::bazel::remote::execution::v2::FindMissingBlobsResponse&)ProtoMessage;
		FStatus Status;
		FFindMissingBlobsResponse Response;
		if (Ok)
		{
			ProtoConverter::FromProto(ProtoStatus, Status);
			ProtoConverter::FromProto(ProtoResponse, Response);
		}
		else
		{
			UE_LOG(LogBazelExecutor, Error, TEXT("FindMissingBlobsAsync: Finish !Ok"));
			Status.Code = EStatusCode::ABORTED;
		}
		ReturnPromise->EmplaceValue(TPair<FStatus, FFindMissingBlobsResponse>(MoveTemp(Status), MoveTemp(Response)));
	};

	if (!CompletionQueueRunnable->AddAsyncResponse<build::bazel::remote::execution::v2::FindMissingBlobsResponse>(
		MoveTemp(ClientContext), MoveTemp(ProtoReader), MoveTemp(Finish)))
	{
		FStatus Status;
		FFindMissingBlobsResponse Response;
		Status.Code = EStatusCode::UNAVAILABLE;
		Status.Message = TEXT("FBazelCompletionQueueRunnable not running");
		ReturnPromise->EmplaceValue(TPair<FStatus, FFindMissingBlobsResponse>(MoveTemp(Status), MoveTemp(Response)));
	}

	return ReturnPromise->GetFuture();
}


TFuture<TPair<FStatus, FBatchUpdateBlobsResponse>> FContentAddressableStorage::BatchUpdateBlobsAsync(const FBatchUpdateBlobsRequest& Request, TUniqueFunction<void()> CompletionCallback, int64 TimeoutMs)
{
	TUniquePtr<grpc::ClientContext> ClientContext(new grpc::ClientContext());
	PrepareContext(*ClientContext.Get(), TimeoutMs);

	build::bazel::remote::execution::v2::BatchUpdateBlobsRequest ProtoRequest;
	ProtoConverter::ToProto(Request, ProtoRequest);

	TUniquePtr<grpc::ClientAsyncResponseReader<build::bazel::remote::execution::v2::BatchUpdateBlobsResponse>> ProtoReader(
		Stub->PrepareAsyncBatchUpdateBlobs(ClientContext.Get(), ProtoRequest, CompletionQueueRunnable->GetCompletionQueue()).release());

	TSharedPtr<TPromise<TPair<FStatus, FBatchUpdateBlobsResponse>>> ReturnPromise = MakeShared<TPromise<TPair<FStatus, FBatchUpdateBlobsResponse>>>(MoveTemp(CompletionCallback));

	TFinishFunction Finish = [ReturnPromise](void* Tag, bool Ok, const grpc::Status& ProtoStatus, const google::protobuf::Message& ProtoMessage)
	{
		const build::bazel::remote::execution::v2::BatchUpdateBlobsResponse& ProtoResponse = (const build::bazel::remote::execution::v2::BatchUpdateBlobsResponse&)ProtoMessage;
		FStatus Status;
		FBatchUpdateBlobsResponse Response;
		if (Ok)
		{
			ProtoConverter::FromProto(ProtoStatus, Status);
			ProtoConverter::FromProto(ProtoResponse, Response);
		}
		else
		{
			UE_LOG(LogBazelExecutor, Error, TEXT("BatchUpdateBlobsAsync: Finish !Ok"));
			Status.Code = EStatusCode::ABORTED;
		}
		ReturnPromise->EmplaceValue(TPair<FStatus, FBatchUpdateBlobsResponse>(MoveTemp(Status), MoveTemp(Response)));
	};

	if (!CompletionQueueRunnable->AddAsyncResponse<build::bazel::remote::execution::v2::BatchUpdateBlobsResponse>(
		MoveTemp(ClientContext), MoveTemp(ProtoReader), MoveTemp(Finish)))
	{
		FStatus Status;
		FBatchUpdateBlobsResponse Response;
		Status.Code = EStatusCode::UNAVAILABLE;
		Status.Message = TEXT("FBazelCompletionQueueRunnable not running");
		ReturnPromise->EmplaceValue(TPair<FStatus, FBatchUpdateBlobsResponse>(MoveTemp(Status), MoveTemp(Response)));
	}

	return ReturnPromise->GetFuture();
}

TFuture<TPair<FStatus, FBatchReadBlobsResponse>> FContentAddressableStorage::BatchReadBlobsAsync(const FBatchReadBlobsRequest& Request, TUniqueFunction<void()> CompletionCallback, int64 TimeoutMs)
{
	TUniquePtr<grpc::ClientContext> ClientContext(new grpc::ClientContext());
	PrepareContext(*ClientContext.Get(), TimeoutMs);

	build::bazel::remote::execution::v2::BatchReadBlobsRequest ProtoRequest;
	ProtoConverter::ToProto(Request, ProtoRequest);

	TUniquePtr<grpc::ClientAsyncResponseReader<build::bazel::remote::execution::v2::BatchReadBlobsResponse>> ProtoReader(
		Stub->PrepareAsyncBatchReadBlobs(ClientContext.Get(), ProtoRequest, CompletionQueueRunnable->GetCompletionQueue()).release());

	TSharedPtr<TPromise<TPair<FStatus, FBatchReadBlobsResponse>>> ReturnPromise = MakeShared<TPromise<TPair<FStatus, FBatchReadBlobsResponse>>>(MoveTemp(CompletionCallback));

	TFinishFunction Finish = [ReturnPromise](void* Tag, bool Ok, const grpc::Status& ProtoStatus, const google::protobuf::Message& ProtoMessage)
	{
		const build::bazel::remote::execution::v2::BatchReadBlobsResponse& ProtoResponse = (const build::bazel::remote::execution::v2::BatchReadBlobsResponse&)ProtoMessage;
		FStatus Status;
		FBatchReadBlobsResponse Response;
		if (Ok)
		{
			ProtoConverter::FromProto(ProtoStatus, Status);
			ProtoConverter::FromProto(ProtoResponse, Response);
		}
		else
		{
			UE_LOG(LogBazelExecutor, Error, TEXT("BatchReadBlobsAsync: Finish !Ok"));
			Status.Code = EStatusCode::ABORTED;
		}
		ReturnPromise->EmplaceValue(TPair<FStatus, FBatchReadBlobsResponse>(MoveTemp(Status), MoveTemp(Response)));
	};

	if (!CompletionQueueRunnable->AddAsyncResponse<build::bazel::remote::execution::v2::BatchReadBlobsResponse>(
		MoveTemp(ClientContext), MoveTemp(ProtoReader), MoveTemp(Finish)))
	{
		FStatus Status;
		FBatchReadBlobsResponse Response;
		Status.Code = EStatusCode::UNAVAILABLE;
		Status.Message = TEXT("FBazelCompletionQueueRunnable not running");
		ReturnPromise->EmplaceValue(TPair<FStatus, FBatchReadBlobsResponse>(MoveTemp(Status), MoveTemp(Response)));
	}

	return ReturnPromise->GetFuture();
}

void FContentAddressableStorage::PrepareContext(grpc::ClientContext& ClientContext, int64 TimeoutMs) const
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
