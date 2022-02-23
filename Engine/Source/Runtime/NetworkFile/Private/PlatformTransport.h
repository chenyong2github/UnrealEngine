// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Runtime/NetworkFile/Private/ITransport.h"
#include "GenericPlatform/GenericPlatformHostSocket.h"


/**
 * Implementation of ITransport using IPlatformHostCommunication/IPlatformHostSocket interfaces (custom target and host pc communication).
 */
class FPlatformTransport : public ITransport
{

public:

	FPlatformTransport(int32 InProtocolIndex, const FString& InProtocolName);
	~FPlatformTransport();

	//~ Begin ITransport interface
	virtual bool Initialize(const TCHAR* HostIp) override;
	virtual bool SendPayloadAndReceiveResponse(TArray<uint8>& In, TArray<uint8>& Out) override;
	virtual bool ReceiveResponse(TArray<uint8>& Out) override;
	//~ End ITransport interface

private:

	/**
	 * Wait until HostSocket is in a non-default state (preferably Connected).
	 * @return True is the socket is Connected, false otherwise (an error or if the host pc immediately disconnects).
	 */
	bool WaitUntilConnected();

	int32				   ProtocolIndex;
	FString				   ProtocolName;
	IPlatformHostSocketPtr HostSocket;

};
