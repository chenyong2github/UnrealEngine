// Copyright Epic Games, Inc. All Rights Reserved.

#include "Async/NetworkPredictionAsyncProxy.h"
#include "Async/NetworkPredictionAsyncWorldManager.h"
#include "Async/NetworkPredictionAsyncID.h"

bool FNetworkPredictionAsyncProxy::RegisterProxy(UWorld* World)
{
	if (!ensure(Manager == nullptr))
	{
		return false;
	}

	Manager = UE_NP::FNetworkPredictionAsyncWorldManager::Get(World);
	if (ensure(Manager))
	{
		const bool bIsClient = (World->GetNetMode() == NM_Client);
		return Manager->RegisterInstance(ID, bIsClient);
	}

	return false;
}

void FNetworkPredictionAsyncProxy::UnregisterProxy()
{
	if (Manager == nullptr)
	{
		return;
	}

	Manager->UnregisterInstance(ID);
}

void FNetworkPredictionAsyncProxy::RegisterController(APlayerController* PC)
{
	if (!ensure(Manager) || !ensure(ID.IsValid()))
	{
		return;
	}

	Manager->RegisterController(ID, PC);
}

bool FNetworkPredictionAsyncProxy::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
	if (!ensure(Manager))
	{
		// We are NetSerializing without being initialized. RegisterProxy should be called from something like InitializeComponent(s), prior to NetSerialization
		return false;
	}

	// ID
	uint32 RawID=(uint32)ID;
	Ar.SerializeIntPacked(RawID);
	
	if (Ar.IsLoading())
	{
		if ((int32)ID != RawID)
		{
			UE_NP::FNetworkPredictionAsyncID NewID((int32)RawID);
			if (ID.IsValid())
			{
				Manager->RemapClientInstance(ID, NewID);
			}

			ID = NewID;
		}
	}

	// Sim state
	Manager->NetSerializeInstance(ID, Ar);
	return true;
}

bool FNetworkPredictionAsyncProxy::Identical(const FNetworkPredictionAsyncProxy* Other, uint32 PortFlags) const
{
	return CachedLatestFrame == Other->CachedLatestFrame;
}

void FNetworkPredictionAsyncProxy::OnPreReplication()
{
	if (Manager)
	{
		CachedLatestFrame = Manager->GetLatestFrame();
	}
}