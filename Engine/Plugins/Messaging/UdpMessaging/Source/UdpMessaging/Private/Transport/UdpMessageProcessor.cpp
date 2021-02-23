// Copyright Epic Games, Inc. All Rights Reserved.

#include "Transport/UdpMessageProcessor.h"
#include "UdpMessagingPrivate.h"

#include "Common/UdpSocketSender.h"
#include "HAL/Event.h"
#include "HAL/RunnableThread.h"
#include "IMessageAttachment.h"
#include "Serialization/ArrayReader.h"
#include "Serialization/ArrayWriter.h"
#include "Sockets.h"
#include "Math/UnrealMathUtility.h"
#include "UObject/Class.h"

#include "Shared/UdpMessagingSettings.h"
#include "Transport/UdpMessageBeacon.h"
#include "Transport/UdpMessageSegmenter.h"
#include "Transport/UdpReassembledMessage.h"
#include "Transport/UdpSerializedMessage.h"
#include "Transport/UdpSerializeMessageTask.h"


/* FUdpMessageHelloSender static initialization
 *****************************************************************************/

const int32 FUdpMessageProcessor::DeadHelloIntervals = 5;


/* FUdpMessageProcessor structors
 *****************************************************************************/

FUdpMessageProcessor::FUdpMessageProcessor(FSocket& InSocket, const FGuid& InNodeId, const FIPv4Endpoint& InMulticastEndpoint)
	: Beacon(nullptr)
	, LocalNodeId(InNodeId)
	, LastSentMessage(-1)
	, MulticastEndpoint(InMulticastEndpoint)
	, Socket(&InSocket)
	, SocketSender(nullptr)
	, Stopping(false)
	, MessageFormat(GetDefault<UUdpMessagingSettings>()->MessageFormat) // NOTE: When the message format changes (in the Udp Messaging settings panel), the service is restarted and the processor recreated.
{
	WorkEvent = MakeShareable(FPlatformProcess::GetSynchEventFromPool(), [](FEvent* EventToDelete)
	{
		FPlatformProcess::ReturnSynchEventToPool(EventToDelete);
	});

	CurrentTime = FDateTime::UtcNow();
	Thread = FRunnableThread::Create(this, TEXT("FUdpMessageProcessor"), 128 * 1024, TPri_AboveNormal, FPlatformAffinity::GetPoolThreadMask());
}


FUdpMessageProcessor::~FUdpMessageProcessor()
{
	// shut down worker thread
	Thread->Kill(true);
	delete Thread;
	Thread = nullptr;

	// remove all transport nodes
	if (NodeLostDelegate.IsBound())
	{
		for (auto& KnownNodePair : KnownNodes)
		{
			NodeLostDelegate.Execute(KnownNodePair.Key);
		}
	}

	KnownNodes.Empty();
}


void FUdpMessageProcessor::AddStaticEndpoint(const FIPv4Endpoint& InEndpoint)
{
	if (Beacon)
	{
		Beacon->AddStaticEndpoint(InEndpoint);
	}
}


void FUdpMessageProcessor::RemoveStaticEndpoint(const FIPv4Endpoint& InEndpoint)
{
	if (Beacon)
	{
		Beacon->RemoveStaticEndpoint(InEndpoint);
	}
}

/* FUdpMessageProcessor interface
 *****************************************************************************/

TMap<uint8, TArray<FGuid>> FUdpMessageProcessor::GetRecipientsPerProtocolVersion(const TArray<FGuid>& Recipients)
{
	TMap<uint8, TArray<FGuid>> NodesPerVersion;
	{
		FScopeLock NodeVersionsLock(&NodeVersionCS);

		// No recipients means a publish, so broadcast to all known nodes (static nodes are in known nodes.)
		// We used to broadcast on the multicast endpoint, but the discovery of nodes should have found available nodes using multicast already
		if (Recipients.Num() == 0)
		{
			for (auto& NodePair : NodeVersions)
			{
				NodesPerVersion.FindOrAdd(NodePair.Value).Add(NodePair.Key);
			}
		}
		else
		{
			for (const FGuid& Recipient : Recipients)
			{
				uint8* Version = NodeVersions.Find(Recipient);
				if (Version)
				{
					NodesPerVersion.FindOrAdd(*Version).Add(Recipient);
				}
			}
		}
	}
	return NodesPerVersion;
}

bool FUdpMessageProcessor::EnqueueInboundSegment(const TSharedPtr<FArrayReader, ESPMode::ThreadSafe>& Data, const FIPv4Endpoint& InSender)
{
	if (Stopping)
	{
		return false;
	}

	if (!InboundSegments.Enqueue(FInboundSegment(Data, InSender)))
	{
		return false;
	}

	WorkEvent->Trigger();

	return true;
}

