// Copyright Epic Games, Inc. All Rights Reserved.

#include "DirectLink/Network/DirectLinkEndpoint.h"

#include "DirectLink/DirectLinkLog.h"
#include "DirectLink/Network/DirectLinkMessages.h"
#include "DirectLink/SceneGraphNode.h"
#include "DirectLink/SceneIndexBuilder.h"

#include "Async/Async.h"
#include "MessageEndpointBuilder.h"

namespace DirectLink
{

FEndpoint::FEndpoint(const FString& InName)
	: SharedState(InName)
	, Internal(*this)
{
	Internal.Init();

	UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Log, TEXT("Endpoint '%s' Start internal thread"), *SharedState.NiceName);

	InnerThreadEvent = FPlatformProcess::GetSynchEventFromPool();
	InnerThreadResult = Async(EAsyncExecution::Thread,
		[&, this]
		{
			InnerThreadId = FPlatformTLS::GetCurrentThreadId();
			Internal.Run();
			return true;
		}
	);
}


FEndpoint::~FEndpoint()
{
	SharedState.bInnerThreadShouldRun = false;
	InnerThreadEvent->Trigger();
	bool _ = InnerThreadResult.Get();
	FPlatformProcess::ReturnSynchEventToPool(InnerThreadEvent);

	UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Log, TEXT("Endpoint '%s' closed"), *SharedState.NiceName);
}

FSourceHandle FEndpoint::AddSource(const FString& Name, EVisibility Visibility)
{
	FGuid Id;
	{
		UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Log, TEXT("Endpoint '%s': Source added '%s'"), *SharedState.NiceName, *Name);
		FRWScopeLock _(SharedState.SourcesLock, SLT_Write);
		TSharedPtr<FStreamSource>& NewSource = SharedState.Sources.Add_GetRef(MakeShared<FStreamSource>(Name, Visibility));
		Id = NewSource->GetId();
	}

	SharedState.bDirtySources = true;

	return Id;
}


void FEndpoint::RemoveSource(const FSourceHandle& SourceId)
{
	{
		// first remove linked streams
		FRWScopeLock _(SharedState.StreamsLock, SLT_Write);
		for (FStreamDescription& Stream : SharedState.Streams)
		{
			if (Stream.SourcePoint == SourceId)
			{
				CloseStreamInternal(Stream, _);
			}
		}
	}

	int32 RemovedCount = 0;
	{
		FRWScopeLock _(SharedState.SourcesLock, SLT_Write);
		RemovedCount = SharedState.Sources.RemoveAll([&](const auto& Source){return Source->GetId() == SourceId;});
	}

	if (RemovedCount)
	{
		SharedState.bDirtySources = true;
	}
}


void FEndpoint::SetSourceRoot(const FSourceHandle& SourceId, ISceneGraphNode* InRoot, bool bSnapshot)
{
	{
		FRWScopeLock _(SharedState.SourcesLock, SLT_Write);
		for (TSharedPtr<FStreamSource>& Source : SharedState.Sources) // #ue_directlink_cleanup: readonly on array, write on specific source lock
		{
			if (Source->GetId() == SourceId)
			{
				Source->SetRoot(InRoot);

				break;
			}
		}
	}

	if (bSnapshot)
	{
		SnapshotSource(SourceId);
	}
}


void FEndpoint::SnapshotSource(const FSourceHandle& SourceId)
{
	TSharedPtr<FLocalSceneIndex> SceneIndex;
	{
		FRWScopeLock _(SharedState.SourcesLock, SLT_Write);
		for (TSharedPtr<FStreamSource>& Source : SharedState.Sources)
		{
			if (Source->GetId() == SourceId)
			{
				SceneIndex = Source->Snapshot();
				// #ue_directlink_cleanup thread safety: this call doesnt actually snapshot elements
				// it only builds an index.
				// the scene could be modified in the same time....

				break;
			}
		}
	}

	// #ue_directlink_quality Snapshot
	// last snapshot should be kept by the source, so that following connection could grab it.
	// 1. user thread do the snap, store a ptr on the source, mark as updated if necessary.
	// 2. inner thread do the propagation to the StreamSenders

	{
		// #ue_directlink_streams cleanup thread safety ? make this async: on inner thread
		FRWScopeLock _(SharedState.StreamsLock, SLT_Write);
		for (FStreamDescription& Stream : SharedState.Streams)
		{
			if (Stream.SourcePoint == SourceId)
			{
				if (Stream.Sender.IsValid()) // IsConnected()
				{
					Stream.Sender->SetSceneIndex(SceneIndex);
				}
			}
		}
	}
}


