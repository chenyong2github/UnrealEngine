// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


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
	QueuedItemsMutex.lock();
	QueuedItems.Add(AsyncReader, MoveTemp(Item));
	QueuedItemsMutex.unlock();
	AsyncReader->StartCall();
	AsyncReader->Finish(Response, Status, AsyncReader);
	return true;
}