bool FUdpMessageProcessor::EnqueueOutboundMessage(const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& MessageContext, const TArray<FGuid>& Recipients)
{
	if (Stopping)
	{
		return false;
	}
	
	TMap<uint8, TArray<FGuid>> RecipientPerVersions = GetRecipientsPerProtocolVersion(Recipients);
	for (const auto& RecipientVersion : RecipientPerVersions)
	{
		// Create a message to serialize using that protocol version
		TSharedRef<FUdpSerializedMessage, ESPMode::ThreadSafe> SerializedMessage = MakeShared<FUdpSerializedMessage, ESPMode::ThreadSafe>(MessageFormat, RecipientVersion.Key, MessageContext->GetFlags());

		// Kick off the serialization task
		TGraphTask<FUdpSerializeMessageTask>::CreateTask().ConstructAndDispatchWhenReady(MessageContext, SerializedMessage, WorkEvent);

		// Enqueue the message
		if (!OutboundMessages.Enqueue(FOutboundMessage(SerializedMessage, RecipientVersion.Value)))
		{
			return false;
		}
	}

	return true;
}

/* FRunnable interface
 *****************************************************************************/

FSingleThreadRunnable* FUdpMessageProcessor::GetSingleThreadInterface()
{
	return this;
}


bool FUdpMessageProcessor::Init()
{
	Beacon = new FUdpMessageBeacon(Socket, LocalNodeId, MulticastEndpoint);
	SocketSender = new FUdpSocketSender(Socket, TEXT("FUdpMessageProcessor.Sender"));

	// Current protocol version 14
	SupportedProtocolVersions.Add(UDP_MESSAGING_TRANSPORT_PROTOCOL_VERSION);
	// Support Protocol version 10, 11, 12, 13
	SupportedProtocolVersions.Add(13);
	SupportedProtocolVersions.Add(12);
	SupportedProtocolVersions.Add(11);
	SupportedProtocolVersions.Add(10);

	return true;
}


uint32 FUdpMessageProcessor::Run()
{
	while (!Stopping)
	{
		FDateTime LastTime = CurrentTime;
		CurrentTime = FDateTime::UtcNow();
		DeltaTime = CurrentTime - LastTime;

		if (WorkEvent->Wait(CalculateWaitTime()))
		{
			ConsumeInboundSegments();
			ConsumeOutboundMessages();
		}
		UpdateKnownNodes();
	}

	delete Beacon;
	Beacon = nullptr;

	delete SocketSender;
	SocketSender = nullptr;

	return 0;
}


void FUdpMessageProcessor::Stop()
{
	Stopping = true;
	WorkEvent->Trigger();
}


void FUdpMessageProcessor::WaitAsyncTaskCompletion()
{
	// Stop the processor thread
	Stop();

	// Make sure we stopped, so we can access KnownNodes safely
	while (SocketSender != nullptr)
	{
		FPlatformProcess::Sleep(0); // Yield.
	}

	// Check if processor has in-flight serialization task(s).
	auto HasIncompleteSerializationTasks = [this]()
	{
		for (const TPair<FGuid, FNodeInfo>& GuidNodeInfoPair : KnownNodes)
		{
			for (const TPair<int32, TSharedPtr<FUdpMessageSegmenter>>& SegmenterPair: GuidNodeInfoPair.Value.Segmenters)
			{
				if (!SegmenterPair.Value->IsMessageSerializationDone())
				{
					return true;
				}
			}
		}

		return false;
	};

	// Ensures the task graph doesn't contain any pending/running serialization tasks after the processor exit. If the engine is shutting down, the serialization (UStruct) might
	// not be available anymore when the task is run (The task graph shuts down after the UStruct stuff).
	while (HasIncompleteSerializationTasks())
	{
		FPlatformProcess::Sleep(0); // Yield.
	}
}

/* FSingleThreadRunnable interface
*****************************************************************************/

void FUdpMessageProcessor::Tick()
{
	CurrentTime = FDateTime::UtcNow();

	ConsumeInboundSegments();
	ConsumeOutboundMessages();
	UpdateKnownNodes();
}

/* FUdpMessageProcessor implementation
 *****************************************************************************/

void FUdpMessageProcessor::AcknowledgeReceipt(int32 MessageId, const FNodeInfo& NodeInfo)
{
	FUdpMessageSegment::FHeader Header;
	{
		Header.ProtocolVersion = NodeInfo.ProtocolVersion;
		Header.RecipientNodeId = NodeInfo.NodeId;
		Header.SenderNodeId = LocalNodeId;
		Header.SegmentType = EUdpMessageSegments::Acknowledge;
	}

	FUdpMessageSegment::FAcknowledgeChunk AcknowledgeChunk;
	{
		AcknowledgeChunk.MessageId = MessageId;
	}

	FArrayWriter Writer;
	{
		Writer << Header;
		AcknowledgeChunk.Serialize(Writer, NodeInfo.ProtocolVersion);
	}

	int32 OutSent;
	Socket->SendTo(Writer.GetData(), Writer.Num(), OutSent, *NodeInfo.Endpoint.ToInternetAddr());
	UE_LOG(LogUdpMessaging, Verbose, TEXT("Sending EUdpMessageSegments::Acknowledge for msg %d from %s"), MessageId, *NodeInfo.NodeId.ToString());
}


