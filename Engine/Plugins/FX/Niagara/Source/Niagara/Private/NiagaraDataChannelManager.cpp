// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataChannelManager.h"

#include "NiagaraModule.h"
#include "NiagaraWorldManager.h"
#include "NiagaraDataChannel.h"
#include "NiagaraDataChannelHandler.h"

DECLARE_CYCLE_STAT(TEXT("FNiagaraDataChannelManager::BeginFrame"), STAT_DataChannelManager_BeginFrame, STATGROUP_NiagaraDataChannels);
DECLARE_CYCLE_STAT(TEXT("FNiagaraDataChannelManager::EndFrame"), STAT_DataChannelManager_EndFrame, STATGROUP_NiagaraDataChannels);
DECLARE_CYCLE_STAT(TEXT("FNiagaraDataChannelManager::Tick"), STAT_DataChannelManager_Tick, STATGROUP_NiagaraDataChannels);

FNiagaraDataChannelManager::FNiagaraDataChannelManager(FNiagaraWorldManager* InWorldMan)
	: WorldMan(InWorldMan)
{
}

void FNiagaraDataChannelManager::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObjects(Channels);
}

void FNiagaraDataChannelManager::Init()
{
	//Initialize any existing data channels, more may be initialized later as they are loaded.
	UNiagaraDataChannel::ForEachDataChannel([&](UNiagaraDataChannel* DataChannel)
	{
		if (DataChannel->HasAnyFlags(RF_ClassDefaultObject) == false)
		{
			InitDataChannel(DataChannel, true);
		}
	});
}

void FNiagaraDataChannelManager::Cleanup()
{
	Channels.Empty();
}

void FNiagaraDataChannelManager::BeginFrame(float DeltaSeconds)
{
	if (INiagaraModule::DataChannelsEnabled())
	{
		SCOPE_CYCLE_COUNTER(STAT_DataChannelManager_BeginFrame);
		//Tick all DataChannel channel handlers.
		for (auto& ChannelPair : Channels)
		{
			ChannelPair.Value->BeginFrame(DeltaSeconds, WorldMan);
		}
	}
}

void FNiagaraDataChannelManager::EndFrame(float DeltaSeconds)
{
	if (INiagaraModule::DataChannelsEnabled())
	{
		SCOPE_CYCLE_COUNTER(STAT_DataChannelManager_EndFrame);
		//Tick all DataChannel channel handlers.
		for (auto& ChannelPair : Channels)
		{
			ChannelPair.Value->EndFrame(DeltaSeconds, WorldMan);
		}
	}
}

void FNiagaraDataChannelManager::Tick(float DeltaSeconds, ETickingGroup TickGroup)
{
	if(INiagaraModule::DataChannelsEnabled())
	{
		SCOPE_CYCLE_COUNTER(STAT_DataChannelManager_Tick);
		//Tick all DataChannel channel handlers.
		for (auto& ChannelPair : Channels)
		{
			ChannelPair.Value->Tick(DeltaSeconds, TickGroup, WorldMan);
		}
	}
	else
	{
		Channels.Empty();
	}
}

UNiagaraDataChannelHandler* FNiagaraDataChannelManager::FindDataChannelHandler(const UNiagaraDataChannel* Channel)
{
	if (TObjectPtr<UNiagaraDataChannelHandler>* Found = Channels.Find(Channel))
	{
		return (*Found).Get();
	}
	return nullptr;
}

void FNiagaraDataChannelManager::InitDataChannel(const UNiagaraDataChannel* InChannel, bool bForce)
{
	UWorld* World = GetWorld();
	if(INiagaraModule::DataChannelsEnabled() && World && !World->IsNetMode(NM_DedicatedServer) && InChannel->IsValid())
	{
		TObjectPtr<UNiagaraDataChannelHandler>& Handler = Channels.FindOrAdd(InChannel);

		if (bForce || Handler == nullptr)
		{
			Handler = InChannel->CreateHandler(WorldMan->GetWorld());
		}
	}
}

void FNiagaraDataChannelManager::RemoveDataChannel(const UNiagaraDataChannel* InChannel)
{
	Channels.Remove(InChannel);
}

UWorld* FNiagaraDataChannelManager::GetWorld()const
{
	return WorldMan->GetWorld();
}
