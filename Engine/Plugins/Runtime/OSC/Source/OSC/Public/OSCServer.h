// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "UObject/Object.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Common/UdpSocketReceiver.h"
#include "OSCMessage.h"
#include "OSCBundle.h"
#include "OSCPacket.h"
#include "OSCServer.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOSCReceivedMessageEvent, const FOSCMessage &, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOSCReceivedBundleEvent, const FOSCBundle &, Bundle);

class FSocket;

UCLASS(BlueprintType)
class OSC_API UOSCServer : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	/** Gets whether or not to loopback if ReceiveIPAddress provided is multicast. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	bool GetMulticastLoopback() const;

	/** Returns whether server is actively listening to incoming messages. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	bool IsActive() const;

	/** Sets the IP address and port to listen for OSC data. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	void Listen();

	/** Set the address and port of server. Fails if server is currently active. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	bool SetAddress(const FString& ReceiveIPAddress, int32 Port);

	/** Set whether or not to loopback if ReceiveIPAddress provided is multicast. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	void SetMulticastLoopback(bool bMulticastLoopback);

	/** Stop and tidy up network socket. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	void Stop();

	/** Event that gets called when an OSC message is received. */
	UPROPERTY(BlueprintAssignable, Category = "Audio|OSC")
	FOSCReceivedMessageEvent OnOscReceived;

	/** Event that gets called when an OSC bundle is received. */
	UPROPERTY(BlueprintAssignable, Category = "Audio|OSC")
	FOSCReceivedBundleEvent OnOscBundleReceived;
	
	/** When true, only process received events from whitelisted clients. */
	UPROPERTY(BlueprintReadWrite, Category = "Audio|OSC")
	bool bWhitelistClients;

	/** Adds client to whitelist of clients to listen for. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	void AddWhitelistedClient(const FString& IPAddress);

	/** Removes whitelisted client to listen for. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	void RemoveWhitelistedClient(const FString& IPAddress);

	/** Clears client whitelist to listen for. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	void ClearWhitelistedClients();

	/** Returns set of whitelisted clients. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	TSet<FString> GetWhitelistedClients() const;

protected:
	void BeginDestroy() override;
	
	/** Callback that receives data from a socket. */
	void Callback(const FArrayReaderPtr& Data, const FIPv4Endpoint& Endpoint);
	
	/** Circular buffer that stores OSC packets. */
	TQueue<TSharedPtr<IOSCPacket>> OSCPackets;

	/** Socket used to listen for OSC packets. */
	FSocket* Socket;

	/** UDP receiver. */
	FUdpSocketReceiver* SocketReceiver;

	/** Set of client addresses whitelisted to process packets from. */
	TSet<uint32> ClientWhitelist;

	/** OSC buffer lock. */
	int32 OSCBufferLock;

	/** IPAddress to listen for OSC packets on.  If unset, defaults to LocalHost */
	FIPv4Address ReceiveIPAddress;

	/** Port to listen for OSC packets on. */
	int32 Port;

	/** Whether or not to loopback if address provided is multicast */
	bool bMulticastLoopback;
};
