// Copyright Epic Games, Inc. All Rights Reserved.

#include "DirectLink/Network/DirectLinkEndpoint.h"

#include "DirectLink/DirectLinkLog.h"
#include "DirectLink/Network/DirectLinkISceneProvider.h"
#include "DirectLink/Network/DirectLinkMessages.h"
#include "DirectLink/Network/DirectLinkStream.h"
#include "DirectLink/Network/DirectLinkStreamSource.h"
#include "DirectLink/SceneGraphNode.h"

#include "Async/Async.h"
#include "MessageEndpointBuilder.h"

namespace DirectLink
{

struct
{
	// heartbeat message periodically sent to keep the connections alive
	double HeartbeatThreshold_s                = 5.0;

	// endpoint not seen for a long time:
	bool   bPeriodicalyCleanupTimedOutEndpoint = true;
	double ThresholdEndpointCleanup_s          = 30.0;
	double CleanupOldEndpointPeriod_s          = 10.0;

	// auto connect streams by name
	bool bAutoconnectFromSources               = true;
	bool bAutoconnectFromDestination           = false;
} gConfig;


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
			FPlatformProcess::SetThreadName(TEXT("DirectLink"));
			InnerThreadId = FPlatformTLS::GetCurrentThreadId();
			Internal.Run();
		}
	);
}


FEndpoint::~FEndpoint()
{
	SharedState.bInnerThreadShouldRun = false;
	InnerThreadEvent->Trigger();
	InnerThreadResult.Get(); // join
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
			if (Stream.SourcePoint == SourceId
			 && Stream.Status != FStreamDescription::EConnectionState::Closed)
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
	{
		FRWScopeLock _(SharedState.SourcesLock, SLT_Write); // make this a read, and detailed thread-safety inside the source
		for (TSharedPtr<FStreamSource>& Source : SharedState.Sources)
		{
			if (Source->GetId() == SourceId)
			{
				Source->Snapshot();
				break;
			}
		}
	}
}