FDestinationHandle FEndpoint::AddDestination(const FString& Name, EVisibility Visibility, const TSharedPtr<ISceneProvider>& Provider)
{
	FDestinationHandle Id;
	{
		FRWScopeLock _(SharedState.DestinationsLock, SLT_Write);
		TSharedPtr<FStreamDestination>& NewDest = SharedState.Destinations.Add_GetRef(MakeShared<FStreamDestination>(Name, Visibility));
		NewDest->Provider = Provider;
		Id = NewDest->GetId();
	}

	SharedState.bDirtyDestinations = true;
	return Id;
}


void FEndpoint::RemoveDestination(const FDestinationHandle& Destination)
{
	{
		// first close associated streams
		FRWScopeLock _(SharedState.StreamsLock, SLT_Write);
		for (FStreamDescription& Stream : SharedState.Streams)
		{
			if (Stream.DestinationPoint == Destination)
			{
				CloseStreamInternal(Stream, _);
			}
		}
	}

	int32 RemovedCount = 0;
	{
		FRWScopeLock _(SharedState.DestinationsLock, SLT_Write);
		RemovedCount = SharedState.Destinations.RemoveAll([&](const auto& Dest){return Dest->GetId() == Destination;});
	}

	if (RemovedCount)
	{
		SharedState.bDirtyDestinations = true;
	}
}


FRawInfo FEndpoint::GetRawInfoCopy() const
{
	FRWScopeLock _(SharedState.RawInfoCopyLock, SLT_ReadOnly);
	return SharedState.RawInfo;
}


void FEndpoint::AddEndpointObserver(IEndpointObserver* Observer)
{
	if (Observer)
	{
		FRWScopeLock _(SharedState.ObserversLock, SLT_Write);
		SharedState.Observers.AddUnique(Observer);
	}
}


void FEndpoint::RemoveEndpointObserver(IEndpointObserver* Observer)
{
	FRWScopeLock _(SharedState.ObserversLock, SLT_Write);
	SharedState.Observers.RemoveSwap(Observer);
}

// #ue_directlink_streams could return an error code...
void FEndpoint::OpenStream(const FSourceHandle& SourceId, const FDestinationHandle& DestinationId)
{
	// check if the stream is already opened
	{
		FRWScopeLock _(SharedState.StreamsLock, SLT_ReadOnly);
		for (const FStreamDescription& Stream : SharedState.Streams)
		{
			if (Stream.SourcePoint == SourceId && Stream.DestinationPoint == DestinationId)
			{
				// useless case, temp because of the unfinished connection policy.
				// #ue_directlink_cleanup Replace with proper policy (user driven connection map) + log if this happen
				return;
			}
		}
	}

	bool bRequestFromSource = false;
	bool bRequestFromDestination = false;
	{
		FRWScopeLock _(SharedState.SourcesLock, SLT_ReadOnly);
		for (TSharedPtr<FStreamSource>& Source : SharedState.Sources)
		{
			if (Source->GetId() == SourceId)
			{
				bRequestFromSource = true;
				break;
			}
		}
	}
	if (!bRequestFromSource)
	{
		// make sure we have the destination
		FRWScopeLock _(SharedState.DestinationsLock, SLT_ReadOnly);
		for (TSharedPtr<FStreamDestination>& Destination : SharedState.Destinations)
		{
			if (Destination->GetId() == SourceId)
			{
				bRequestFromDestination = true;
				break;
			}
		}
	}
	if (!bRequestFromSource && ! bRequestFromDestination)
	{
		// #ue_directlink_cleanup log
		// we don't have any side of the connection...
		return;
	}

	// Find Remote address.
	FMessageAddress RemoteAddress;
	{
		FGuid RemoteDataPointId = bRequestFromSource ? DestinationId : SourceId;

		FRWScopeLock _(SharedState.RawInfoCopyLock, SLT_ReadOnly);
		if (FRawInfo::FDataPointInfo* DataPointInfo = SharedState.RawInfo.DataPointsInfo.Find(RemoteDataPointId))
		{
			RemoteAddress = DataPointInfo->EndpointAddress;
		}
	}

	if (RemoteAddress.IsValid())
	{
		FRWScopeLock _(SharedState.StreamsLock, SLT_Write);
		FStreamPort StreamPort = ++SharedState.StreamPortIdGenerator;

		FDirectLinkMsg_OpenStreamRequest* Request = NewMessage<FDirectLinkMsg_OpenStreamRequest>();
		Request->bRequestFromSource = bRequestFromSource;
		Request->RequestFromStreamPort = StreamPort;
		Request->SourceGuid = SourceId;
		Request->DestinationGuid = DestinationId;

		UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Log, TEXT("Endpoint '%s': Send FDirectLinkMsg_OpenStreamRequest"), *SharedState.NiceName);
		SharedState.MessageEndpoint->Send(Request, RemoteAddress);

		FStreamDescription& NewStream = SharedState.Streams.AddDefaulted_GetRef();
		NewStream.bThisIsSource = bRequestFromSource;
		NewStream.SourcePoint = SourceId;
		NewStream.DestinationPoint = DestinationId;
		NewStream.LocalStreamPort = StreamPort;
		NewStream.RemoteAddress = RemoteAddress;
		NewStream.Status = FStreamDescription::EConnectionState::RequestSent;
		NewStream.LastRemoteLifeSign = FPlatformTime::Seconds();
	}
	else
	{
		UE_LOG(LogDirectLink, Error, TEXT("Connection Request failed: no recipent found"));
	}
}