FTimespan FUdpMessageProcessor::CalculateWaitTime() const
{
	return FTimespan::FromMilliseconds(10);
}


void FUdpMessageProcessor::ConsumeInboundSegments()
{
	FInboundSegment Segment;

	while (InboundSegments.Dequeue(Segment))
	{
		// quick hack for TTP# 247103
		if (!Segment.Data.IsValid())
		{
			continue;
		}

		FUdpMessageSegment::FHeader Header;
		*Segment.Data << Header;

		if (FilterSegment(Header))
		{
			FNodeInfo& NodeInfo = KnownNodes.FindOrAdd(Header.SenderNodeId);

			if (!NodeInfo.NodeId.IsValid())
			{
				NodeInfo.NodeId = Header.SenderNodeId;
				NodeInfo.ProtocolVersion = Header.ProtocolVersion;
				NodeDiscoveredDelegate.ExecuteIfBound(NodeInfo.NodeId);
			}

			NodeInfo.ProtocolVersion = Header.ProtocolVersion;
			NodeInfo.Endpoint = Segment.Sender;
			NodeInfo.LastSegmentReceivedTime = CurrentTime;

			switch (Header.SegmentType)
			{
			case EUdpMessageSegments::Abort:
				ProcessAbortSegment(Segment, NodeInfo);
				break;

			case EUdpMessageSegments::Acknowledge:
				ProcessAcknowledgeSegment(Segment, NodeInfo);
				break;

			case EUdpMessageSegments::AcknowledgeSegments:
				ProcessAcknowledgeSegmentsSegment(Segment, NodeInfo);
				break;

			case EUdpMessageSegments::Bye:
				ProcessByeSegment(Segment, NodeInfo);
				break;

			case EUdpMessageSegments::Data:			
				ProcessDataSegment(Segment, NodeInfo);
				break;

			case EUdpMessageSegments::Hello:
				ProcessHelloSegment(Segment, NodeInfo);
				break;

			case EUdpMessageSegments::Ping:
				ProcessPingSegment(Segment, NodeInfo);
				break;

			case EUdpMessageSegments::Pong:
				ProcessPongSegment(Segment, NodeInfo);
				break;

			case EUdpMessageSegments::Retransmit:
				ProcessRetransmitSegment(Segment, NodeInfo);
				break;

			case EUdpMessageSegments::Timeout:
				ProcessTimeoutSegment(Segment, NodeInfo);
				break;

			default:
				ProcessUnknownSegment(Segment, NodeInfo, (uint8)Header.SegmentType);
			}
		}
	}
}


void FUdpMessageProcessor::ConsumeOutboundMessages()
{
	FOutboundMessage OutboundMessage;

	while (OutboundMessages.Dequeue(OutboundMessage))
	{
		++LastSentMessage;

		for (const FGuid& RecipientId : OutboundMessage.RecipientIds)
		{
			FNodeInfo* RecipientNodeInfo = KnownNodes.Find(RecipientId);
			// Queue segmenters to the nodes we are dispatching to
			if (RecipientNodeInfo)
			{
				UE_LOG(LogUdpMessaging, Verbose, TEXT("Passing %d byte message to be segement-sent to %s"), 
					OutboundMessage.SerializedMessage->TotalSize(), *RecipientNodeInfo->NodeId.ToString());

				RecipientNodeInfo->Segmenters.Add(
					LastSentMessage,
					MakeShared<FUdpMessageSegmenter>(OutboundMessage.SerializedMessage.ToSharedRef(), UDP_MESSAGING_SEGMENT_SIZE)
				);
			}
			else
			{
				UE_LOG(LogUdpMessaging, Verbose, TEXT("No recipient NodeInfo found for %s"), *RecipientId.ToString());
			}
		}
	}
}


bool FUdpMessageProcessor::FilterSegment(const FUdpMessageSegment::FHeader& Header)
{
	// filter locally generated segments
	if (Header.SenderNodeId == LocalNodeId)
	{
		return false;
	}

	// filter unsupported protocol versions
	if (!SupportedProtocolVersions.Contains(Header.ProtocolVersion))
	{
		return false;
	}

	return true;
}


void FUdpMessageProcessor::ProcessAbortSegment(FInboundSegment& Segment, FNodeInfo& NodeInfo)
{
	FUdpMessageSegment::FAbortChunk AbortChunk;
	AbortChunk.Serialize(*Segment.Data, NodeInfo.ProtocolVersion);

	NodeInfo.Segmenters.Remove(AbortChunk.MessageId);
}