FDestinationHandle FEndpoint::AddDestination(const FString& Name, EVisibility Visibility, const TSharedPtr<ISceneProvider>& Provider)
{
	FDestinationHandle Id;
	if (ensure(Provider.IsValid()))
	{
		FRWScopeLock _(SharedState.DestinationsLock, SLT_Write);
		TSharedPtr<FStreamDestination>& NewDest = SharedState.Destinations.Add_GetRef(MakeShared<FStreamDestination>(Name, Visibility, Provider));
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
			if (Stream.DestinationPoint == Destination
			 && Stream.Status != FStreamDescription::EConnectionState::Closed)
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


FEndpoint::EOpenStreamResult FEndpoint::OpenStream(const FSourceHandle& SourceId, const FDestinationHandle& DestinationId)
{
	// #ue_directlink_cleanup Merge with Handle_OpenStreamRequest
	// #ue_directlink_syncprotocol tempo before next allowed request ?
	// check if the stream is already opened
	{
		FRWScopeLock _(SharedState.StreamsLock, SLT_ReadOnly);
		for (const FStreamDescription& Stream : SharedState.Streams)
		{
			if (Stream.SourcePoint == SourceId && Stream.DestinationPoint == DestinationId)
			{
				if (Stream.Status == FStreamDescription::EConnectionState::Active
				 || Stream.Status == FStreamDescription::EConnectionState::RequestSent)
				{
					// useless case, temp because of the unfinished connection policy.
					// #ue_directlink_connexion Replace with proper policy (user driven connection map) + log if this happen
					return EOpenStreamResult::AlreadyOpened;
				}
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
			if (Destination->GetId() == DestinationId)
			{
				bRequestFromDestination = true;
				break;
			}
		}
	}

	if (!bRequestFromSource && !bRequestFromDestination)
	{
		// we don't have any side of the connection...
		UE_LOG(LogDirectLinkNet, Log, TEXT("Endpoint '%s': Cannot open stream: no source or destination point found."), *SharedState.NiceName);
		return EOpenStreamResult::SourceAndDestinationNotFound;
	}

	if (bRequestFromSource && bRequestFromDestination)
	{
		UE_LOG(LogDirectLinkNet, Log, TEXT("Endpoint '%s': Cannot open stream: have source and destination."), *SharedState.NiceName);
		return EOpenStreamResult::Unsuppported;
	}

	// Find Remote address.
	FMessageAddress RemoteAddress;
	{
		const FGuid& RemoteDataPointId = bRequestFromSource ? DestinationId : SourceId;
		// #ue_directlink_cleanup sad that we rely on raw info.
		FRWScopeLock _(SharedState.RawInfoCopyLock, SLT_ReadOnly);
		if (FRawInfo::FDataPointInfo* DataPointInfo = SharedState.RawInfo.DataPointsInfo.Find(RemoteDataPointId))
		{
			if (DataPointInfo->bIsPublic)
			{
				RemoteAddress = DataPointInfo->EndpointAddress;
			}
			else
			{
				UE_LOG(LogDirectLinkNet, Warning, TEXT("Endpoint '%s': Cannot open stream: Remote connection Point is private."), *SharedState.NiceName);
				return EOpenStreamResult::CannotConnectToPrivate;
			}
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
		return EOpenStreamResult::RemoteEndpointNotFound;
	}
	return EOpenStreamResult::Opened;
}


void FEndpoint::CloseStream(const FSourceHandle& SourceId, const FDestinationHandle& DestinationId)
{
	FRWScopeLock _(SharedState.StreamsLock, SLT_Write);
	for (FStreamDescription& Stream : SharedState.Streams)
	{
		if (Stream.SourcePoint == SourceId
		 && Stream.DestinationPoint == DestinationId
		 && Stream.Status != FStreamDescription::EConnectionState::Closed)
		{
			CloseStreamInternal(Stream, _);
		}
	}
}


void FEndpoint::CloseStreamInternal(FStreamDescription& Stream, const FRWScopeLock& _, bool bNotifyRemote)
{
	if (Stream.Status == FStreamDescription::EConnectionState::Closed)
	{
		return;
	}

	if (bNotifyRemote && Stream.RemoteAddress.IsValid())
	{
		UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Log, TEXT("Endpoint '%s': Stream removed"), *SharedState.NiceName, *Stream.SourcePoint.ToString());
		FDirectLinkMsg_CloseStreamRequest* Request = NewMessage<FDirectLinkMsg_CloseStreamRequest>();
		Request->RecipientStreamPort = Stream.RemoteStreamPort;

		UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Log, TEXT("Endpoint '%s': Send FDirectLinkMsg_CloseStreamRequest"), *SharedState.NiceName);
		SharedState.MessageEndpoint->Send(Request, Stream.RemoteAddress);
	}

	// close local stream
	Stream.Status = FStreamDescription::EConnectionState::Closed;
	Stream.Sender.Reset();
	Stream.Receiver.Reset(); // #ue_directlink_cleanup notify associated scene provider
}


FString FEndpoint::FInternalThreadState::ToString_dbg() const
{
	FString Out;
	{
		Out.Appendf(TEXT("Endpoint '%s' (%s):\n"), *SharedState.NiceName, *MessageEndpoint->GetAddress().ToString());
	}

	auto PrintEndpoint = [&](const FDirectLinkMsg_EndpointState& Endpoint, int32 RemoteEndpointIndex)
	{
		Out.Appendf(TEXT("-- endpoint #%d %s/%d:'%s' \n"),
			RemoteEndpointIndex,
			*Endpoint.ComputerName,
			Endpoint.ProcessId,
			*Endpoint.NiceName
		);

		Out.Appendf(TEXT("-- %d Sources:\n"), Endpoint.Sources.Num());
		int32 SrcIndex = 0;
		for (const FNamedId& Src : Endpoint.Sources)
		{
			Out.Appendf(TEXT("--- Source #%d: '%s' (%08X) %s\n"), SrcIndex, *Src.Name, Src.Id.A,
				Src.bIsPublic ? TEXT("public"):TEXT("private"));
			SrcIndex++;
		}

		Out.Appendf(TEXT("-- %d Destinations:\n"), Endpoint.Destinations.Num());
		int32 DestinationIndex = 0;
		for (const FNamedId& Dest : Endpoint.Destinations)
		{
			Out.Appendf(TEXT("--- Dest #%d: '%s' (%08X) %s\n"), DestinationIndex, *Dest.Name, Dest.Id.A,
				Dest.bIsPublic ? TEXT("public"):TEXT("private"));
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
			FGuid LocalPoint = Stream.bThisIsSource ? Stream.SourcePoint : Stream.DestinationPoint;
			FGuid RemotePoint = Stream.bThisIsSource ? Stream.DestinationPoint : Stream.SourcePoint;
			const TCHAR* OrientationText = Stream.bThisIsSource ? TEXT(">>>") : TEXT("<<<");
			const TCHAR* StatusText = TEXT("?"); //Stream.stabThisIsSource ? 'S' : 'D';
			switch (Stream.Status)
			{
				case FStreamDescription::EConnectionState::Uninitialized: StatusText = TEXT("Uninitialized"); break;
				case FStreamDescription::EConnectionState::RequestSent:   StatusText = TEXT("RequestSent  "); break;
				case FStreamDescription::EConnectionState::Active:        StatusText = TEXT("Active       "); break;
				case FStreamDescription::EConnectionState::Closed:        StatusText = TEXT("Closed       "); break;
			}
			Out.Appendf(TEXT("-- [%s] stream: %08X:%d %s %08X:%d\n"), StatusText, LocalPoint.A, Stream.LocalStreamPort, OrientationText, RemotePoint.A, Stream.RemoteStreamPort);
		}
	}

	return Out;
}


void FEndpoint::FInternalThreadState::Handle_DeltaMessage(const FDirectLinkMsg_DeltaMessage& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	{
		FRWScopeLock _(SharedState.StreamsLock, SLT_Write);
		// #ue_directlink_cleanup read array, lock specific stream ? TArray<TUniquePtr<>> ?
		// -> decorelate streams descriptions from actualr sender receiver

		FStreamDescription* StreamPtr = SharedState.GetStreamByLocalPort(Message.DestinationStreamPort, _);
		if (StreamPtr == nullptr)
		{
			UE_LOG(LogDirectLinkNet, Warning, TEXT("Endpoint '%s': Dropped delta message (no stream at port %d)"), *SharedState.NiceName, Message.DestinationStreamPort);
			return;
		}

		FStreamDescription& Stream = *StreamPtr;
		bool bIsActive = Stream.Status == FStreamDescription::EConnectionState::Active;
		bool bIsReceiver = Stream.Receiver.IsValid();
		bool bIsExpectedSender = Stream.RemoteAddress == Context->GetSender();
		if (!bIsActive || !bIsReceiver || !bIsExpectedSender)
		{
			UE_LOG(LogDirectLinkNet, Warning, TEXT("Endpoint '%s': Dropped delta message (inactive stream used on port %d)"), *SharedState.NiceName, Message.DestinationStreamPort);
			return;
		}

		Stream.Receiver->HandleDeltaMessage(Message);
		Stream.LastRemoteLifeSign = Now_s;
	}
}


void FEndpoint::FInternalThreadState::Handle_HaveListMessage(const FDirectLinkMsg_HaveListMessage& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	{
		FRWScopeLock _(SharedState.StreamsLock, SLT_Write);

		FStreamDescription* StreamPtr = SharedState.GetStreamByLocalPort(Message.SourceStreamPort, _);
		if (StreamPtr == nullptr)
		{
			UE_LOG(LogDirectLinkNet, Warning, TEXT("Endpoint '%s': Dropped havelist message (no stream at port %d)"), *SharedState.NiceName, Message.SourceStreamPort);
			return;
		}

		FStreamDescription& Stream = *StreamPtr;
		bool bIsActive = Stream.Status == FStreamDescription::EConnectionState::Active;
		bool bIsSender = Stream.Sender.IsValid();
		bool bIsExpectedRemote = Stream.RemoteAddress == Context->GetSender();
		if (!bIsActive || !bIsSender || !bIsExpectedRemote)
		{
			UE_LOG(LogDirectLinkNet, Warning, TEXT("Endpoint '%s': Dropped havelist message (inactive stream used on port %d)"), *SharedState.NiceName, Message.SourceStreamPort);
			return;
		}

		Stream.Sender->HandleHaveListMessage(Message);
		Stream.LastRemoteLifeSign = Now_s;
	}
}


void FEndpoint::FInternalThreadState::Handle_EndpointLifecycle(const FDirectLinkMsg_EndpointLifecycle& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	// #ue_directlink_quality ignore incompatible peers (based on serial revision)

	const FMessageAddress& RemoteEndpointAddress = Context->GetSender();
	if (IsMine(RemoteEndpointAddress))
	{
		return;
	}

	UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Verbose, TEXT("Endpoint '%s': Handle_EndpointLifecycle"), *SharedState.NiceName);

	MarkRemoteAsSeen(RemoteEndpointAddress);
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

			FDirectLinkMsg_EndpointState* RemoteState = RemoteEndpointDescriptions.Find(RemoteEndpointAddress);

			bool bIsUpToDate = RemoteState
				&& RemoteState->StateRevision != 0
				&& RemoteState->StateRevision == Message.EndpointStateRevision;

			if (!bIsUpToDate)
			{
				UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Log, TEXT("Endpoint '%s': Send FDirectLinkMsg_QueryEndpointState"), *SharedState.NiceName);
				MessageEndpoint->Send(NewMessage<FDirectLinkMsg_QueryEndpointState>(), RemoteEndpointAddress);
			}
			break;
		}

		case FDirectLinkMsg_EndpointLifecycle::ELifecycle::Stop:
		{
			RemoveEndpoint(RemoteEndpointAddress);
			break;
		}
	}
}


