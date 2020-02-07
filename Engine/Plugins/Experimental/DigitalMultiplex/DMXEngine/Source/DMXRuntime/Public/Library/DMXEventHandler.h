// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Library/DMXObjectBase.h"
#include "DMXProtocolTypes.h"

#include "DMXEventHandler.generated.h"

class IDMXProtocol;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FProtocolReceivedDelegate, FDMXProtocolName, Protocol, int32, Universe, const TArray<uint8>&, DMXBuffer);

/** Broadcasts Protocol events */
UCLASS(BlueprintType)
class DMXRUNTIME_API UDMXEventHandler : public UDMXObjectBase
{
	GENERATED_BODY()
	
public:
	UPROPERTY(BlueprintAssignable, Category = "DMX")
	FProtocolReceivedDelegate OnProtocolReceived;

public:
	UDMXEventHandler();

	//~ UObject interface begin
	virtual bool ConditionalBeginDestroy();
	//~ UObject interface end

protected:

	// This function is a delegate for when protocols have input updates
	void BufferReceivedBroadcast(FName Protocol, uint16 UniverseID, const TArray<uint8>& Values);
};
