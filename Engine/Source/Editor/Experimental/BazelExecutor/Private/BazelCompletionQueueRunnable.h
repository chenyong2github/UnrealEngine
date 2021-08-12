// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Misc/SingleThreadRunnable.h"
#include "Misc/ScopeLock.h"

namespace grpc
{
	class ClientContext;
	class Status;
	template <class R>
	class ClientAsyncReader;
	template <class R>
	class ClientAsyncResponseReader;
	class CompletionQueue;
}

namespace google
{
	namespace longrunning
	{
		class Operation;
	}

	namespace protobuf
	{
		class Message;
	}
}


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
	TUniquePtr<grpc::CompletionQueue> CompletionQueue;
	TUniquePtr<FCriticalSection> QueuedItemsCriticalSection;

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