void FEndpoint::FInternalThreadState::Handle_QueryEndpointState(const FDirectLinkMsg_QueryEndpointState& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	ReplicateState(Context->GetSender());
}


void FEndpoint::FInternalThreadState::Handle_EndpointState(const FDirectLinkMsg_EndpointState& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	const FMessageAddress& RemoteEndpointAddress = Context->GetSender();
	if (IsMine(RemoteEndpointAddress))
	{
		return;
	}

	{
		FDirectLinkMsg_EndpointState& RemoteState = RemoteEndpointDescriptions.FindOrAdd(RemoteEndpointAddress);
		RemoteState = Message;
		MarkRemoteAsSeen(RemoteEndpointAddress);
	}

	UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Log, TEXT("Endpoint '%s' Handle_EndpointState"), *SharedState.NiceName);
	UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Log, TEXT("%s"), *ToString_dbg());
}


void FEndpoint::FInternalThreadState::Handle_OpenStreamRequest(const FDirectLinkMsg_OpenStreamRequest& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	// #ue_directlink_cleanup refuse connection if local connection point is private
	const FMessageAddress& RemoteEndpointAddress = Context->GetSender();

	FDirectLinkMsg_OpenStreamAnswer* Answer = NewMessage<FDirectLinkMsg_OpenStreamAnswer>();
	Answer->RecipientStreamPort = Message.RequestFromStreamPort;

	// first, check if that stream is already opened
	{
		FRWScopeLock _(SharedState.StreamsLock, SLT_Write);
		for (FStreamDescription& Stream : SharedState.Streams)
		{
			if (Stream.SourcePoint == Message.SourceGuid && Stream.DestinationPoint == Message.DestinationGuid)
			{
				// #ue_directlink_cleanup implement a robust handling of duplicated connections, reopened connections, etc...
				if (Stream.Status == FStreamDescription::EConnectionState::Active)
				{
					Answer->bAccepted = false; // #ue_directlink_cleanup send the same enum as returned by OpenStream
					Answer->Error = TEXT("connection already active"); // #ue_directlink_cleanup merge with OpenStream, and enum to text
					UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Log, TEXT("Endpoint '%s': Send FDirectLinkMsg_OpenStreamAnswer (refused, already active)"), *SharedState.NiceName);
					MessageEndpoint->Send(Answer, RemoteEndpointAddress);
					return;
				}
			}
		}
	}

	TUniquePtr<FStreamReceiver> NewReceiver;
	TSharedPtr<FStreamSender> NewSender;
	if (Message.bRequestFromSource)
	{
		NewReceiver = MakeReceiver(Message.SourceGuid, Message.DestinationGuid, RemoteEndpointAddress, Message.RequestFromStreamPort);
	}
	else
	{
		NewSender = MakeSender(Message.SourceGuid, RemoteEndpointAddress, Message.RequestFromStreamPort);
	}

	Answer->bAccepted = NewSender.IsValid() || NewReceiver.IsValid();

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
		NewStream.Sender = MoveTemp(NewSender);
		NewStream.Receiver = MoveTemp(NewReceiver);
		NewStream.Status = FStreamDescription::EConnectionState::Active;
		NewStream.LastRemoteLifeSign = Now_s;
	}

	UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Log, TEXT("Endpoint '%s': Send FDirectLinkMsg_OpenStreamAnswer (accepted)"), *SharedState.NiceName);
	MessageEndpoint->Send(Answer, RemoteEndpointAddress);

	UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Verbose, TEXT("Endpoint '%s': Handle_OpenStreamRequest"), *SharedState.NiceName);
	UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Verbose, TEXT("%s"), *ToString_dbg());
}


