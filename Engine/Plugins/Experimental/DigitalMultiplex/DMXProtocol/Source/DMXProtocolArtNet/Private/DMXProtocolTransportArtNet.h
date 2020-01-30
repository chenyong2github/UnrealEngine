// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "Interfaces/IDMXProtocolTransport.h"
#include "DMXProtocolTypes.h"

class FSocket;
class ISocketSubsystem;
class FDMXProtocolArtNet;

class DMXPROTOCOLARTNET_API FDMXProtocolSenderArtNet
	: public IDMXProtocolSender
{

public:
	FDMXProtocolSenderArtNet(FSocket& InSocket, FDMXProtocolArtNet* InProtocol);
	virtual ~FDMXProtocolSenderArtNet();

	//~ Begin FRunnable implementation
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;
	//~ End FRunnable implementation

	//~ Begin FSingleThreadRunnable implementation
	virtual void Tick() override;
	virtual class FSingleThreadRunnable* GetSingleThreadInterface() override;
	//~ End FSingleThreadRunnable implementation

	//~ Begin IDMXProtocolSender implementation
	virtual bool EnqueueOutboundPackage(FDMXPacketPtr Packet) override;
	//~ End IDMXProtocolSender implementation

public:
	/** Consumes all outbound packages. */
	void ConsumeOutboundPackages();


private:
	/** Holds the queue of outbound packages. */
	TQueue<FDMXPacketPtr, EQueueMode::Mpsc> OutboundPackages;

	/** Holds the last sent message number. */
	int32 LastSentPackage;

	mutable FCriticalSection CriticalSection;

	FThreadSafeCounter StopTaskCounter;

	/** Holds the thread object. */
	FRunnableThread* Thread;

	/** Holds an event signaling that inbound messages need to be processed. */
	TSharedPtr<FEvent, ESPMode::ThreadSafe> WorkEvent;

	FThreadSafeBool bRequestingExit;

	/** Holds the network socket used to sender packages. */
	FSocket* BroadcastSocket;

	FDMXProtocolArtNet* Protocol;
};


class FDMXProtocolReceiverArtNet
	: public IDMXProtocolReceiver
{
public:
	FDMXProtocolReceiverArtNet(FSocket& InSocket, FDMXProtocolArtNet* InProtocol, const FTimespan& InWaitTime);
	virtual ~FDMXProtocolReceiverArtNet();

public:
	//~ Begin FRunnable implementation
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;
	//~ End FRunnable implementation

	//~ Begin FSingleThreadRunnable implementation
	virtual void Tick() override;
	virtual class FSingleThreadRunnable* GetSingleThreadInterface() override;
	//~ End FSingleThreadRunnable implementation

	//~ Begin IDMXProtocolReceiver implementation
	virtual FOnDMXDataReceived& OnDataReceived() override;
	virtual FRunnableThread* GetThread() const override;
	//~ End IDMXProtocolReceiver implementation

protected:
	void Update(const FTimespan& SocketWaitTime);

private:

	/** The network socket. */
	FSocket* Socket;

	/** Pointer to the socket sub-system. */
	ISocketSubsystem* SocketSubsystem;

	/** Flag indicating that the thread is stopping. */
	bool Stopping;

	/** The thread object. */
	FRunnableThread* Thread;

	/** The receiver thread's name. */
	FString ThreadName;

	/** The amount of time to wait for inbound packets. */
	FTimespan WaitTime;

private:

	/** Holds the data received delegate. */
	FOnDMXDataReceived DMXDataReceiveDelegate;
};