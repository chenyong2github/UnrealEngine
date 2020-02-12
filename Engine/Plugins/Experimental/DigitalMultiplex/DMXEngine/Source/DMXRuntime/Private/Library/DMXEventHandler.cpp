// Copyright Epic Games, Inc. All Rights Reserved.

#include "Library/DMXEventHandler.h"
#include "Interfaces/IDMXProtocol.h"

void UDMXEventHandler::BufferReceivedBroadcast(FName Protocol, uint16 UniverseID, const TArray<uint8>& Values)
{
	OnProtocolReceived.Broadcast(FDMXProtocolName(Protocol), UniverseID, Values);
}

UDMXEventHandler::UDMXEventHandler()
{
	if (!HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		for (const FName& ProtocolName : IDMXProtocol::GetProtocolNames())
		{
			if (TSharedPtr<IDMXProtocol> Protocol = IDMXProtocol::Get(ProtocolName))
			{
				Protocol->GetOnUniverseInputUpdate().AddUObject(this, &UDMXEventHandler::BufferReceivedBroadcast);
			}
		}
	}
}

bool UDMXEventHandler::ConditionalBeginDestroy()
{
	OnProtocolReceived.RemoveAll(this);

	return Super::ConditionalBeginDestroy();
}