void FEndpoint::FInternalThreadState::Handle_OpenStreamAnswer(const FDirectLinkMsg_OpenStreamAnswer& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Verbose, TEXT("Endpoint '%s': Handle_OpenStreamAnswer"), *SharedState.NiceName);
	const FMessageAddress& RemoteEndpointAddress = Context->GetSender();

	{
		FRWScopeLock _(SharedState.StreamsLock, SLT_Write);
		if (FStreamDescription* StreamPtr = SharedState.GetStreamByLocalPort(Message.RecipientStreamPort, _))
		{
			FStreamDescription& Stream = *StreamPtr;
			if (Stream.Status == FStreamDescription::EConnectionState::RequestSent)
			{
				if (Message.bAccepted)
				{
					Stream.RemoteStreamPort = Message.OpenedStreamPort;
					if (Stream.bThisIsSource)
					{
						Stream.Sender = MakeSender(Stream.SourcePoint, RemoteEndpointAddress, Message.OpenedStreamPort);
					}
					else
					{
						Stream.Receiver = MakeReceiver(Stream.SourcePoint, Stream.DestinationPoint, RemoteEndpointAddress, Message.OpenedStreamPort);
					}

					check(Stream.Receiver || Stream.Sender)
					Stream.Status = FStreamDescription::EConnectionState::Active;
				}
				else
				{
					Stream.Status = FStreamDescription::EConnectionState::Closed;
					UE_LOG(LogDirectLinkNet, Warning, TEXT("stream connection refused. %s"), *Message.Error);
				}

				Stream.LastRemoteLifeSign = Now_s;
			}
		}
		else
		{
			UE_LOG(LogDirectLinkNet, Warning, TEXT("error: no such stream (%d)"), Message.RecipientStreamPort);
		}
	}

	UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Verbose, TEXT("%s"), *ToString_dbg());
}