void FEndpoint::CloseStream(const FSourceHandle& SourceId, const FDestinationHandle& DestinationId)
{
	FRWScopeLock _(SharedState.StreamsLock, SLT_Write);
	for (FStreamDescription& Stream : SharedState.Streams)
	{
		if (Stream.SourcePoint == SourceId && Stream.DestinationPoint == DestinationId)
		{
			CloseStreamInternal(Stream, _);
		}
	}
}


void FEndpoint::CloseStreamInternal(FStreamDescription& Stream, const FRWScopeLock& _, bool bNotifyRemote)
{
	if (bNotifyRemote && Stream.RemoteAddress.IsValid())
	{
		UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Log, TEXT("Endpoint '%s': Stream removed"), *SharedState.NiceName, *Stream.SourcePoint.ToString());
		FDirectLinkMsg_CloseStreamRequest* Request = NewMessage<FDirectLinkMsg_CloseStreamRequest>();
		Request->RecipientStreamPort = Stream.RemoteStreamPort;

		UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Log, TEXT("Endpoint '%s': Send FDirectLinkMsg_CloseStreamRequest"), *SharedState.NiceName);
		SharedState.MessageEndpoint->Send(Request, Stream.RemoteAddress);
	}

	Stream.Sender.Reset();
	Stream.Receiver.Reset();

	Stream.Status = FStreamDescription::EConnectionState::Closed;
}


FString FEndpoint::FInternalThreadState::ToString_dbg() const
{
	FString Out;
	{
		Out.Appendf(TEXT("Endpoint '%s' (%s):\n"), *SharedState.NiceName, *MessageEndpoint->GetAddress().ToString());
	}

	auto PrintEndpoint = [&](const FDirectLinkMsg_EndpointState& Remote, int32 RemoteEndpointIndex)
	{
		Out.Appendf(TEXT("-- endpoint [%d] %s/%d:'%s' \n"),
			RemoteEndpointIndex,
			*Remote.ComputerName,
			Remote.ProcessId,
			*Remote.NiceName
		);

		Out.Appendf(TEXT("-- %d Sources:\n"), Remote.Sources.Num());
		int32 SrcIndex = 0;
		for (const FNamedId& Src : Remote.Sources)
		{
			Out.Appendf(TEXT("--- src #%d '%s' (%d)\n"), SrcIndex, *Src.Name, Src.Id.A);
			SrcIndex++;
		}

		Out.Appendf(TEXT("-- %d Destinations:\n"), Remote.Destinations.Num());
		int32 DestinationIndex = 0;
		for (const FNamedId& Dest : Remote.Destinations)
		{
			Out.Appendf(TEXT("--- dst #%d '%s' (%d)\n"), DestinationIndex, *Dest.Name, Dest.Id.A);
			DestinationIndex++;
		}
	};

	Out.Appendf(TEXT("- This:\n"));
	PrintEndpoint(ThisDescription, 0);

	Out.Appendf(TEXT("- Remotes:\n"));
	int32 RemoteEndpointIndex = 0;
	for (const auto& KeyValue : RemoteEndpointDescriptions)
	{
		const FDirectLinkMsg_EndpointState& Remote = KeyValue.Value;
		PrintEndpoint(Remote, RemoteEndpointIndex);
		RemoteEndpointIndex++;
	}

	{
		FRWScopeLock _(SharedState.StreamsLock, SLT_ReadOnly);
		Out.Appendf(TEXT("- %d Streams:\n"), SharedState.Streams.Num());
		for (const FStreamDescription& Stream : SharedState.Streams)
		{
			char SourceFlag = Stream.bThisIsSource ? 'S' : 'D';
			const TCHAR* StatusText = TEXT("?"); //Stream.stabThisIsSource ? 'S' : 'D';
			switch (Stream.Status)
			{
				case FStreamDescription::EConnectionState::Uninitialized: StatusText =  TEXT("Uninitialized"); break;
				case FStreamDescription::EConnectionState::RequestSent:   StatusText =  TEXT("RequestSent  "); break;
				case FStreamDescription::EConnectionState::Active:        StatusText =  TEXT("Active       "); break;
				case FStreamDescription::EConnectionState::Closed:        StatusText =  TEXT("Closed       "); break;
			}
			Out.Appendf(TEXT("-- [%s] port:%d, type:%c, from:%d, to: %d on port: %d\n"), StatusText, Stream.LocalStreamPort, SourceFlag, Stream.SourcePoint.A, Stream.DestinationPoint.A, Stream.RemoteStreamPort);
		}
	}

	return Out;
}


