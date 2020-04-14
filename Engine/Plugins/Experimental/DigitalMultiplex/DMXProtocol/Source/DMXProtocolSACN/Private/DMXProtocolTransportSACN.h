// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DMXProtocolCommon.h"
#include "Interfaces/IDMXProtocolTransport.h"

#include "Containers/Queue.h"

class FSocket;
class ISocketSubsystem;
class FDMXProtocolSACN;

class DMXPROTOCOLSACN_API FDMXProtocolSenderSACN
	: public IDMXProtocolSender
{

public:
	FDMXProtocolSenderSACN(FSocket& InSocket, FDMXProtocolSACN* InProtocol);
	virtual ~FDMXProtocolSenderSACN();

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

	FThreadSafeCounter StopTaskCounter;

	/** Holds the thread object. */
	FRunnableThread* Thread;

	/** Holds an event signaling that inbound messages need to be processed. */
	TSharedPtr<FEvent, ESPMode::ThreadSafe> WorkEvent;

	FThreadSafeBool bRequestingExit;

	/** Holds the network socket used to sender packages. */
	FSocket* BroadcastSocket;

	FDMXProtocolSACN* Protocol;

	ISocketSubsystem* SocketSubsystem;
};
