// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolCommon.h"
#include "Interfaces/IDMXProtocol.h"

#include "Dom/JsonObject.h"

template<class TUniverse>
class FDMXProtocolUniverseManager
{
public:
	using TUniversesPtr = TSharedPtr<TUniverse, ESPMode::ThreadSafe>;
	using IUniversesMap = TMap<uint32, TUniversesPtr>;

public:
	FDMXProtocolUniverseManager(IDMXProtocol* InDMXProtocol)
		: DMXProtocol(InDMXProtocol)
	{}

	TUniversesPtr AddUniverse(uint32 InUniverseID, TUniversesPtr InUniverse)
	{
		return UniversesMap.Add(InUniverseID, InUniverse);
	}

	TUniversesPtr AddUniverseCreate(uint32 InUniverseID)
	{
		TUniversesPtr* UniverseAddr = UniversesMap.Find(InUniverseID);

		// Try to find universe
		if (UniverseAddr != nullptr)
		{
			return *UniverseAddr;
		}
		// Otherwise create new and return new instance 
		else
		{
			FJsonObject UniverseSettings;
			UniverseSettings.SetNumberField(TEXT("UniverseID"), InUniverseID);
			UniverseSettings.SetNumberField(TEXT("PortID"), 0); // TODO set PortID

			TUniversesPtr Universe = StaticCastSharedPtr<TUniverse>(DMXProtocol->AddUniverse(UniverseSettings));

			UniversesMap.Add(InUniverseID, Universe);

			return Universe;
		}
	}

	bool RemoveUniverseById(uint32 UniverseID)
	{
		TUniversesPtr Universe = GetUniverseById(UniverseID);
		if (Universe.IsValid())
		{
			UniversesMap.Remove(UniverseID);
			return true;
		}
		else
		{
			UE_LOG_DMXPROTOCOL(Verbose, TEXT("UniverseID %d doesn't exists"), UniverseID);
			return false;
		}
	}

	void RemoveAll()
	{
		UniversesMap.Empty();
	}

	TUniversesPtr GetUniverseById(uint32 UniverseID)
	{
		TUniversesPtr* UniverseAddr = UniversesMap.Find(UniverseID);

		if (UniverseAddr != nullptr)
		{
			return *UniverseAddr;
		}

		return nullptr;
	}

	const TMap<uint32, TUniversesPtr>& GetAllUniverses() const
	{
		return UniversesMap; 
	}

private:
	IUniversesMap UniversesMap;

	IDMXProtocol* DMXProtocol;
};