void FEndpoint::FInternalThreadState::Handle_EndpointLifecycle(const FDirectLinkMsg_EndpointLifecycle& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	check(IsInnerThread());
	UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Verbose, TEXT("Endpoint '%s': Handle_EndpointLifecycle"), *SharedState.NiceName);

	// #ue_directlink_quality ignore incompatible peers (based on serial revision)

	const FMessageAddress& RemoteEndpointAddress = Context->GetSender();
	if (IsMine(RemoteEndpointAddress))
	{
		return;
	}

	switch (Message.LifecycleState)
	{
		case FDirectLinkMsg_EndpointLifecycle::ELifecycle::Start:
		{
			// Noop: remote endpoint will broadcast it's state later
			ReplicateState(RemoteEndpointAddress);
			break;
		}

		case FDirectLinkMsg_EndpointLifecycle::ELifecycle::Heartbeat:
		{
			// #ue_directlink_streams handle connection loss, threshold, and last_message_time.
			// if now-last_message_time > threshold -> mark as dead

			bool bIsUpToDate = true;
			{
				FDirectLinkMsg_EndpointState* RemoteState = RemoteEndpointDescriptions.Find(RemoteEndpointAddress);

				bIsUpToDate = RemoteState
					&& RemoteState->StateRevision != 0
					&& RemoteState->StateRevision == Message.EndpointStateRevision;
			}

			if (!bIsUpToDate)
			{
				UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Log, TEXT("Endpoint '%s': Send FDirectLinkMsg_QueryEndpointState"), *SharedState.NiceName);
				MessageEndpoint->Send(NewMessage<FDirectLinkMsg_QueryEndpointState>(), RemoteEndpointAddress);
			}
			break;
		}

		case FDirectLinkMsg_EndpointLifecycle::ELifecycle::Stop:
		{
			if (FDirectLinkMsg_EndpointState* RemoteState = RemoteEndpointDescriptions.Find(RemoteEndpointAddress))
			{
				UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Display, TEXT("Endpoint '%s' removes '%s'"), *SharedState.NiceName, *RemoteState->NiceName);
				RemoteEndpointDescriptions.Remove(RemoteEndpointAddress);
			}

			// close remaining associated streams
			{
				FRWScopeLock _(SharedState.StreamsLock, SLT_Write);
				for (auto& Stream : SharedState.Streams)
				{
					if (Stream.RemoteAddress == RemoteEndpointAddress
					 && Stream.Status != FStreamDescription::EConnectionState::Closed)
					{
						bool bNotifyRemote = false;
						UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Log, TEXT("Endpoint '%s': Closed connection (reason: remote endpoint stopped)"), *SharedState.NiceName);
						Owner.CloseStreamInternal(Stream, _, bNotifyRemote);
					}
				}
			}

			break;
		}
	}
}


void FEndpoint::FInternalThreadState::Handle_QueryEndpointState(const FDirectLinkMsg_QueryEndpointState& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	check(IsInnerThread());
	ReplicateState(Context->GetSender());
}