void FUdpMessageProcessor::ProcessAcknowledgeSegment(FInboundSegment& Segment, FNodeInfo& NodeInfo)
{
	FUdpMessageSegment::FAcknowledgeChunk AcknowledgeChunk;
	AcknowledgeChunk.Serialize(*Segment.Data, NodeInfo.ProtocolVersion);

	NodeInfo.Segmenters.Remove(AcknowledgeChunk.MessageId);

	UE_LOG(LogUdpMessaging, Verbose, TEXT("Received Acknowledge for %d from %s"), AcknowledgeChunk.MessageId , *NodeInfo.NodeId.ToString());

}


void FUdpMessageProcessor::ProcessAcknowledgeSegmentsSegment(FInboundSegment& Segment, FNodeInfo& NodeInfo) // TODO: Rename function
{
	FUdpMessageSegment::FAcknowledgeSegmentsChunk AcknowledgeChunk;
	AcknowledgeChunk.Serialize(*Segment.Data, NodeInfo.ProtocolVersion);

	UE_LOG(LogUdpMessaging, Verbose, TEXT("Received AcknowledgeSegments for %d from %s"), AcknowledgeChunk.MessageId, *NodeInfo.NodeId.ToString());

	TSharedPtr<FUdpMessageSegmenter> Segmenter = NodeInfo.Segmenters.FindRef(AcknowledgeChunk.MessageId);
	if (Segmenter.IsValid())
	{
		Segmenter->MarkAsAcknowledged(AcknowledgeChunk.Segments);
		if (Segmenter->IsSendingComplete() && Segmenter->AreAcknowledgementsComplete())
		{
			UE_LOG(LogUdpMessaging, Verbose, TEXT("Segmenter for %s is now complete. Removing"), *NodeInfo.NodeId.ToString());
			NodeInfo.Segmenters.Remove(AcknowledgeChunk.MessageId);
		}
	}
	else
	{
		UE_LOG(LogUdpMessaging, Verbose, TEXT("No such segmenter for message %d"), AcknowledgeChunk.MessageId);
	}
}


void FUdpMessageProcessor::ProcessByeSegment(FInboundSegment& Segment, FNodeInfo& NodeInfo)
{
	FGuid RemoteNodeId;
	*Segment.Data << RemoteNodeId;

	if (RemoteNodeId.IsValid() && (RemoteNodeId == NodeInfo.NodeId))
	{
		RemoveKnownNode(RemoteNodeId);
	}
}


void FUdpMessageProcessor::ProcessDataSegment(FInboundSegment& Segment, FNodeInfo& NodeInfo)
{
	FUdpMessageSegment::FDataChunk DataChunk;
	DataChunk.Serialize(*Segment.Data, NodeInfo.ProtocolVersion);
	
	if (Segment.Data->IsError())
	{
		UE_LOG(LogUdpMessaging, Warning, TEXT("FUdpMessageProcessor::ProcessDataSegment: Failed to serialize DataChunk. Sender=%s"),
			*(Segment.Sender.ToString()));
		return;
	}

	// Discard late segments for sequenced messages
	if ((DataChunk.Sequence != 0) && (DataChunk.Sequence < NodeInfo.Resequencer.GetNextSequence()))
	{
		return;
	}
	TSharedPtr<FUdpReassembledMessage, ESPMode::ThreadSafe>& ReassembledMessage = NodeInfo.ReassembledMessages.FindOrAdd(DataChunk.MessageId);

	// Reassemble message
	if (!ReassembledMessage.IsValid())
	{
		ReassembledMessage = MakeShared<FUdpReassembledMessage, ESPMode::ThreadSafe>(NodeInfo.ProtocolVersion, DataChunk.MessageFlags, DataChunk.MessageSize, DataChunk.TotalSegments, DataChunk.Sequence, Segment.Sender);

		if (ReassembledMessage->IsMalformed())
		{
			// Go ahead and throw away the message.
			// The sender should see the NAK and resend, so we'll attempt to recreate it later.
			UE_LOG(LogUdpMessaging, Warning, TEXT("FUdpMessageProcessor::ProcessDataSegment: Ignoring malformed Message %s"), *(ReassembledMessage->Describe()));
			NodeInfo.ReassembledMessages.Remove(DataChunk.MessageId);
			ReassembledMessage.Reset();
			return;
		}
	}

	/**
	// TODO: In a future release uncomment these checks.
	//		Don't do this for 4.23, because there could be existing third party tools / producers that
	//		just send dummy data (they shouldn't!), and this could break them unexpectedly.
	//		These should probably also be moved into a shared location (like into FUdpReassembledMessage).

	if (ReassembledMessage->GetTotalSegmentsCount() != DataChunk.TotalSegments)
	{
		UE_LOG(LogUdpMessaging, Warning, TEXT("FUdpMessageProcessor::ProcessDataSegment: Ignoring segment with invalid TotalSegment count. Message=%s, ExpectedTotalSegments=%lu, InTotalSegments=%lu"),
			*ReassembledMessage->Describe(), ReassembledMessage->GetTotalSegmentCount(), DataChunk.TotalSegments);
		return;
	}
	if (ReassembledMessage->GetData().Num() != DataChunk.MessageSize)
	{
		UE_LOG(LogUdpMessaging, Warning, TEXT("FUdpMessageProcessor::ProcessDataSegment: Ignoring segment with invalid MessageSize. Message=%s, ExpectedMessageSize=%d, InMessageSize=%d"),
			*ReassembledMessage->Describe(), ReassembledMessage->GetData().Num(), DataChunk.MessageSize);
		return;
	}
	if (ReassembledMessage->GetSequence() != DataChunk.Sequence)
	{
		UE_LOG(LogUdpMessaging, Warning, TEXT("FUdpMessageProcessor::ProcessDataSegment: Ignoring segment with invalid Sequence. Message=%s, ExpectedSequence=%llu, InSequence=%llu"),
			*ReassembledMessage->Describe(), ReassembledMessaged->GetSequence(), DataChunk.Sequence);
		return;
	}

	// TODO: Check MessageFlags.
	// TODO: Check Sender.
	*/

	ReassembledMessage->Reassemble(DataChunk.SegmentNumber, DataChunk.SegmentOffset, DataChunk.Data, CurrentTime);

	// Deliver or re-sequence message
	if (!ReassembledMessage->IsComplete() || ReassembledMessage->IsDelivered())
	{
		return;
	}

	UE_LOG(LogUdpMessaging, Verbose, TEXT("FUdpMessageProcessor::ProcessDataSegment: Reassembled %d bytes message %s for %s (%s)"),
		ReassembledMessage->GetData().Num(), 
		*ReassembledMessage->Describe(), 
		*NodeInfo.NodeId.ToString(),
		*NodeInfo.Endpoint.ToString());

	AcknowledgeReceipt(DataChunk.MessageId, NodeInfo);
	DeliverMessage(ReassembledMessage, NodeInfo);
}