void FEndpoint::FInternalThreadState::Handle_CloseStreamRequest(const FDirectLinkMsg_CloseStreamRequest& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
{
	{
		FRWScopeLock _(SharedState.StreamsLock, SLT_Write); // read array, lock specific stream ?
		if (FStreamDescription* StreamPtr = SharedState.GetStreamByLocalPort(Message.RecipientStreamPort, _))
		{
			bool bNotifyRemote = false; // since it's already a request from Remote...
			Owner.CloseStreamInternal(*StreamPtr, _, bNotifyRemote);
		}
	}

	UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Verbose, TEXT("Endpoint '%s': Handle_CloseStreamRequest"), *SharedState.NiceName);
	UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Verbose, TEXT("%s"), *ToString_dbg());
}


bool FEndpoint::FInternalThreadState::IsMine(const FMessageAddress& MaybeRemoteAddress) const
{
	return MessageEndpoint->GetAddress() == MaybeRemoteAddress;
}


void FEndpoint::FInternalThreadState::ReplicateState(const FMessageAddress& RemoteEndpointAddress) const
{
	if (MessageEndpoint.IsValid())
	{
		FDirectLinkMsg_EndpointState* EndpointStateMessage = NewMessage<FDirectLinkMsg_EndpointState>();
		*EndpointStateMessage = ThisDescription;

		if (RemoteEndpointAddress.IsValid())
		{
			UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Verbose, TEXT("Endpoint '%s': Send FDirectLinkMsg_EndpointState"), *SharedState.NiceName);
			MessageEndpoint->Send(EndpointStateMessage, RemoteEndpointAddress);
		}
		else
		{
			UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Verbose, TEXT("Endpoint '%s': Publish FDirectLinkMsg_EndpointState"), *SharedState.NiceName);
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
	{
		ThisDescription.Sources.Reset();
		FRWScopeLock _(SharedState.SourcesLock, SLT_ReadOnly);
		for (const TSharedPtr<FStreamSource>& Source : SharedState.Sources)
		{
			ThisDescription.Sources.Add({Source->GetName(), Source->GetId(), Source->IsPublic()});
		}
	}
	ThisDescription.StateRevision++;
}