void FEndpoint::FInternalThreadState::Handle_EndpointState(const FDirectLinkMsg_EndpointState& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	check(IsInnerThread());

	const FMessageAddress& RemoteEndpointAddress = Context->GetSender();
	if (IsMine(RemoteEndpointAddress))
	{
		return;
	}

	{
		FDirectLinkMsg_EndpointState& RemoteState = RemoteEndpointDescriptions.FindOrAdd(RemoteEndpointAddress);
		RemoteState = Message;
	}

	UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Log, TEXT("Endpoint '%s' Handle_EndpointState"), *SharedState.NiceName);
	UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Log, TEXT("%s"), *ToString_dbg());
}


void FEndpoint::FInternalThreadState::Handle_OpenStreamRequest(const FDirectLinkMsg_OpenStreamRequest& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	check(IsInnerThread());

	const FMessageAddress& RemoteEndpointAddress = Context->GetSender();

	if (Message.bRequestFromSource)
	{
		// request from source, we must have the destination.
		TSharedPtr<ISceneProvider> Provider;
		{
			FRWScopeLock _(SharedState.DestinationsLock, SLT_ReadOnly);
			for (const TSharedPtr<FStreamDestination>& Dest : SharedState.Destinations)
			{
				if (Dest->GetId() == Message.DestinationGuid)
				{
					Provider = Dest->Provider;
					break;
				}
			}
		}

		FDirectLinkMsg_OpenStreamAnswer* Answer = NewMessage<FDirectLinkMsg_OpenStreamAnswer>();
		Answer->RecipientStreamPort = Message.RequestFromStreamPort;
		Answer->bAccepted = Provider != nullptr && Provider->CanOpenNewConnection();

		if (Answer->bAccepted)
		{
			FRWScopeLock _(SharedState.StreamsLock, SLT_Write);
			FStreamPort StreamPort = ++SharedState.StreamPortIdGenerator;
			Answer->OpenedStreamPort = StreamPort;
			FStreamDescription& NewStream = SharedState.Streams.AddDefaulted_GetRef();
			NewStream.bThisIsSource = !Message.bRequestFromSource;
			NewStream.SourcePoint = Message.SourceGuid;
			NewStream.DestinationPoint = Message.DestinationGuid;
			NewStream.RemoteAddress = RemoteEndpointAddress;
			NewStream.RemoteStreamPort = Message.RequestFromStreamPort;
			NewStream.LocalStreamPort = StreamPort;
			NewStream.Status = FStreamDescription::EConnectionState::Active;
			NewStream.LastRemoteLifeSign = Now_s;

			bool bThisIsSource = !Message.bRequestFromSource;
			if (bThisIsSource)
			{
				check(false); // niy
			}
			else
			{
				NewStream.Receiver = MakeUnique<FStreamReceiver>();
				NewStream.Receiver->SetConsumer(Provider->GetDeltaConsumer(FSceneIdentifier{}));
			}
		}

		UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Log, TEXT("Endpoint '%s': Send FDirectLinkMsg_OpenStreamAnswer"), *SharedState.NiceName);
		MessageEndpoint->Send(Answer, RemoteEndpointAddress);
	}
	else
	{
		// #ue_directlink_streams todo: enable destinations to open stream
		ensure(!"not implemented yet");
	}

	UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Verbose, TEXT("Endpoint '%s': Handle_OpenStreamRequest"), *SharedState.NiceName);
	UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Verbose, TEXT("%s"), *ToString_dbg());
}


void FEndpoint::FInternalThreadState::Handle_OpenStreamAnswer(const FDirectLinkMsg_OpenStreamAnswer& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	check(IsInnerThread());
	const FMessageAddress& RemoteEndpointAddress = Context->GetSender();

	{
		FRWScopeLock _(SharedState.StreamsLock, SLT_Write);
		for (FStreamDescription& Stream : SharedState.Streams)
		{
			if (Stream.Status != FStreamDescription::EConnectionState::Closed
				&& Stream.LocalStreamPort == Message.RecipientStreamPort) // destination instead of recipient ?
			{
				if (Message.bAccepted)
				{
					Stream.RemoteStreamPort = Message.OpenedStreamPort;
					Stream.Sender = MakeUnique<FStreamSender>(MessageEndpoint);
					Stream.Sender->SetRemoteInfo(RemoteEndpointAddress, Message.OpenedStreamPort);

					Stream.Status = FStreamDescription::EConnectionState::Active;
				}
				else
				{
					Stream.Status = FStreamDescription::EConnectionState::Closed; // don't remove yet, as current stupid policy would just retry indefinitely...
				}
				Stream.LastRemoteLifeSign = Now_s;
				break;
			}
		}
	}

	UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Verbose, TEXT("Endpoint '%s': Handle_OpenStreamAnswer"), *SharedState.NiceName);
	UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Verbose, TEXT("%s"), *ToString_dbg());
}