void FUdpMessageProcessor::ProcessHelloSegment(FInboundSegment& Segment, FNodeInfo& NodeInfo)
{
	FGuid RemoteNodeId;
	*Segment.Data << RemoteNodeId;

	if (RemoteNodeId.IsValid())
	{
		NodeInfo.ResetIfRestarted(RemoteNodeId);
	}
}

void FUdpMessageProcessor::ProcessPingSegment(FInboundSegment& Segment, FNodeInfo& NodeInfo)
{
	FGuid RemoteNodeId;
	*Segment.Data << RemoteNodeId;
	uint8 NodeProtocolVersion;
	*Segment.Data << NodeProtocolVersion;

	if (RemoteNodeId.IsValid())
	{
		NodeInfo.ResetIfRestarted(RemoteNodeId);
	}
	
	// The protocol version we are going to use to communicate to this node is the smallest between its version and our own
	uint8 ProtocolVersion = FMath::Min<uint8>(NodeProtocolVersion, UDP_MESSAGING_TRANSPORT_PROTOCOL_VERSION);

	// if that protocol isn't in our supported protocols we do not reply to the pong and remove this node since we don't support its version
	if (!SupportedProtocolVersions.Contains(ProtocolVersion))
	{
		RemoveKnownNode(NodeInfo.NodeId);
		return;
	}

	// Set this node protocol to our agreed protocol
	NodeInfo.ProtocolVersion = ProtocolVersion;

	// Send the pong
	FUdpMessageSegment::FHeader Header;
	{
		// Reply to the ping using the agreed protocol
		Header.ProtocolVersion = ProtocolVersion;
		Header.RecipientNodeId = NodeInfo.NodeId;
		Header.SenderNodeId = LocalNodeId;
		Header.SegmentType = EUdpMessageSegments::Pong;
	}

	FArrayWriter Writer;
	{
		Writer << Header;
		Writer << LocalNodeId;
	}

	int32 OutSent;
	Socket->SendTo(Writer.GetData(), Writer.Num(), OutSent, *NodeInfo.Endpoint.ToInternetAddr());
}


void FUdpMessageProcessor::ProcessPongSegment(FInboundSegment& Segment, FNodeInfo& NodeInfo)
{
	FGuid RemoteNodeId;
	*Segment.Data << RemoteNodeId;

	if (RemoteNodeId.IsValid())
	{
		NodeInfo.ResetIfRestarted(RemoteNodeId);
	}
}


void FUdpMessageProcessor::ProcessRetransmitSegment(FInboundSegment& Segment, FNodeInfo& NodeInfo)
{
	FUdpMessageSegment::FRetransmitChunk RetransmitChunk;
	RetransmitChunk.Serialize(*Segment.Data, NodeInfo.ProtocolVersion);

	TSharedPtr<FUdpMessageSegmenter> Segmenter = NodeInfo.Segmenters.FindRef(RetransmitChunk.MessageId);

	UE_LOG(LogUdpMessaging, Verbose, TEXT("Received retransmit for %d from %s"), RetransmitChunk.MessageId, *NodeInfo.NodeId.ToString());

	if (Segmenter.IsValid())
	{
		Segmenter->MarkForRetransmission(RetransmitChunk.Segments);
	}
	else
	{
		UE_LOG(LogUdpMessaging, Verbose, TEXT("No such segmenter for message %d"), RetransmitChunk.MessageId);
	}
}