void FEndpoint::FInternalThreadState::UpdateDestinationDescription()
{
	{
		ThisDescription.Destinations.Reset();
		FRWScopeLock _(SharedState.DestinationsLock, SLT_ReadOnly);
		for (const TSharedPtr<FStreamDestination>& Dest : SharedState.Destinations)
		{
			ThisDescription.Destinations.Add({Dest->GetName(), Dest->GetId(), Dest->IsPublic()});
		}
	}
	ThisDescription.StateRevision++;
}


TUniquePtr<FStreamReceiver> FEndpoint::FInternalThreadState::MakeReceiver(FGuid SourceGuid, FGuid DestinationGuid, FMessageAddress RemoteAddress, FStreamPort RemotePort)
{
	{
		FRWScopeLock _(SharedState.DestinationsLock, SLT_ReadOnly);
		for (const TSharedPtr<FStreamDestination>& Dest : SharedState.Destinations)
		{
			if (Dest->GetId() == DestinationGuid)
			{
				const TSharedPtr<ISceneProvider>& Provider = Dest->GetProvider();
				check(Provider);

				ISceneProvider::FSourceInformation SourceInfo;
				SourceInfo.Id = SourceGuid;

				if (Provider->CanOpenNewConnection(SourceInfo))
				{
					if (TSharedPtr<ISceneReceiver> DeltaConsumer = Provider->GetSceneReceiver(SourceInfo))
					{
						return MakeUnique<FStreamReceiver>(MessageEndpoint, RemoteAddress, RemotePort, DeltaConsumer.ToSharedRef());
					}
				}

				UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Warning, TEXT("Endpoint '%s': Handle_OpenStreamRequest: new connection refused by provider"), *SharedState.NiceName);
				break;
			}
		}
	}

	return nullptr;
}


TSharedPtr<FStreamSender> FEndpoint::FInternalThreadState::MakeSender(FGuid SourceGuid, FMessageAddress RemoteAddress, FStreamPort RemotePort)
{
	{
		FRWScopeLock _(SharedState.SourcesLock, SLT_ReadOnly);
		for (const TSharedPtr<FStreamSource>& Source : SharedState.Sources)
		{
			if (Source->GetId() == SourceGuid)
			{
				TSharedPtr<FStreamSender> Sender = MakeShared<FStreamSender>(MessageEndpoint, RemoteAddress, RemotePort);
				Source->LinkSender(Sender);
				return Sender;
			}
		}
	}

	return nullptr;
}


void FEndpoint::FInternalThreadState::RemoveEndpoint(const FMessageAddress& RemoteEndpointAddress)
{
	if (FDirectLinkMsg_EndpointState* RemoteState = RemoteEndpointDescriptions.Find(RemoteEndpointAddress))
	{
		UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Display, TEXT("Endpoint '%s' removes '%s'"), *SharedState.NiceName, *RemoteState->NiceName);
	}

	RemoteEndpointDescriptions.Remove(RemoteEndpointAddress);
	RemoteLastSeenTime.Remove(RemoteEndpointAddress);

	// close remaining associated streams
	{
		FRWScopeLock _(SharedState.StreamsLock, SLT_Write);
		for (auto& Stream : SharedState.Streams)
		{
			if (Stream.RemoteAddress == RemoteEndpointAddress
				&& Stream.Status != FStreamDescription::EConnectionState::Closed)
			{
				UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Log, TEXT("Endpoint '%s': Closed connection  (reason: remote endpoint removed)"), *SharedState.NiceName);
				bool bNotifyRemote = false;
				Owner.CloseStreamInternal(Stream, _, bNotifyRemote);
			}
		}
	}
}