void FEndpoint::FInternalThreadState::Handle_DeltaMessage(const FDirectLinkMsg_DeltaMessage& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	check(IsInnerThread());

	FRWScopeLock _(SharedState.StreamsLock, SLT_Write); // read array, lock specific stream ?
	for (FStreamDescription& Stream : SharedState.Streams)
	{
		if (Stream.LocalStreamPort == Message.DestinationStreamPort)
		{
			if (Stream.Status == FStreamDescription::EConnectionState::Active
			&& ensure(Stream.Receiver) && ensure(Stream.RemoteAddress == Context->GetSender()))
			{
				Stream.LastRemoteLifeSign = Now_s;
				Stream.Receiver->HandleDeltaMessage(Message);
				return;
			}
			UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Warning, TEXT("Endpoint '%s': Dropped delta message (inactive stream used on port %d)"), *SharedState.NiceName, Message.DestinationStreamPort);
			return;
		}
	}

	UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Warning, TEXT("Endpoint '%s': Dropped delta message (no stream found at port %d)"), *SharedState.NiceName, Message.DestinationStreamPort);
}


void FEndpoint::FInternalThreadState::Handle_CloseStreamRequest(const FDirectLinkMsg_CloseStreamRequest& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	{
		FRWScopeLock _(SharedState.StreamsLock, SLT_Write); // read array, lock specific stream ?
		for (FStreamDescription& Stream : SharedState.Streams)
		{
			if (Stream.LocalStreamPort == Message.RecipientStreamPort)
			{
				bool bNotifyRemote = false; // since it's already a request from Remote...
				Owner.CloseStreamInternal(Stream, _, bNotifyRemote);
			}
		}
	}

	UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Verbose, TEXT("Endpoint '%s': Handle_CloseStreamRequest"), *SharedState.NiceName);
	UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Verbose, TEXT("%s"), *ToString_dbg());
}


bool FEndpoint::FInternalThreadState::IsAliveOnNetwork() const
{
// #ue_directlink_cleanup not clear when this should be used.. before any message send ?
	return MessageEndpoint.IsValid() && SharedState.bInnerThreadShouldRun;
}


bool FEndpoint::FInternalThreadState::IsMine(const FMessageAddress& MaybeRemoteAddress) const
{
	return MessageEndpoint->GetAddress() == MaybeRemoteAddress;
}


void FEndpoint::FInternalThreadState::ReplicateState(const FMessageAddress& RemoteEndpointAddress) const
{
	check(IsInnerThread());

	if (IsAliveOnNetwork())
	{
		auto* EndpointStateMessage = NewMessage<FDirectLinkMsg_EndpointState>();
		{
// 			FRWScopeLock _(ThisDescriptionLock, SLT_ReadOnly);
			*EndpointStateMessage = ThisDescription;
		}

		if (RemoteEndpointAddress.IsValid())
		{
			UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Log, TEXT("Endpoint '%s': Send FDirectLinkMsg_EndpointState"), *SharedState.NiceName);
			MessageEndpoint->Send(EndpointStateMessage, RemoteEndpointAddress);
		}
		else
		{
			UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Log, TEXT("Endpoint '%s': Publish FDirectLinkMsg_EndpointState"), *SharedState.NiceName);
			LastBroadcastedStateRevision = EndpointStateMessage->StateRevision;
			MessageEndpoint->Publish(EndpointStateMessage);
		}
	}
}


void FEndpoint::FInternalThreadState::ReplicateState_Broadcast() const
{
	FMessageAddress Invalid;
	ReplicateState(Invalid);
}


void FEndpoint::FInternalThreadState::UpdateSourceDescription()
{
	check(IsInnerThread());
	{
		ThisDescription.Sources.Reset();
		FRWScopeLock _(SharedState.SourcesLock, SLT_ReadOnly);
		for (const TSharedPtr<FStreamSource>& Source : SharedState.Sources)
		{
			if (Source->GetVisibility() == EVisibility::Public)
			{
				ThisDescription.Sources.Add({Source->GetName(), Source->GetId()});
			}
		}
	}
	ThisDescription.StateRevision++;
}


