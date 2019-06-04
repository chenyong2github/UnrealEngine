// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Containers/CircularQueue.h"
#include "UObject/Object.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Common/UdpSocketReceiver.h"
#include "OSCMessage.h"
#include "OSCBundle.h"
#include "OSCServer.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOSCReceivedMessageEvent, const FString &, Address, const FOSCMessage &, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOSCReceivedBundleEvent, const FOSCBundle &, Bundle);

class FSocket;

UCLASS(BlueprintType)
class OSC_API UOSCServer : public UObject
{
	GENERATED_BODY()

	struct OscCircularBuffer
	{
	public:

		OscCircularBuffer(uint32 CircularQueueSize)
			: Buffer(CircularQueueSize)
		{}

		OscCircularBuffer()
			: Buffer(1)
		{ }

		TCircularQueue<TSharedPtr<FOSCPacket>> Buffer;
	};

public:
	UOSCServer();
	virtual ~UOSCServer();

	/** Sets the IP address and port to listen for OSC data. */
	void Listen(FIPv4Address IPAddress, uint32_t Port, bool MulticastLoopback);

	/** Event that gets called when an OSC message is received. */
	UPROPERTY(BlueprintAssignable, Category = "Audio|OSC")
	FOSCReceivedMessageEvent OnOscReceived;

	/** Event that gets called when an OSC bundle is received. */
	UPROPERTY(BlueprintAssignable, Category = "Audio|OSC")
	FOSCReceivedBundleEvent OnOscBundleReceived;
	
protected:

	void BeginDestroy() override;
	
	/** Stop and tidy up network socket. */
	void Stop();

	/** Callback that receives data from a socket. */
	void Callback(const FArrayReaderPtr& Data, const FIPv4Endpoint& Endpoint);
	
	/** Circular buffer that stores OSC packets. */
	OscCircularBuffer OSCPackets;
	
	/** Socket used to listen for OSC packets. */
	FSocket* Socket;

	/** UDP receiver. */
	FUdpSocketReceiver* SocketReceiver;

	/** OSC buffer lock. */
	int32 OSCBufferLock;
};
