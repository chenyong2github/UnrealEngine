// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Misc/SingleThreadRunnable.h"

THIRD_PARTY_INCLUDES_START
UE_PUSH_MACRO("TEXT")
#undef TEXT
#include <grpcpp/grpcpp.h>
#include "build\bazel\remote\execution\v2\remote_execution.grpc.pb.h"
UE_POP_MACRO("TEXT");
THIRD_PARTY_INCLUDES_END


using TStartCallFunction = TUniqueFunction<void(void* Tag, bool Ok)>;
using TReadFunction = TUniqueFunction<void(void* Tag, bool Ok, const google::longrunning::Operation& Operation)>;
using TFinishFunction = TUniqueFunction<void(void* Tag, bool Ok, const grpc::Status& Status, const google::protobuf::Message& Message)>;

class FBazelCompletionQueueRunnable : public FRunnable, FSingleThreadRunnable
{
private:
	struct FQueuedItem
	{
		enum class EState
		{
			Starting,
			Reading,
			Finishing,
		};
		EState State;
		TUniquePtr<grpc::ClientContext> ClientContext;
		TUniquePtr<grpc::ClientAsyncReader<google::longrunning::Operation>> Reader;
		TUniquePtr<google::protobuf::Message> Message;
		TUniquePtr<grpc::Status> Status;
		TStartCallFunction StartCall;
		TReadFunction Read;
		TFinishFunction Finish;
	};

	bool Running;
	TMap<void*, FQueuedItem> QueuedItems;
	grpc::CompletionQueue CompletionQueue;

	void ProcessNext(void* Tag, bool Ok);

public:

	FBazelCompletionQueueRunnable();
	virtual ~FBazelCompletionQueueRunnable();

	// FRunnable methods.
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;

	FSingleThreadRunnable* GetSingleThreadInterface() override;

	// FSingleThreadRunnable methods.
	virtual void Tick() override;

	inline bool IsRunning() const { return Running; }

public:
	bool AddAsyncOperation(
		TUniquePtr<grpc::ClientContext> ClientContext,
		TUniquePtr<grpc::ClientAsyncReader<google::longrunning::Operation>> Reader,
		TStartCallFunction OnStartCall = nullptr,
		TReadFunction OnRead = nullptr,
		TFinishFunction OnFinish = nullptr);

	template<class MessageType>
	bool AddAsyncResponse(
		TUniquePtr<grpc::ClientContext> ClientContext,
		TUniquePtr<grpc::ClientAsyncResponseReader<MessageType>> Reader,
		TFinishFunction OnFinish = nullptr);

	grpc::CompletionQueue* GetCompletionQueue();
};

template<class MessageType>
bool FBazelCompletionQueueRunnable::AddAsyncResponse(
	TUniquePtr<grpc::ClientContext> ClientContext,
	TUniquePtr<grpc::ClientAsyncResponseReader<MessageType>> Reader,
	TFinishFunction OnFinish)
{
	static_assert(std::is_base_of<google::protobuf::Message, MessageType>::value, "MessageType is not derived from google::protobuf::Message");
	if (!Running || !ClientContext.IsValid() || !Reader.IsValid())
	{
		return false;
	}

	grpc::ClientAsyncResponseReader<MessageType>* AsyncReader = Reader.Get();

	// Keep Reader in scope for the lifetime of this async call
	TFinishFunction OnFinishResponse = [Reader = MoveTemp(Reader), OnFinish = MoveTemp(OnFinish)](void* Tag, bool Ok, const grpc::Status& Status, const google::protobuf::Message& Message)
	{
		if (OnFinish)
		{
			OnFinish(Tag, Ok, Status, Message);
		}
	};

	MessageType* Response = new MessageType();
	grpc::Status* Status = new grpc::Status();

	FQueuedItem Item;
	Item.State = FQueuedItem::EState::Finishing;
	Item.Status.Reset(Status);
	Item.Message.Reset(Response);
	Item.ClientContext = MoveTemp(ClientContext);
	Item.Finish = MoveTemp(OnFinishResponse);
	QueuedItems.Add(AsyncReader, MoveTemp(Item));
	AsyncReader->StartCall();
	AsyncReader->Finish(Response, Status, AsyncReader);
	return true;
}