void FEndpoint::FInternalThreadState::MarkRemoteAsSeen(const FMessageAddress& RemoteEndpointAddress)
{
	RemoteLastSeenTime.Add(RemoteEndpointAddress, Now_s);
}


void FEndpoint::FInternalThreadState::CleanupTimedOutEndpoint()
{
	TArray<FMessageAddress> RemovableEndpoints;
	for (const auto& KV : RemoteEndpointDescriptions)
	{
		if (double* LastSeen = RemoteLastSeenTime.Find(KV.Key))
		{
			if (Now_s - *LastSeen > gConfig.ThresholdEndpointCleanup_s)
			{
				RemovableEndpoints.Add(KV.Key);
				UE_LOG(LogDirectLinkNet, Log, TEXT("Endpoint '%s': Removed Endpoint %s (timeout)"), *SharedState.NiceName, *KV.Value.NiceName);
			}
		}
	}

	for (const FMessageAddress& RemovableEndpoint : RemovableEndpoints)
	{
		RemoveEndpoint(RemovableEndpoint);
	}
}


FRawInfo::FEndpointInfo::FEndpointInfo(const FDirectLinkMsg_EndpointState& Msg)
	: Name(Msg.NiceName)
	, Destinations(Msg.Destinations)
	, Sources(Msg.Sources)
	, UserName(Msg.UserName)
	, ExecutableName(Msg.ExecutableName)
	, ComputerName(Msg.ComputerName)
	, ProcessId(Msg.ProcessId)
{
}


