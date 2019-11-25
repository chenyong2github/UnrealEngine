// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Async/TaskGraphInterfaces.h"
#include "Containers/Queue.h"
#include "Templates/UniquePtr.h"
#include "UObject/Object.h"

#include "OSCBundle.h"
#include "OSCMessage.h"
#include "OSCPacket.h"

#include "OSCServer.generated.h"

// Forward Declarations
class FSocket;


// Delegates
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOSCReceivedMessageEvent, const FOSCMessage&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOSCDispatchMessageEvent, const FOSCAddress&, AddressPattern, const FOSCMessage&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOSCReceivedBundleEvent, const FOSCBundle&, Bundle);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FOSCDispatchMessageEventBP, const FOSCAddress&, AddressPattern, const FOSCMessage&, Message);

DECLARE_STATS_GROUP(TEXT("OSC Commands"), STATGROUP_OSCNetworkCommands, STATCAT_Advanced);


/** Interface for internal networking implementation.  See UOSCServer for details */
class OSC_API IOSCServerProxy
{
public:
	virtual ~IOSCServerProxy() { }

	virtual bool GetMulticastLoopback() const = 0;
	virtual bool IsActive() const = 0;
	virtual void Listen(const FString& ServerName) = 0;
	virtual bool SetAddress(const FString& InReceiveIPAddress, int32 InPort) = 0;
	virtual void SetMulticastLoopback(bool bInMulticastLoopback) = 0;
	virtual void Stop() = 0;
	virtual void AddWhitelistedClient(const FString& InIPAddress) = 0;
	virtual void RemoveWhitelistedClient(const FString& IPAddress) = 0;
	virtual void ClearWhitelistedClients() = 0;
	virtual TSet<FString> GetWhitelistedClients() const = 0;
	virtual void SetWhitelistClientsEnabled(bool bEnabled) = 0;
};

UCLASS(BlueprintType)
class OSC_API UOSCServer : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	void Connect();

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
	FOSCReceivedMessageEvent OnOscMessageReceived;

	/** Event that gets called when an OSC bundle is received. */
	UPROPERTY(BlueprintAssignable, Category = "Audio|OSC")
	FOSCReceivedBundleEvent OnOscBundleReceived;

	/** When set to true, server will only process received 
	  * messages from whitelisted clients.
	  */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	void SetWhitelistClientsEnabled(bool bEnabled);

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

	/** Adds event to dispatch when OSCAddressPattern is matched. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	void BindEventToOnOSCAddressPatternMatchesPath(const FOSCAddress& OSCAddressPattern, const FOSCDispatchMessageEventBP& Event);

	/** Unbinds specific event from OSCAddress pattern. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	void UnbindEventFromOnOSCAddressPatternMatchesPath(const FOSCAddress& OSCAddressPattern, const FOSCDispatchMessageEventBP& Event);

	/** Removes OSCAddressPattern from sending dispatch events. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	void UnbindAllEventsFromOnOSCAddressPatternMatchesPath(const FOSCAddress& OSCAddressPattern);

	/** Removes all events from OSCAddressPatterns to dispatch. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	void UnbindAllEventsFromOnOSCAddressPatternMatching();

	/** Returns set of OSCAddressPatterns currently listening for matches to dispatch. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	TArray<FOSCAddress> GetBoundOSCAddressPatterns() const;

	/** Clears all packets pending processing */
	void ClearPackets();

	/** Enqueues packet to be processed */
	void EnqueuePacket(TSharedPtr<IOSCPacket>);

	/** Callback for when packet is received by server */
	void OnPacketReceived(const FString& InIPAddress);

protected:
	void BeginDestroy() override;

private:
	/** Dispatches provided bundle received */
	void DispatchBundle(const FString& InIPAddress, const FOSCBundle& InBundle);

	/** Dispatches provided message received */
	void DispatchMessage(const FString& InIPAddress, const FOSCMessage& InMessage);

	/** Pointer to internal implementation of server proxy */
	TUniquePtr<IOSCServerProxy> ServerProxy;

	/** Queue stores incoming OSC packet requests to process on the game thread. */
	TQueue<TSharedPtr<IOSCPacket>> OSCPackets;

	/** Address pattern hash to check against when dispatching incoming messages */
	TMap<FOSCAddress, FOSCDispatchMessageEvent> AddressPatterns;
};
