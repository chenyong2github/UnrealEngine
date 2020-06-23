// Copyright Epic Games, Inc. All Rights Reserved.

#include "Transport/UdpMessageTransport.h"
#include "UdpMessagingPrivate.h"

#include "Common/UdpSocketBuilder.h"
#include "Common/UdpSocketReceiver.h"
#include "HAL/RunnableThread.h"
#include "IMessageContext.h"
#include "IMessageTransportHandler.h"
#include "Misc/Guid.h"
#include "Serialization/ArrayReader.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "Async/Async.h"

#include "Transport/UdpReassembledMessage.h"
#include "Transport/UdpDeserializedMessage.h"
#include "Transport/UdpSerializedMessage.h"
#include "Transport/UdpMessageProcessor.h"


/* FUdpMessageTransport structors
 *****************************************************************************/

FUdpMessageTransport::FUdpMessageTransport(const FIPv4Endpoint& InUnicastEndpoint, const FIPv4Endpoint& InMulticastEndpoint, TArray<FIPv4Endpoint> InStaticEndpoints, uint8 InMulticastTtl)
	: MessageProcessor(nullptr)
	, MessageProcessorThread(nullptr)
	, MulticastEndpoint(InMulticastEndpoint)
	, MulticastReceiver(nullptr)
	, MulticastSocket(nullptr)
	, MulticastTtl(InMulticastTtl)
	, SocketSubsystem(ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
	, TransportHandler(nullptr)
	, UnicastEndpoint(InUnicastEndpoint)
#if PLATFORM_DESKTOP
	, UnicastReceiver(nullptr)
	, UnicastSocket(nullptr)
#endif
	, StaticEndpoints(MoveTemp(InStaticEndpoints))
{
}


FUdpMessageTransport::~FUdpMessageTransport()
{
	StopTransport();
}

void FUdpMessageTransport::OnAppPreExit()
{
	if (MessageProcessor)
	{		
		MessageProcessor->WaitAsyncTaskCompletion();
	}
}

void FUdpMessageTransport::AddStaticEndpoint(const FIPv4Endpoint& InEndpoint)
{
	bool bAlreadyInSet = false;
	StaticEndpoints.Add(InEndpoint, &bAlreadyInSet);
	if (MessageProcessor && !bAlreadyInSet)
	{
		MessageProcessor->AddStaticEndpoint(InEndpoint);
	}
}


void FUdpMessageTransport::RemoveStaticEndpoint(const FIPv4Endpoint& InEndpoint)
{
	int32 NbRemoved = StaticEndpoints.Remove(InEndpoint);
	if (MessageProcessor && NbRemoved > 0)
	{
		MessageProcessor->RemoveStaticEndpoint(InEndpoint);
	}
}

bool FUdpMessageTransport::RestartTransport()
{
	IMessageTransportHandler* Handler = TransportHandler;
	StopTransport();
	return StartTransport(*Handler);
}

/* IMessageTransport interface
 *****************************************************************************/

FName FUdpMessageTransport::GetDebugName() const
{
	return "UdpMessageTransport";
}


bool FUdpMessageTransport::StartTransport(IMessageTransportHandler& Handler)
{
	// Set the handler even if initialization fails. This allows tries for reinitialization using the same handler.
	TransportHandler = &Handler;

#if PLATFORM_DESKTOP
	// create & initialize unicast socket (only on multi-process platforms)
	UnicastSocket = FUdpSocketBuilder(TEXT("UdpMessageUnicastSocket"))
		.AsNonBlocking()
		.BoundToEndpoint(UnicastEndpoint)
		.WithMulticastLoopback()
		.WithMulticastTtl(MulticastTtl) // since this socket is also used to send to multicast addresses
		.WithReceiveBufferSize(UDP_MESSAGING_RECEIVE_BUFFER_SIZE);

	if (UnicastSocket == nullptr)
	{
		UE_LOG(LogUdpMessaging, Error, TEXT("StartTransport failed to create unicast socket on %s"), *UnicastEndpoint.ToString());

		return false;
	}
#endif

	// create & initialize multicast socket (optional)
	MulticastSocket = FUdpSocketBuilder(TEXT("UdpMessageMulticastSocket"))
		.AsNonBlocking()
		.AsReusable()
#if PLATFORM_WINDOWS
		// If multiple bus instances bind the same unicast ip:port combination (allowed as the socket is marked as reusable), 
		// then for each packet sent to that ip:port combination, only one of the instances (at the discretion of the OS) will receive it. 
		// The instance that receives the packet may vary over time, seemingly based on the congestion of its socket.
		// This isn't the intended usage.
		// To allow traffic to be sent directly to unicast for discovery, set the interface and port for the unicast endpoint
		// However for legacy reason, keep binding this as well, although it might be unreliable in some cases
		.BoundToAddress(UnicastEndpoint.Address)
#endif
		.BoundToPort(MulticastEndpoint.Port)
#if PLATFORM_SUPPORTS_UDP_MULTICAST_GROUP
		.JoinedToGroup(MulticastEndpoint.Address, UnicastEndpoint.Address)
		.WithMulticastLoopback()
		.WithMulticastTtl(MulticastTtl)
		.WithMulticastInterface(UnicastEndpoint.Address)
#endif
		.WithReceiveBufferSize(UDP_MESSAGING_RECEIVE_BUFFER_SIZE);

	if (MulticastSocket == nullptr)
	{
		UE_LOG(LogUdpMessaging, Warning, TEXT("StartTransport failed to create multicast socket on %s, joined to %s with TTL %i"), *UnicastEndpoint.ToString(), *MulticastEndpoint.ToString(), MulticastTtl);

#if !PLATFORM_DESKTOP
		return false;
#endif
	}

	// initialize threads
	FTimespan ThreadWaitTime = FTimespan::FromMilliseconds(100);

#if PLATFORM_DESKTOP
	MessageProcessor = new FUdpMessageProcessor(*UnicastSocket, FGuid::NewGuid(), MulticastEndpoint);
#else
	MessageProcessor = new FUdpMessageProcessor(*MulticastSocket, FGuid::NewGuid(), MulticastEndpoint);
#endif
	{
		// Add the static endpoints
		for (const FIPv4Endpoint& Endpoint : StaticEndpoints)
		{
			MessageProcessor->AddStaticEndpoint(Endpoint);
		}

		MessageProcessor->OnMessageReassembled().BindRaw(this, &FUdpMessageTransport::HandleProcessorMessageReassembled);
		MessageProcessor->OnNodeDiscovered().BindRaw(this, &FUdpMessageTransport::HandleProcessorNodeDiscovered);
		MessageProcessor->OnNodeLost().BindRaw(this, &FUdpMessageTransport::HandleProcessorNodeLost);
		MessageProcessor->OnError().BindRaw(this, &FUdpMessageTransport::HandleProcessorError);
	}

	if (MulticastSocket != nullptr)
	{
		MulticastReceiver = new FUdpSocketReceiver(MulticastSocket, ThreadWaitTime, TEXT("UdpMessageMulticastReceiver"));
		MulticastReceiver->OnDataReceived().BindRaw(this, &FUdpMessageTransport::HandleSocketDataReceived);
		MulticastReceiver->SetMaxReadBufferSize(2048);
		MulticastReceiver->Start();
	}

#if PLATFORM_DESKTOP
	UnicastReceiver = new FUdpSocketReceiver(UnicastSocket, ThreadWaitTime, TEXT("UdpMessageUnicastReceiver"));
	{
		UnicastReceiver->OnDataReceived().BindRaw(this, &FUdpMessageTransport::HandleSocketDataReceived);
		UnicastReceiver->SetMaxReadBufferSize(2048);
		UnicastReceiver->Start();
	}
#endif

	return true;
}


void FUdpMessageTransport::StopTransport()
{
	// shut down threads
	delete MulticastReceiver;
	MulticastReceiver = nullptr;

#if PLATFORM_DESKTOP
	delete UnicastReceiver;
	UnicastReceiver = nullptr;
#endif

	delete MessageProcessor;
	MessageProcessor = nullptr;

	// destroy sockets
	if (MulticastSocket != nullptr)
	{
		SocketSubsystem->DestroySocket(MulticastSocket);
		MulticastSocket = nullptr;
	}
	
#if PLATFORM_DESKTOP
	if (UnicastSocket != nullptr)
	{
		SocketSubsystem->DestroySocket(UnicastSocket);
		UnicastSocket = nullptr;
	}
#endif

	TransportHandler = nullptr;
	ErrorFuture.Reset();
}


bool FUdpMessageTransport::TransportMessage(const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context, const TArray<FGuid>& Recipients)
{
	if (MessageProcessor == nullptr)
	{
		return false;
	}

	if (Context->GetRecipients().Num() > UDP_MESSAGING_MAX_RECIPIENTS)
	{
		return false;
	}

	return MessageProcessor->EnqueueOutboundMessage(Context, Recipients);
}


/* FUdpMessageTransport event handlers
 *****************************************************************************/

void FUdpMessageTransport::HandleProcessorMessageReassembled(const FUdpReassembledMessage& ReassembledMessage, const TSharedPtr<IMessageAttachment, ESPMode::ThreadSafe>& Attachment, const FGuid& NodeId)
{
	// @todo gmp: move message deserialization into an async task
	TSharedRef<FUdpDeserializedMessage, ESPMode::ThreadSafe> DeserializedMessage = MakeShared<FUdpDeserializedMessage, ESPMode::ThreadSafe>(Attachment);

	if (DeserializedMessage->Deserialize(ReassembledMessage))
	{
		TransportHandler->ReceiveTransportMessage(DeserializedMessage, NodeId);
	}
}


void FUdpMessageTransport::HandleProcessorNodeDiscovered(const FGuid& DiscoveredNodeId)
{
	TransportHandler->DiscoverTransportNode(DiscoveredNodeId);
}


void FUdpMessageTransport::HandleProcessorNodeLost(const FGuid& LostNodeId)
{
	TransportHandler->ForgetTransportNode(LostNodeId);
}


void FUdpMessageTransport::HandleProcessorError()
{
	if (!ErrorFuture.IsValid() && TransportErrorDelegate.IsBound())
	{
		ErrorFuture = Async(EAsyncExecution::TaskGraphMainThread, [ErrorDelegate = TransportErrorDelegate]()
		{
			ErrorDelegate.ExecuteIfBound();
		});
	}
}


void FUdpMessageTransport::HandleSocketDataReceived(const TSharedPtr<FArrayReader, ESPMode::ThreadSafe>& Data, const FIPv4Endpoint& Sender)
{
	if (MessageProcessor != nullptr)
	{
		MessageProcessor->EnqueueInboundSegment(Data, Sender);
	}
}