void FUdpMessageProcessor::ProcessTimeoutSegment(FInboundSegment& Segment, FNodeInfo& NodeInfo)
{
	FUdpMessageSegment::FTimeoutChunk TimeoutChunk;
	TimeoutChunk.Serialize(*Segment.Data, NodeInfo.ProtocolVersion);

	TSharedPtr<FUdpMessageSegmenter> Segmenter = NodeInfo.Segmenters.FindRef(TimeoutChunk.MessageId);

	if (Segmenter.IsValid())
	{
		Segmenter->MarkForRetransmission();
	}
}


void FUdpMessageProcessor::ProcessUnknownSegment(FInboundSegment& Segment, FNodeInfo& EndpointInfo, uint8 SegmentType)
{
	UE_LOG(LogUdpMessaging, Verbose, TEXT("Received unknown segment type '%i' from %s"), SegmentType, *Segment.Sender.ToText().ToString());
}


void FUdpMessageProcessor::DeliverMessage(const TSharedPtr<FUdpReassembledMessage, ESPMode::ThreadSafe>& ReassembledMessage, FNodeInfo& NodeInfo)
{
	// Do not deliver message while saving or garbage collecting since those deliveries will fail anyway...
	if (GIsSavingPackage || IsGarbageCollecting())
	{
		UE_LOG(LogUdpMessaging, Verbose, TEXT("Skipping delivery of %s"), *ReassembledMessage->Describe());
		return;
	}

	if (ReassembledMessage->GetSequence() == 0)
	{
		if (NodeInfo.NodeId.IsValid())
		{
			MessageReassembledDelegate.ExecuteIfBound(*ReassembledMessage, nullptr, NodeInfo.NodeId);
		}
	}
	else if (NodeInfo.Resequencer.Resequence(ReassembledMessage))
	{
		TSharedPtr<FUdpReassembledMessage, ESPMode::ThreadSafe> ResequencedMessage;

		while (NodeInfo.Resequencer.Pop(ResequencedMessage))
		{
			if (NodeInfo.NodeId.IsValid())
			{
				MessageReassembledDelegate.ExecuteIfBound(*ResequencedMessage, nullptr, NodeInfo.NodeId);
			}
		}
	}
	// Mark the message delivered but do not remove it from the list yet, this is to prevent the double delivery of reliable message
	ReassembledMessage->MarkDelivered();
}


void FUdpMessageProcessor::RemoveKnownNode(const FGuid& NodeId)
{
	NodeLostDelegate.ExecuteIfBound(NodeId);
	KnownNodes.Remove(NodeId);
}


void FUdpMessageProcessor::UpdateKnownNodes()
{
	// Estimated max send bytes per seconds
	const uint32 MaxSendRate = 125000000;

	// Remove dead nodes
	FTimespan DeadHelloTimespan = DeadHelloIntervals * Beacon->GetBeaconInterval();
	for (auto It = KnownNodes.CreateIterator(); It; ++It)
	{
		FGuid& NodeId = It->Key;
		FNodeInfo& NodeInfo = It->Value;

		if ((NodeId.IsValid()) && ((NodeInfo.LastSegmentReceivedTime + DeadHelloTimespan) <= CurrentTime))
		{
			UE_LOG(LogUdpMessaging, Verbose, TEXT("FUdpMessageProcessor::UpdateKnownNodes: Removing Node %s (%s)"), *NodeInfo.NodeId.ToString(), *NodeInfo.Endpoint.ToString());
			NodeLostDelegate.ExecuteIfBound(NodeId);
			It.RemoveCurrent();
		}
	}

	UpdateNodesPerVersion();
	Beacon->SetEndpointCount(KnownNodes.Num() + 1);

	if (KnownNodes.Num() == 0)
	{
		return;
	}

	bool bSuccess = true;
	double DeltaSeconds = DeltaTime.GetTotalSeconds();
	const int32 MaxSendRateDelta = MaxSendRate * DeltaSeconds;

	// Calculate the number of byte allowed per node for this tick
	int32 MaxNodeByteSend = (MaxSendRateDelta) / KnownNodes.Num();

	int32 AllByteSent = 0;
	bool bSendPending = true;
	while (bSendPending 
		&& AllByteSent < MaxSendRateDelta
		&& bSuccess)
	{
		bSendPending = false;

		for (auto& KnownNodePair : KnownNodes)
		{
			int32 NodeByteSent = UpdateSegmenters(KnownNodePair.Value, MaxNodeByteSend);
			// if NodByteSent is negative, there is a socket error, continuing is useless
			bSuccess = NodeByteSent >= 0;
			if (!bSuccess)
			{
				break;
			}
			// if NodeByteSent is higher than the allotted number of bytes for the node, queue another round of sending once all node had a go
			bSendPending |= NodeByteSent > MaxNodeByteSend;
			AllByteSent += NodeByteSent;
		}
	}

	for (auto& KnownNodePair : KnownNodes)
	{
		bSuccess = UpdateReassemblers(KnownNodePair.Value);
		// if there is a socket error, continuing is useless
		if (!bSuccess)
		{
			break;
		}
	}

	// if we had socket error, fire up the error delegate
	if (!bSuccess || Beacon->HasSocketError())
	{
		ErrorDelegate.ExecuteIfBound();
	}
}