void FEndpoint::FInternalThreadState::Init()
{
	MessageEndpoint = FMessageEndpoint::Builder(TEXT("DirectLinkEndpoint"))
		.Handling<FDirectLinkMsg_DeltaMessage>(this, &FInternalThreadState::Handle_DeltaMessage)
		.Handling<FDirectLinkMsg_HaveListMessage>(this, &FInternalThreadState::Handle_HaveListMessage)
		.Handling<FDirectLinkMsg_EndpointLifecycle>(this, &FInternalThreadState::Handle_EndpointLifecycle)
		.Handling<FDirectLinkMsg_QueryEndpointState>(this, &FInternalThreadState::Handle_QueryEndpointState)
		.Handling<FDirectLinkMsg_EndpointState>(this, &FInternalThreadState::Handle_EndpointState)
		.Handling<FDirectLinkMsg_OpenStreamRequest>(this, &FInternalThreadState::Handle_OpenStreamRequest)
		.Handling<FDirectLinkMsg_OpenStreamAnswer>(this, &FInternalThreadState::Handle_OpenStreamAnswer)
		.Handling<FDirectLinkMsg_CloseStreamRequest>(this, &FInternalThreadState::Handle_CloseStreamRequest)
		.WithInbox();

	if (ensure(MessageEndpoint.IsValid()))
	{
		MessageEndpoint->Subscribe<FDirectLinkMsg_EndpointLifecycle>();
		MessageEndpoint->Subscribe<FDirectLinkMsg_EndpointState>();
		SharedState.MessageEndpoint = MessageEndpoint;
		SharedState.bInnerThreadShouldRun = true;
		Now_s = FPlatformTime::Seconds();
	}
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

	UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Verbose, TEXT("Endpoint '%s': Publishing FDirectLinkMsg_EndpointLifecycle Start"), *SharedState.NiceName);

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

		if (Now_s - LastHeartbeatTime_s > gConfig.HeartbeatThreshold_s)
		{
			UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Verbose, TEXT("Endpoint '%s': Publishing FDirectLinkMsg_EndpointLifecycle Heartbeat %f"), *SharedState.NiceName, Now_s);
			MessageEndpoint->Publish(NewMessage<FDirectLinkMsg_EndpointLifecycle>(FDirectLinkMsg_EndpointLifecycle::ELifecycle::Heartbeat, ThisDescription.StateRevision));
			LastHeartbeatTime_s = Now_s;
		}

		// consume remote messages
		MessageEndpoint->ProcessInbox();

		// cleanup old endpoints
		if (gConfig.bPeriodicalyCleanupTimedOutEndpoint
		 && (Now_s - LastEndpointCleanupTime_s > gConfig.CleanupOldEndpointPeriod_s))
		{
			CleanupTimedOutEndpoint();
			LastEndpointCleanupTime_s = Now_s;
		}

		// sync send
		{
			FRWScopeLock _(SharedState.StreamsLock, SLT_ReadOnly);
			for (FStreamDescription& Stream : SharedState.Streams)
			{
				if (Stream.Status == FStreamDescription::EConnectionState::Active
					&& Stream.bThisIsSource && ensure(Stream.Sender.IsValid()))
				{
					Stream.Sender->Tick();
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
				EndpointsInfo.Emplace(KV.Key, FRawInfo::FEndpointInfo{KV.Value});
			}
			FMessageAddress ThisEndpointAddress = MessageEndpoint->GetAddress();
			EndpointsInfo.Emplace(ThisEndpointAddress, FRawInfo::FEndpointInfo{ThisDescription});

			// prepare data - sources and destinations
			TMap<FGuid, FRawInfo::FDataPointInfo> DataPointsInfo;
			auto l = [&](const FDirectLinkMsg_EndpointState& EpDescription, const FMessageAddress& EpAddress, bool bIsLocal)
			{
				for (const auto& Src : EpDescription.Sources)
				{
					DataPointsInfo.Add(Src.Id, FRawInfo::FDataPointInfo{EpAddress, Src.Name, true, bIsLocal, Src.bIsPublic});
				}
				for (const auto& Dst : EpDescription.Destinations)
				{
					DataPointsInfo.Add(Dst.Id, FRawInfo::FDataPointInfo{EpAddress, Dst.Name, false, bIsLocal, Dst.bIsPublic});
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

		// #ue_directlink_connexion temp autoconnect policy.
		// for all local source, connect to all remote dest with the same name
		// reimpl with named broadcast source, and client connect themselves

		if (gConfig.bAutoconnectFromSources || gConfig.bAutoconnectFromDestination)
		{
			TArray<FNamedId> AllSources = gConfig.bAutoconnectFromSources ? ThisDescription.Sources : TArray<FNamedId>{};
			TArray<FNamedId> AllDestinations = gConfig.bAutoconnectFromDestination ? ThisDescription.Destinations : TArray<FNamedId>{};

			for (const auto& KV : RemoteEndpointDescriptions)
			{
				if (gConfig.bAutoconnectFromSources)
				{
					for (const auto& Dst : KV.Value.Destinations)
					{
						if (Dst.bIsPublic) AllDestinations.Add(Dst);
					}
				}
				if (gConfig.bAutoconnectFromDestination)
				{
					for (const auto& Src : KV.Value.Sources)
					{
						if (Src.bIsPublic) AllSources.Add(Src);
					}
				}
			}

			for (const auto& Src : AllSources)
			{
				for (const auto& Dst : AllDestinations)
				{
					if (Src.Name == Dst.Name)
					{
						Owner.OpenStream(Src.Id, Dst.Id);
					}
				}
			}
		}

		if (MessageEndpoint->IsInboxEmpty())
		{
			Owner.InnerThreadEvent->Wait(FTimespan::FromMilliseconds(50));
		}
	}

	UE_CLOG(SharedState.bDebugLog, LogDirectLinkNet, Display, TEXT("Endpoint '%s': Publishing FDirectLinkMsg_EndpointLifecycle Stop"), *SharedState.NiceName);
	MessageEndpoint->Publish(NewMessage<FDirectLinkMsg_EndpointLifecycle>(FDirectLinkMsg_EndpointLifecycle::ELifecycle::Stop));
	FMessageEndpoint::SafeRelease(MessageEndpoint);
}


FStreamDescription* FEndpoint::FSharedState::GetStreamByLocalPort(FStreamPort LocalPort, const FRWScopeLock& _)
{
	// try to skip a lookup
	if (Streams.IsValidIndex(LocalPort-1)
		&& ensure(Streams[LocalPort-1].LocalStreamPort == LocalPort))
	{
		return &Streams[LocalPort-1];
	}

	for (FStreamDescription& Stream : Streams)
	{
		if (Stream.LocalStreamPort == LocalPort)
		{
			return &Stream;
		}
	}
	return nullptr;
}

} // namespace DirectLink
