// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "IMessageTransport.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "IMessageAttachment.h"
#include "Templates/SharedPointer.h"
#include "Async/Future.h"

class FArrayReader;
class FUdpReassembledMessage;
class FRunnableThread;
class FSocket;
class FUdpMessageProcessor;
class FUdpSocketReceiver;
class IMessageAttachment;
class IMessageContext;
class IMessageTransportHandler;
class ISocketSubsystem;

struct FGuid;


/**
 * Implements a message transport technology using an UDP network connection.
 *
 * On platforms that support multiple processes, the transport is using two sockets,
 * one for per-process unicast sending/receiving, and one for multicast receiving.
 * Console and mobile platforms use a single multicast socket for all sending and receiving.
 */
class FUdpMessageTransport
	: public TSharedFromThis<FUdpMessageTransport, ESPMode::ThreadSafe>
	, public IMessageTransport
{
public:

	/**
	 * Creates and initializes a new instance.
	 *
	 * @param InLocalEndpoint The local IP endpoint to receive messages on.
	 * @param InMulticastEndpoint The multicast group endpoint to transport messages to.
	 * @param InMulticastTtl The multicast time-to-live.
	 */
	FUdpMessageTransport(const FIPv4Endpoint& InLocalEndpoint, const FIPv4Endpoint& InMulticastEndpoint, TArray<FIPv4Endpoint> InStaticEndpoints, uint8 InMulticastTtl);

	/** Virtual destructor. */
	virtual ~FUdpMessageTransport();

	/** Notifies the transport that the application is about to exit. */
	void OnAppPreExit();

public:
	/**
	 * Add a static endpoint to the transport
	 * @param InEndpoint the endpoint to add
	 */
	void AddStaticEndpoint(const FIPv4Endpoint& InEndpoint);

	/**
	 * Remove a static endpoint from the transport
	 * @param InEndpoint the endpoint to remove
	 */
	void RemoveStaticEndpoint(const FIPv4Endpoint& InEndpoint);

	/**
	 * Returns a delegate that is executed when a socket error happened.
	 *
	 * @return The delegate.
	 * @note this delegate is executed from the main thread by the task graph.
	 */
	DECLARE_DELEGATE(FOnError)
	FOnError& OnTransportError()
	{
		return TransportErrorDelegate;
	}

	/**
	 * Restart the transport using the same Transport handler
	 * @return true if the restart was successful
	 */
	bool RestartTransport();

	//~ IMessageTransport interface
	virtual FName GetDebugName() const override;
	virtual bool StartTransport(IMessageTransportHandler& Handler) override;
	virtual void StopTransport() override;
	virtual bool TransportMessage(const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context, const TArray<FGuid>& Recipients) override;

private:

	/** Handles received transport messages. */
	void HandleProcessorMessageReassembled(const FUdpReassembledMessage& ReassembledMessage, const TSharedPtr<IMessageAttachment, ESPMode::ThreadSafe>& Attachment, const FGuid& NodeId);

	/** Handles discovered transport endpoints. */
	void HandleProcessorNodeDiscovered(const FGuid& DiscoveredNodeId);

	/** Handles lost transport endpoints. */
	void HandleProcessorNodeLost(const FGuid& LostNodeId);

	/** */
	void HandleProcessorError();

	/** Handles received socket data. */
	void HandleSocketDataReceived(const TSharedPtr<FArrayReader, ESPMode::ThreadSafe>& Data, const FIPv4Endpoint& Sender);

private:
	/** Holds any pending restart request. */
	TFuture<void> ErrorFuture;

	/** Holds the transport error delegate. */
	FOnError TransportErrorDelegate;

	/** Holds the message processor. */
	FUdpMessageProcessor* MessageProcessor;

	/** Holds the message processor thread. */
	FRunnableThread* MessageProcessorThread;

	/** Holds the multicast endpoint. */
	FIPv4Endpoint MulticastEndpoint;

	/** Holds the multicast socket receiver. */
	FUdpSocketReceiver* MulticastReceiver;

	/** Holds the multicast socket. */
	FSocket* MulticastSocket;

	/** Holds the multicast time to live. */
	uint8 MulticastTtl;

	/** Holds a pointer to the socket sub-system. */
	ISocketSubsystem* SocketSubsystem;

	/** Message transport handler. */
	IMessageTransportHandler* TransportHandler;

	/** Holds the local endpoint to receive messages on. */
	FIPv4Endpoint UnicastEndpoint;

#if PLATFORM_DESKTOP
	/** Holds the unicast socket receiver. */
	FUdpSocketReceiver* UnicastReceiver;

	/** Holds the unicast socket. */
	FSocket* UnicastSocket;
#endif

	/** Holds the static endpoints. */
	TSet<FIPv4Endpoint> StaticEndpoints;
};