void FEndpoint::FInternalThreadState::UpdateDestinationDescription()
{
	check(IsInnerThread());
	{
		ThisDescription.Destinations.Reset();
		FRWScopeLock _(SharedState.DestinationsLock, SLT_ReadOnly);
		for (const TSharedPtr<FStreamDestination>& Dest : SharedState.Destinations)
		{
			if (Dest->GetVisibility() == EVisibility::Public)
			{
				ThisDescription.Destinations.Add({Dest->GetName(), Dest->GetId()});
			}
		}
	}
	ThisDescription.StateRevision++;
}



FRawInfo::FEndpointInfo::FEndpointInfo(const FMessageAddress& A, const FDirectLinkMsg_EndpointState& Msg)
{
	// tbd
	Name = Msg.NiceName;
	x = Msg;
	a = A;
}


void FEndpoint::FInternalThreadState::Init()
{
	FMessageEndpointBuilder Builder(TEXT("DirectLinkEndpoint"));
	Builder
		.Handling<FDirectLinkMsg_EndpointLifecycle>(this, &FInternalThreadState::Handle_EndpointLifecycle)
		.Handling<FDirectLinkMsg_QueryEndpointState>(this, &FInternalThreadState::Handle_QueryEndpointState)
		.Handling<FDirectLinkMsg_EndpointState>(this, &FInternalThreadState::Handle_EndpointState)
		.Handling<FDirectLinkMsg_OpenStreamRequest>(this, &FInternalThreadState::Handle_OpenStreamRequest)
		.Handling<FDirectLinkMsg_OpenStreamAnswer>(this, &FInternalThreadState::Handle_OpenStreamAnswer)
		.Handling<FDirectLinkMsg_DeltaMessage>(this, &FInternalThreadState::Handle_DeltaMessage)
		.Handling<FDirectLinkMsg_CloseStreamRequest>(this, &FInternalThreadState::Handle_CloseStreamRequest)

		.WithInbox()
	;

	Now_s = FPlatformTime::Seconds();
	MessageEndpoint = Builder.Build();
	SharedState.MessageEndpoint = MessageEndpoint;
	if (ensure(IsAliveOnNetwork()))
	{
		MessageEndpoint->Subscribe<FDirectLinkMsg_EndpointLifecycle>();
		MessageEndpoint->Subscribe<FDirectLinkMsg_EndpointState>();
	}
	SharedState.bInnerThreadShouldRun = MessageEndpoint.IsValid();
}

