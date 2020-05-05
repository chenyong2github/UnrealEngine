// Copyright Epic Games, Inc. All Rights Reserved.
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
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOSCReceivedMessageEvent, const FOSCMessage&, Message, const FString&, IPAddress, int32, Port);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOSCReceivedMessageNativeEvent, const FOSCMessage&, const FString& /*IPAddress*/, uint16 /*Port*/);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOSCDispatchMessageEvent, const FOSCAddress&, AddressPattern, const FOSCMessage&, Message, const FString&, IPAddress, int32, Port);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOSCReceivedBundleEvent, const FOSCBundle&, Bundle, const FString&, IPAddress, int32, Port);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOSCReceivedBundleNativeEvent, const FOSCBundle&, const FString& /*IPAddress*/, uint16 /*Port*/);
DECLARE_DYNAMIC_DELEGATE_FourParams(FOSCDispatchMessageEventBP, const FOSCAddress&, AddressPattern, const FOSCMessage&, Message, const FString&, IPAddress, int32, Port);

DECLARE_STATS_GROUP(TEXT("OSC Commands"), STATGROUP_OSCNetworkCommands, STATCAT_Advanced);


/** Interface for internal networking implementation.  See UOSCServer for details */
class OSC_API IOSCServerProxy
{
public:
	virtual ~IOSCServerProxy() { }

	virtual FString GetIpAddress() const = 0;
	virtual int32 GetPort() const = 0;
	virtual bool GetMulticastLoopback() const = 0;
	virtual bool IsActive() const = 0;
	virtual void Listen(const FString& ServerName) = 0;
	virtual bool SetAddress(const FString& InReceiveIPAddress, int32 InPort) = 0;
	virtual void SetMulticastLoopback(bool bInMulticastLoopback) = 0;
#if WITH_EDITOR
	virtual void SetTickableInEditor(bool bInTickInEditor) = 0;
#endif // WITH_EDITOR
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

	/** Native event that gets called when an OSC message is received. */
	FOSCReceivedMessageNativeEvent OnOscMessageReceivedNative;

	/** Event that gets called when an OSC bundle is received. */
	UPROPERTY(BlueprintAssignable, Category = "Audio|OSC")
	FOSCReceivedBundleEvent OnOscBundleReceived;

	/** Native event that gets called when an OSC bundle is received. */
	FOSCReceivedBundleNativeEvent OnOscBundleReceivedNative;

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

	/** Returns the IP for the server if connected as a string. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	FString GetIpAddress(bool bIncludePort) const;

	/** Returns the port for the server if connected. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	int32 GetPort() const;

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

#if WITH_EDITOR
	/** Set whether server instance can be ticked in-editor (editor only and available to blueprint
	  * for use in editor utility scripts/script actions).
	  */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	void SetTickInEditor(bool bInTickInEditor);
#endif // WITH_EDITOR

	/** Clears all packets pending processing */
	void ClearPackets();

	/** Enqueues packet to be processed */
	void EnqueuePacket(TSharedPtr<IOSCPacket> InPacket);

	/** Callback for when packet is received by server */
	void PumpPacketQueue(const TSet<uint32>* InWhitelistedClients);

protected:
	void BeginDestroy() override;

private:
	/** Dispatches provided bundle received */
	void DispatchBundle(const FString& InIPAddress, uint16 InPort, const FOSCBundle& InBundle);

	/** Dispatches provided message received */
	void DispatchMessage(const FString& InIPAddress, uint16 InPort, const FOSCMessage& InMessage);

	/** Pointer to internal implementation of server proxy */
	TUniquePtr<IOSCServerProxy> ServerProxy;

	/** Queue stores incoming OSC packet requests to process on the game thread. */
	TQueue<TSharedPtr<IOSCPacket>> OSCPackets;

	/** Address pattern hash to check against when dispatching incoming messages */
	TMap<FOSCAddress, FOSCDispatchMessageEvent> AddressPatterns;
};
