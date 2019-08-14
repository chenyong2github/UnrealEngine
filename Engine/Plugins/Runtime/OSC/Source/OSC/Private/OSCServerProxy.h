// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Common/UdpSocketReceiver.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "UObject/Object.h"

#include "OSCServer.h"


class OSC_API FOSCServerProxy : public IOSCServerProxy
{
public:
	FOSCServerProxy(UOSCServer& InServer);
	virtual ~FOSCServerProxy() { }

	bool GetMulticastLoopback() const override;

	bool IsActive() const override;

	void Listen(const FString& ServerName) override;

	bool SetAddress(const FString& ReceiveIPAddress, int32 Port) override;
	void SetMulticastLoopback(bool bMulticastLoopback) override;

	void Stop() override;

	void AddWhitelistedClient(const FString& IPAddress) override;
	void RemoveWhitelistedClient(const FString& IPAddress) override;
	void ClearWhitelistedClients() override;
	TSet<FString> GetWhitelistedClients() const override;

	void SetWhitelistClientsEnabled(bool bEnabled) override;

	/** Callback that receives data from a socket. */
	void OnMessageReceived(const FArrayReaderPtr& Data, const FIPv4Endpoint& Endpoint);

	UOSCServer* Server;

	/** Socket used to listen for OSC packets. */
	FSocket* Socket;

	/** UDP receiver. */
	FUdpSocketReceiver* SocketReceiver;

	/** Set of client addresses whitelisted to process packets from. */
	TSet<uint32> ClientWhitelist;

	/** IPAddress to listen for OSC packets on.  If unset, defaults to LocalHost */
	FIPv4Address ReceiveIPAddress;

	/** Port to listen for OSC packets on. */
	int32 Port;

	/** Whether or not to loopback if address provided is multicast */
	bool bMulticastLoopback;

	/** Whether or not to use client whitelist */
	bool bWhitelistClients;
};