int32 FUdpMessageProcessor::UpdateSegmenters(FNodeInfo& NodeInfo, uint32 MaxSendBytes)
{

	FUdpMessageSegment::FHeader Header
	{
		NodeInfo.ProtocolVersion,		// Header.ProtocolVersion - Send data segment using the node protocol version
		NodeInfo.NodeId,				// Header.RecipientNodeId
		LocalNodeId,					// Header.SenderNodeId
		EUdpMessageSegments::Data		// Header.SegmentType
	};

	uint32 ByteSent = 0;
	for (TMap<int32, TSharedPtr<FUdpMessageSegmenter> >::TIterator It(NodeInfo.Segmenters); It; ++It)
	{
		TSharedPtr<FUdpMessageSegmenter>& Segmenter = It.Value();

		Segmenter->Initialize();

		if (Segmenter->IsInitialized() && Segmenter->NeedSending(CurrentTime))
		{
			FUdpMessageSegment::FDataChunk DataChunk;

			// Track the segments we sent as we'll update the segmenter to keep track
			TArray<uint32> SentSegments;
			SentSegments.Reserve(Segmenter->GetSegmentCount());

			for (TConstSetBitIterator<> BIt(Segmenter->GetPendingSendSegments()); BIt; ++BIt)
			{
				Segmenter->GetPendingSegment(BIt.GetIndex(), DataChunk.Data);
				DataChunk.SegmentNumber = BIt.GetIndex();

				DataChunk.MessageId = It.Key();
				DataChunk.MessageFlags = Segmenter->GetMessageFlags();
				DataChunk.MessageSize = Segmenter->GetMessageSize();
				DataChunk.SegmentOffset = UDP_MESSAGING_SEGMENT_SIZE * DataChunk.SegmentNumber;
				DataChunk.Sequence = 0; // @todo gmp: implement message sequencing
				DataChunk.TotalSegments = Segmenter->GetSegmentCount();

				// validate with are sending message in the proper protocol version
				check(Header.ProtocolVersion == Segmenter->GetProtocolVersion());

				TSharedRef<FArrayWriter, ESPMode::ThreadSafe> Writer = MakeShared<FArrayWriter, ESPMode::ThreadSafe>();
				{
					*Writer << Header;
					DataChunk.Serialize(*Writer, Header.ProtocolVersion);
				}

				ByteSent += Writer->Num();

				UE_LOG(LogUdpMessaging, Verbose, TEXT("FUdpMessageProcessor::UpdateSegmenters: Sending msg %d as segment %d/%d of %d bytes to %s"),
					DataChunk.MessageId, 
					DataChunk.SegmentNumber + 1, 
					DataChunk.TotalSegments, 
					Segmenter->GetMessageSize(), 
					*NodeInfo.NodeId.ToString());

				if (!SocketSender->Send(Writer, NodeInfo.Endpoint))
				{
					UE_LOG(LogUdpMessaging, Error, TEXT("FUdpMessageProcessor::UpdateSegmenters: Error sending message segment %d/%d of %d bytes to %s"),
						DataChunk.SegmentNumber + 1, DataChunk.TotalSegments, Segmenter->GetMessageSize(), *NodeInfo.NodeId.ToString());
					return -1;
 				}

				// Track this segment
				SentSegments.Push(BIt.GetIndex());

				// if we reached the max send rate, break
				if (ByteSent >= MaxSendBytes)
				{
					UE_LOG(LogUdpMessaging, Verbose, TEXT("FUdpMessageProcessor::UpdateSegmenters: Reached max BytesSent (%d of %d) on message segment %d/%d to %s"),
						ByteSent, MaxSendBytes,
						DataChunk.SegmentNumber + 1,
						DataChunk.TotalSegments,
						*NodeInfo.NodeId.ToString());
					break;
				}
			}


			// Mark those segments as sent. This removes them from the pending list
			Segmenter->MarkAsSent(SentSegments);

			// Did this pass complete everything?
			if (Segmenter->IsSendingComplete())
			{
				// update sent time for reliable messages
				if (EnumHasAnyFlags(Segmenter->GetMessageFlags(), EMessageFlags::Reliable))
				{
					Segmenter->UpdateSentTime(CurrentTime);
				}
				else
				{
					UE_LOG(LogUdpMessaging, Verbose, TEXT("FUdpMessageProcessor::UpdateSegmenters: Finished with message segmenter for %s"), *NodeInfo.NodeId.ToString());
					It.RemoveCurrent();
				}
			}
			else
			{
				// Still work to do that will be picked up next tick
				UE_LOG(LogUdpMessaging, Verbose, TEXT("FUdpMessageProcessor::UpdateSegmenters: Will continue processing segments for msg %d from %s on next tick"), DataChunk.MessageId , *NodeInfo.NodeId.ToString());
			}
		}
		else if (Segmenter->IsInvalid())
		{
			It.RemoveCurrent();
		}

		// if we reach the max send rate, break
		if (ByteSent >= MaxSendBytes)
		{
			break;
		}
	}
	return ByteSent;
}


