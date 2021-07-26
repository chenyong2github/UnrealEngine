// Copyright Epic Games, Inc. All Rights Reserved.

#include "BazelCompletionQueueRunnable.h"

#include <chrono>

THIRD_PARTY_INCLUDES_START
UE_PUSH_MACRO("TEXT")
#undef TEXT
#include <grpcpp/grpcpp.h>
#include "build/bazel/remote/execution/v2/remote_execution.grpc.pb.h"
UE_POP_MACRO("TEXT");
THIRD_PARTY_INCLUDES_END


FBazelCompletionQueueRunnable::FBazelCompletionQueueRunnable()
	: CompletionQueue(new grpc::CompletionQueue())
{
}

FBazelCompletionQueueRunnable::~FBazelCompletionQueueRunnable()
{
}

void FBazelCompletionQueueRunnable::ProcessNext(void* Tag, bool Ok)
{
	QueuedItemsMutex.lock();
	FQueuedItem* Item = QueuedItems.Find(Tag);
	QueuedItemsMutex.unlock();
	if (!Item)
	{
		return;
	}

	switch (Item->State)
	{
	case FQueuedItem::EState::Starting:
	{
		if (Item->StartCall)
		{
			Item->StartCall(Tag, Ok);
		}

		if (Ok)
		{
			google::longrunning::Operation* Operation = (google::longrunning::Operation*)Item->Message.Get();
			Item->State = FQueuedItem::EState::Reading;
			Item->Reader->Read(Operation, Tag);
		}
		else
		{
			Item->State = FQueuedItem::EState::Finishing;
			Item->Reader->Finish(Item->Status.Get(), Tag);
		}
		break;
	}
	case FQueuedItem::EState::Reading:
	{
		google::longrunning::Operation* Operation = (google::longrunning::Operation*)Item->Message.Get();
		if (Item->Read)
		{
			Item->Read(Tag, Ok, *Operation);
		}

		if (Operation->done() || !Ok)
		{
			Item->State = FQueuedItem::EState::Finishing;
			Item->Reader->Finish(Item->Status.Get(), Tag);
		}
		else
		{
			Item->Reader->Read(Operation, Tag);
		}
		break;
	}
	case FQueuedItem::EState::Finishing:
	{
		if (Item->Finish)
		{
			Item->Finish(Tag, Ok, *Item->Status.Get(), *Item->Message.Get());
		}

		QueuedItemsMutex.lock();
		QueuedItems.Remove(Tag);
		QueuedItemsMutex.unlock();
		break;
	}
	}
}

bool FBazelCompletionQueueRunnable::Init()
{
	Running = true;
	return Running;
}

uint32 FBazelCompletionQueueRunnable::Run()
{
	void* Tag;
	bool Ok = false;
	while (Running && CompletionQueue->Next(&Tag, &Ok))
	{
		if (!Running)
		{
			break;
		}
		ProcessNext(Tag, Ok);
	}
	return 0;
}

void FBazelCompletionQueueRunnable::Stop()
{
	// Request shutdown of the CompletionQueue
	Running = false;
	CompletionQueue->Shutdown();
}

void FBazelCompletionQueueRunnable::Exit()
{
	Stop();

	// Drain the completion queue
	void* Tag;
	bool Ok;
	while (CompletionQueue->Next(&Tag, &Ok)) {}

	QueuedItemsMutex.lock();
	for (const TPair<void*,FQueuedItem>& Pair : QueuedItems)
	{
		const FQueuedItem& Item = Pair.Value;
		if (Item.Finish)
		{
			Item.Finish(Pair.Key, false, *Item.Status.Get(), *Item.Message.Get());
		}
	}

	QueuedItems.Empty();
	QueuedItemsMutex.unlock();
}

FSingleThreadRunnable* FBazelCompletionQueueRunnable::GetSingleThreadInterface()
{
	return this;
}

void FBazelCompletionQueueRunnable::Tick()
{
	if (!Running || QueuedItems.IsEmpty())
	{
		return;
	}

	void* Tag;
	bool Ok = false;
	const std::chrono::time_point Deadline = std::chrono::system_clock::now() + std::chrono::microseconds(100);
	while (Running && std::chrono::system_clock::now() < Deadline)
	{
		grpc::CompletionQueue::NextStatus Status = CompletionQueue->AsyncNext(&Tag, &Ok, Deadline);
		if (Status == grpc::CompletionQueue::NextStatus::GOT_EVENT)
		{
			ProcessNext(Tag, Ok);
			continue;
		}

		break;
	}
}

bool FBazelCompletionQueueRunnable::AddAsyncOperation(
	TUniquePtr<grpc::ClientContext> ClientContext,
	TUniquePtr<grpc::ClientAsyncReader<google::longrunning::Operation>> Reader,
	TStartCallFunction OnStartCall,
	TReadFunction OnRead,
	TFinishFunction OnFinish)
{
	if (!Running || !ClientContext.IsValid() || !Reader.IsValid())
	{
		return false;
	}

	grpc::ClientAsyncReader<google::longrunning::Operation>* AsyncReader = Reader.Get();
	FQueuedItem Item;
	Item.State = FQueuedItem::EState::Starting;
	Item.ClientContext = MoveTemp(ClientContext);
	Item.Reader = MoveTemp(Reader);
	Item.Message.Reset(new google::longrunning::Operation());
	Item.Status.Reset(new grpc::Status());
	Item.StartCall = MoveTemp(OnStartCall);
	Item.Read = MoveTemp(OnRead);
	Item.Finish = MoveTemp(OnFinish);
	QueuedItemsMutex.lock();
	QueuedItems.Add(AsyncReader, MoveTemp(Item));
	QueuedItemsMutex.unlock();
	AsyncReader->StartCall(AsyncReader);
	return true;
}

grpc::CompletionQueue* FBazelCompletionQueueRunnable::GetCompletionQueue()
{
	if (!Running)
	{
		return nullptr;
	}

	return CompletionQueue.Get();
}
