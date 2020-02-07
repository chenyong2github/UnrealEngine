// Copyright Epic Games, Inc. All Rights Reserved.

#include "Library/DMXEventHandler.h"
#include "Interfaces/IDMXProtocol.h"

void UDMXEventHandler::BufferReceivedBroadcast(FName Protocol, uint16 UniverseID, const TArray<uint8>& Values)
{
	OnProtocolReceived.Broadcast(FDMXProtocolName(Protocol), UniverseID, Values);
}

UDMXEventHandler::UDMXEventHandler()
{
	AddToRoot();

	for (const FName& ProtocolName : IDMXProtocol::GetProtocolNames())
	{
		if (TSharedPtr<IDMXProtocol> Protocol = IDMXProtocol::Get(ProtocolName))
		{
			Protocol->GetOnUniverseInputUpdate().AddUObject(this, &UDMXEventHandler::BufferReceivedBroadcast);
		}
	}
}

bool UDMXEventHandler::ConditionalBeginDestroy()
{
	RemoveFromRoot();
	OnProtocolReceived.RemoveAll(this);

	return Super::ConditionalBeginDestroy();
}
