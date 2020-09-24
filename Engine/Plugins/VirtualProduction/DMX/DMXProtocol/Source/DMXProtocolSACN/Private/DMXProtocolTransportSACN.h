// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DMXProtocolCommon.h"
#include "Interfaces/IDMXProtocolTransport.h"

#include "HAL/CriticalSection.h"

class FSocket;
class ISocketSubsystem;
class FDMXProtocolSACN;
class FInternetAddr;
class FDMXProtocolUniverseSACN;

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
	/** Holds the map of outbound packages. It takes last changes for same universe ID */
	TMap<uint32, FDMXPacketPtr> OutboundPackages;

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

	/** Socket subsystem for internet address */
	ISocketSubsystem* SocketSubsystem;

	/** Internet address to send requests to*/
	TSharedPtr<FInternetAddr> InternetAddr;

	FCriticalSection PacketCS;
};

class FDMXProtocolReceiverSACN
	: public IDMXProtocolReceiver
{
public:
	FDMXProtocolReceiverSACN(FDMXProtocolSACN* InProtocol, int32 InReceivingRefreshRate);
	virtual ~FDMXProtocolReceiverSACN();

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
	virtual FOnDMXDataReceived& OnDataReceived() override { return DMXDataReceiveDelegate; }
	virtual FRunnableThread* GetThread() const override;
	//~ End IDMXProtocolReceiver implementation

	FCriticalSection& GetListeningUniversesLock() const { return ListeningUniversesLock; }

protected:
	void Update();

private:
	FDMXProtocolSACN* Protocol;

	/** Flag indicating that the thread is stopping. */
	bool Stopping;

	/** The thread object. */
	FRunnableThread* Thread;

	/** The receiver thread's name. */
	FString ThreadName;

private:
	mutable FCriticalSection ListeningUniversesLock;

	/** Keep the universes in the separate copy in order to solve the Lock issue on a thread loop */
	TMap<uint32, TSharedPtr<FDMXProtocolUniverseSACN, ESPMode::ThreadSafe>> UniversesCopy;

	int32 UniversesNumCached;

	int32 ReceivingRefreshRate;

	/** Holds the data received delegate. */
	FOnDMXDataReceived DMXDataReceiveDelegate;
};