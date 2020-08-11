// Copyright Epic Games, Inc. All Rights Reserved.

#include "Interfaces/IDMXProtocol.h"
#include "Managers/DMXProtocolUniverseManager.h"

#include "Dom/JsonObject.h"

TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe> IDMXProtocol::GetUniverseByIdCreateDefault(uint32 InUniverseId)
{
	TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe> Universe = GetUniverseById(InUniverseId);

	if (!Universe.IsValid())
	{
		FJsonObject DefaultSettings;
		GetDefaultUniverseSettings(InUniverseId, DefaultSettings);
		Universe = AddUniverse(DefaultSettings);
	}
	check(Universe.IsValid());

	return Universe;
}