const FTimespan FUdpMessageProcessor::StaleReassemblyInterval = FTimespan::FromSeconds(30);

bool FUdpMessageProcessor::UpdateReassemblers(FNodeInfo& NodeInfo)
{
	FUdpMessageSegment::FHeader Header
	{
		FMath::Max(NodeInfo.ProtocolVersion, (uint8)11),	// Header.ProtocolVersion, AcknowledgeSegments are version 11 and onward segment
		NodeInfo.NodeId,									// Header.RecipientNodeId
		LocalNodeId,										// Header.SenderNodeId
		EUdpMessageSegments::AcknowledgeSegments			// Header.SegmentType
	};

	for (TMap<int32, TSharedPtr<FUdpReassembledMessage, ESPMode::ThreadSafe>>::TIterator It(NodeInfo.ReassembledMessages); It; ++It)
	{
		TSharedPtr<FUdpReassembledMessage, ESPMode::ThreadSafe>& ReassembledMessage = It.Value();

		// Send pending acknowledgments
		if (ReassembledMessage->HasPendingAcknowledgements())
		{
			TArray<uint32> PendingAcknowledgments = ReassembledMessage->GetPendingAcknowledgments();
			const int32 AckCount = PendingAcknowledgments.Num();

			FUdpMessageSegment::FAcknowledgeSegmentsChunk AcknowledgeChunk(It.Key()/*MessageId*/, MoveTemp(PendingAcknowledgments)/*Segments*/);
			TSharedRef<FArrayWriter, ESPMode::ThreadSafe> Writer = MakeShared<FArrayWriter, ESPMode::ThreadSafe>();
			{
				*Writer << Header;
				AcknowledgeChunk.Serialize(*Writer, Header.ProtocolVersion);				
			}

			UE_LOG(LogUdpMessaging, Verbose, TEXT("FUdpMessageProcessor::UpdateReassemblers: Sending EUdpMessageSegments::AcknowledgeSegments for %d segments for message %d from %s"), AckCount, It.Key(), *ReassembledMessage->Describe());

			if (!SocketSender->Send(Writer, NodeInfo.Endpoint))
			{
				UE_LOG(LogUdpMessaging, Error, TEXT("FUdpMessageProcessor::UpdateReassemblers: error sending EUdpMessageSegments::AcknowledgeSegments from %s"), *NodeInfo.NodeId.ToString());

				return false;
			}
			
			UE_LOG(LogUdpMessaging, Verbose, TEXT("FUdpMessageProcessor::UpdateReassemblers: sending acknowledgement for reliable msg %d from %s"), 
				It.Key(), 
				*NodeInfo.NodeId.ToString());			
		}

		// Try to deliver completed message that couldn't be delivered the first time around
		if (ReassembledMessage->IsComplete() && !ReassembledMessage->IsDelivered())
		{
			DeliverMessage(ReassembledMessage, NodeInfo);
		}

		// Remove stale reassembled message if they aren't reliable or are marked delivered
		if (ReassembledMessage->GetLastSegmentTime() + StaleReassemblyInterval <= CurrentTime &&
			(!EnumHasAnyFlags(ReassembledMessage->GetFlags(), EMessageFlags::Reliable) || ReassembledMessage->IsDelivered()))
		{
			if (!ReassembledMessage->IsDelivered())
			{ 
				const int ReceivedSegments = ReassembledMessage->GetTotalSegmentsCount() - ReassembledMessage->GetPendingSegmentsCount();
				UE_LOG(LogUdpMessaging, Warning, TEXT("FUdpMessageProcessor::UpdateReassemblers Discarding %d/%d of stale message segements from %s"), 
					ReceivedSegments,
					ReassembledMessage->GetTotalSegmentsCount(),
					*ReassembledMessage->Describe());
			}
			It.RemoveCurrent();
		}
	}
	return true;
}


void FUdpMessageProcessor::UpdateNodesPerVersion()
{
	FScopeLock NodeVersionLock(&NodeVersionCS);
	NodeVersions.Empty();
	for (auto& NodePair : KnownNodes)
	{
		NodeVersions.Add(NodePair.Key, NodePair.Value.ProtocolVersion);
	}
}
