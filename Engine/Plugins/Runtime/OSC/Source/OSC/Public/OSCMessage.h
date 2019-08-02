// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "OSCLog.h"
#include "OSCPacket.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "UObject/WeakObjectPtr.h"

#include "OSCMessage.generated.h"

// Forward Declarations
class FOSCStream;


USTRUCT(BlueprintType)
struct OSC_API FOSCMessage
{
	GENERATED_USTRUCT_BODY()

	FOSCMessage();
	FOSCMessage(const TSharedPtr<FOSCMessagePacket>& InPacket);
	~FOSCMessage();

	void SetPacket(TSharedPtr<FOSCMessagePacket>& InPacket);
	const TSharedPtr<FOSCMessagePacket>& GetPacket() const;

	bool SetAddress(const FOSCAddress& InAddress);
	const FOSCAddress& GetAddress() const;

private:
	TSharedPtr<FOSCMessagePacket> Packet;
};