// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "IPAddress.h"
#include "OSCMessage.h"
#include "OSCBundle.h"
#include "OSCClient.generated.h"

class FSocket;

UCLASS(BlueprintType)
class OSC_API UOSCClient : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	UOSCClient();

	/** Sets the OSC Client IP address and port. Returns whether 
	  * address and port was successfully set. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	bool SetSendIPAddress(const FString& IPAddress, const int32 Port);

	/** Gets the OSC Client IP address and port. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	void GetSendIPAddress(UPARAM(ref) FString& IPAddress, UPARAM(ref) int32& Port);

	/** Send OSC message to  a specific address. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	void SendOSCMessage(UPARAM(ref) FOSCMessage& Message);

	/** Send OSC Bundle over the network. */
	UFUNCTION(BlueprintCallable, Category = "Audio|OSC")
	void SendOSCBundle(UPARAM(ref) FOSCBundle& Bundle);

protected:

	void BeginDestroy() override;
	
	/** Stop and tidy up network socket. */
	void Stop();
	
	/** Send OSC packet data. */
	void SendPacket(IOSCPacket& InPacket);

	/** Socket used to send the OSC packets. */
	FSocket* Socket;

	/** IP Address used by socket. */
	TSharedPtr<FInternetAddr> IPAddress;
};