void FEndpoint::FInternalThreadState::Run()
{
	// setup local endpoint description (aka replicated state)
	ThisDescription = FDirectLinkMsg_EndpointState(1, kCurrentProtocolVersion);
	ThisDescription.ComputerName = FPlatformProcess::ComputerName();
	ThisDescription.UserName = FPlatformProcess::UserName();
	ThisDescription.ProcessId = (int32)FPlatformProcess::GetCurrentProcessId();
	ThisDescription.ExecutableName = FPlatformProcess::ExecutableName();
	ThisDescription.NiceName = SharedState.NiceName;

	UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Display, TEXT("Endpoint '%s': Publishing FDirectLinkMsg_EndpointLifecycle Start"), *SharedState.NiceName);
	// #ue_directlink_cleanup used ?
	MessageEndpoint->Publish(NewMessage<FDirectLinkMsg_EndpointLifecycle>(FDirectLinkMsg_EndpointLifecycle::ELifecycle::Start));

	while (SharedState.bInnerThreadShouldRun)
	{
		Now_s = FPlatformTime::Seconds();

		// process local signals
		if (SharedState.bDirtySources.exchange(false))
		{
			UpdateSourceDescription();
		}
		if (SharedState.bDirtyDestinations.exchange(false))
		{
			UpdateDestinationDescription();
		}

		if (LastBroadcastedStateRevision != ThisDescription.StateRevision)
		{
			ReplicateState_Broadcast();
		}

		double HeartbeatThreshold_s = 5.0;
		if (Now_s - LastHeartbeatTime_s > HeartbeatThreshold_s)
		{
			UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Log, TEXT("Endpoint '%s': Publishing FDirectLinkMsg_EndpointLifecycle Heartbeat %f"), *SharedState.NiceName, Now_s);
			MessageEndpoint->Publish(NewMessage<FDirectLinkMsg_EndpointLifecycle>(FDirectLinkMsg_EndpointLifecycle::ELifecycle::Heartbeat, ThisDescription.StateRevision));
			LastHeartbeatTime_s = Now_s;
		}

		// consume remote messages
		MessageEndpoint->ProcessInbox();

		// #ue_directlink_cleanup temp connection policy.
		// for all local source, connect to all remote dest with the same name
		// reimpl with named broadcast source, and client connect themselves
		{
			for (const auto& Src : ThisDescription.Sources)
			{
				for (const auto& KV : RemoteEndpointDescriptions)
				{
					const FMessageAddress& Remote = KV.Key;

					for (const auto& Dst : KV.Value.Destinations)
					{
						if (Src.Name == Dst.Name)
						{
							Owner.OpenStream(Src.Id, Dst.Id);
						}
					}
				}
			}
		}

		// rebuild description of remote endpoints
		if (true) // #ue_directlink_integration flag based: user-side api driven
		{
			// prepare data - Endpoint part
			TMap<FMessageAddress, FRawInfo::FEndpointInfo> EndpointsInfo;
			EndpointsInfo.Reserve(RemoteEndpointDescriptions.Num());
			for (const auto& KV : RemoteEndpointDescriptions)
			{
				EndpointsInfo.Emplace(KV.Key, FRawInfo::FEndpointInfo{KV.Key, KV.Value});
			}
			FMessageAddress ThisEndpointAddress = MessageEndpoint->GetAddress();
			EndpointsInfo.Emplace(ThisEndpointAddress, FRawInfo::FEndpointInfo{ThisEndpointAddress, ThisDescription});

			// prepare data - sources and destinations
			TMap<FGuid, FRawInfo::FDataPointInfo> DataPointsInfo;
			auto l = [&](const FDirectLinkMsg_EndpointState& EpDescription, const FMessageAddress& EpAddress, bool bIsLocal)
			{
				for (const auto& Src : EpDescription.Sources)
				{
					DataPointsInfo.Add(Src.Id, FRawInfo::FDataPointInfo{EpAddress, true, bIsLocal});
				}
				for (const auto& Dst : EpDescription.Destinations)
				{
					DataPointsInfo.Add(Dst.Id, FRawInfo::FDataPointInfo{EpAddress, false, bIsLocal});
				}
			};

			l(ThisDescription, ThisEndpointAddress, true);
			for (const auto& KV : RemoteEndpointDescriptions)
			{
				l(KV.Value, KV.Key, false);
			}

			// prepare data - Streams part
			TArray<FRawInfo::FStreamInfo> StreamsInfo;
			{
				FRWScopeLock _(SharedState.StreamsLock, SLT_ReadOnly);
				StreamsInfo.Reserve(SharedState.Streams.Num());
				for (FStreamDescription& Stream : SharedState.Streams)
				{
					FRawInfo::FStreamInfo I;
					I.StreamId = Stream.LocalStreamPort;
					I.Source = Stream.SourcePoint;
					I.Destination = Stream.DestinationPoint;
					I.bIsActive = Stream.Status == FStreamDescription::EConnectionState::Active;
					StreamsInfo.Add(I);
				}
			}

			{
				// update info for local observers
				FRWScopeLock _(SharedState.RawInfoCopyLock, SLT_Write);
				SharedState.RawInfo.ThisEndpointAddress = ThisEndpointAddress;
				SharedState.RawInfo.EndpointsInfo = MoveTemp(EndpointsInfo);
				SharedState.RawInfo.DataPointsInfo = MoveTemp(DataPointsInfo);
				SharedState.RawInfo.StreamsInfo = MoveTemp(StreamsInfo);
			}

			{
				// Notify observers
				FRawInfo RawInfo = Owner.GetRawInfoCopy(); // stupid copy, but avoids locking 2 mutexes at once
				FRWScopeLock _(SharedState.ObserversLock, SLT_ReadOnly);
				for (IEndpointObserver* Observer : SharedState.Observers)
				{
					Observer->OnStateChanged(RawInfo);
				}
			}
		}

		// #ue_directlink_streams don't wait if active communication in progress
		Owner.InnerThreadEvent->Wait(FTimespan::FromMilliseconds(50));
	}

	UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Display, TEXT("Endpoint '%s': Publishing FDirectLinkMsg_EndpointLifecycle Stop"), *SharedState.NiceName);
	MessageEndpoint->Publish(NewMessage<FDirectLinkMsg_EndpointLifecycle>(FDirectLinkMsg_EndpointLifecycle::ELifecycle::Stop));
	FMessageEndpoint::SafeRelease(MessageEndpoint);
}

} // namespace DirectLink
