// Copyright Epic Games, Inc. All Rights Reserved.

#include "ForwardingChannelsSubsystem.h"
#include "ForwardingChannel.h"
#include "ForwardingGroup.h"
#include "Engine/GameInstance.h"
#include "EngineLogs.h"

namespace ForwardingChannelsSubsystemPrivate
{
	template<typename FunctorType>
	void ForEachFactory(TArray<TScriptInterface<IForwardingChannelFactory>>& ForwardingChannelFactories, const FunctorType& Functor)
	{
		for (auto It = ForwardingChannelFactories.CreateIterator(); It; ++It)
		{
			TScriptInterface<IForwardingChannelFactory>& Factory = *It;

			if (!Factory)
			{
				It.RemoveCurrent();
				continue;
			}

			Functor(*Factory);
		}
	}
}

void UForwardingChannelsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	bIsInitialized = true;
}

void UForwardingChannelsSubsystem::Deinitialize()
{
	using namespace ForwardingChannels;

	for (TPair<FName, TWeakPtr<FForwardingGroup>> NameAndGroup : ChannelGroupsByName)
	{
		TSharedPtr<FForwardingGroup> Group = NameAndGroup.Value.Pin();
		if (Group.IsValid())
		{
			Group->OnSubsystemDeinitialized();
		}
	}

	ChannelGroupsByName.Empty();
	bIsInitialized = false;
}

TSharedPtr<ForwardingChannels::FForwardingChannel> UForwardingChannelsSubsystem::CreateChannel(const ForwardingChannels::FCreateChannelParams& Params)
{
	using namespace ForwardingChannels;

	TSharedPtr<FForwardingChannel> CreatedChannel;

	if (!bIsInitialized)
	{
		UE_LOG(LogNet, Warning,
			TEXT("UForwardingChannelsSubsystem::CreateChannel: Unable to create channel while subsystem is uninitialized. Group Name = %s"),
			*Params.GroupName.ToString());
	}
	else if (Params.GroupName == NAME_None)
	{
		UE_LOG(LogNet, Warning,
			TEXT("UForwardingChannelsSubsystem::CreateChannel: Must specify valid Group Name."));
	}
	else
	{
		CreatedChannel = FForwardingChannel::CreateChannel(Params, this);
	}

	return CreatedChannel;
}

TSharedPtr<ForwardingChannels::FForwardingGroup> UForwardingChannelsSubsystem::GetOrCreateForwardingGroup(const FName GroupName)
{
	using namespace ForwardingChannels;

	TSharedPtr<FForwardingGroup> Group;

	if (!bIsInitialized)
	{
		UE_LOG(LogNet, Warning,
			TEXT("UForwardingChannelsSubsystem::GetOrCreateForwardingGroup: Unable to create group while subsystem is uninitialized. Group Name = %s"),
			*GroupName.ToString());
	}
	else if (GroupName == NAME_None)
	{
		UE_LOG(LogNet, Warning,
			TEXT("UForwardingChannelsSubsystem::GetOrCreateForwardingGroup: Must specify valid Group Name."));
	}
	else
	{
		Group = ChannelGroupsByName.FindRef(GroupName).Pin();
		if (!Group.IsValid())
		{
			Group = MakeShared<FForwardingGroup>(GroupName);
			ChannelGroupsByName.Add(GroupName, Group);
		}
	}

	return Group;
}


void UForwardingChannelsSubsystem::RegisterForwardingChannelFactory(TScriptInterface<IForwardingChannelFactory> InFactory)
{
	if (!bIsInitialized || !InFactory)
	{
		return;
	}

	ForwardingChannelFactories.AddUnique(InFactory);
}

void UForwardingChannelsSubsystem::UnregisterForwardingChannelFactory(TScriptInterface<IForwardingChannelFactory> InFactory)
{
	// Explicitly ignoring bIsInitialized here, because we don't clear the ForwardingChannelFactories group
	// in Deinitialize.
	ForwardingChannelFactories.Remove(InFactory);
}

void UForwardingChannelsSubsystem::CreateForwardingChannels(class UNetConnection* InNetConnection)
{
	if (!bIsInitialized)
	{
		return;
	}

	ForwardingChannelsSubsystemPrivate::ForEachFactory(ForwardingChannelFactories, [InNetConnection](IForwardingChannelFactory& Factory)
		{
			Factory.CreateForwardingChannel(InNetConnection);
		}
	);
}

void UForwardingChannelsSubsystem::SetAcceptClientPackets(bool bShouldAcceptClientPackets)
{
	if (!bIsInitialized)
	{
		return;
	}

	ForwardingChannelsSubsystemPrivate::ForEachFactory(ForwardingChannelFactories, [bShouldAcceptClientPackets](IForwardingChannelFactory& Factory)
		{
			Factory.SetAcceptClientPackets(bShouldAcceptClientPackets);
		}
	);
}