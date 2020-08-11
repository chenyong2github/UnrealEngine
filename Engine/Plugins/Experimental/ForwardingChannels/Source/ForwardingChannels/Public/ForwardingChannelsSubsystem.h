// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ForwardingChannelsFwd.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ForwardingChannelFactory.h"
#include "UObject/ScriptInterface.h"
#include "ForwardingChannelsSubsystem.generated.h"

/**
 * Used to create / manage Forwarding Channels and Groups that can be used
 * to help send packets between multiple servers and clients.
 */
UCLASS(DisplayName = "Forwarding Channels Subsystem", Transient)
class FORWARDINGCHANNELS_API UForwardingChannelsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:

	//~ Begin USubsystem Interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem Interface

	/**
	 * Create a new Forwarding channel with the given parameters.
	 * The channel will be registered with the appropriate FForwardingGroup (creating it if necessary).
	 * The channel will be unregistered from the FForwardingGroup upon destruction.
	 *
	 * @param Params	Params to use for creation.
	 *
	 * @return	The newly created FForwardingChannel. May be null if creation of the Channel or Group failed.
	 */
	TSharedPtr<ForwardingChannels::FForwardingChannel> CreateChannel(const ForwardingChannels::FCreateChannelParams& Params);

	/**
	 * Find or create the specified FForwardingGroup.
	 *
	 * @param GroupName		The name of the group to find or create.
	 *
	 * @return	The found (or created) FForwardingGroup. May be null if a group didn't already exist and we failed
				to create one.
	 */
	TSharedPtr<ForwardingChannels::FForwardingGroup> GetOrCreateForwardingGroup(const FName GroupName);

	/**
	 * Registers the given Forwarding Channel Factory so it can receive callbacks to create
	 * forwarding channels when necessary.
	 *
	 * @param InFactory the Factory to register.
	 */
	void RegisterForwardingChannelFactory(TScriptInterface<IForwardingChannelFactory> InFactory);

	/**
	 * Unregisters the given Forwarding Channel Factory.
	 *
	 * @param InFactory the Factory to register.
	 */
	void UnregisterForwardingChannelFactory(TScriptInterface<IForwardingChannelFactory> InFactory);

	/**
	 * Request that all registered Forwarding Channel Factories create forwarding channels / groups
	 * that will be owned by the given Net Connection.
	 *
	 * This is typically called on servers to create UChannels, and clients will create the necessary
	 * forwarding channels when they receive the UChannel open notification.
	 *
	 * Even though a factory is registered, there's no guarantee it will create a forwarding channel.
	 *
	 * @param InNetConnection	The Net Connection that will own the UChannel for the Forwarding Channel.
	 */
	void CreateForwardingChannels(class UNetConnection* InNetConnection);

	/**
	 * Request that all registered Forwarding Channel Factories accept or ignore client packets.
	 * @see IForwardingChannelFactory::SetAcceptClientPackets.
	 *
	 * @param bool bShouldAcceptClientPackets
	 */
	void SetAcceptClientPackets(bool bShouldAcceptClientPackets);

private:

	UPROPERTY()
	TArray<TScriptInterface<IForwardingChannelFactory>> ForwardingChannelFactories;

	TMap<FName, TWeakPtr<ForwardingChannels::FForwardingGroup>> ChannelGroupsByName;
	bool bIsInitialized = false;
};