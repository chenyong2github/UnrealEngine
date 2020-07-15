// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ForwardingChannelFactory.generated.h"

UINTERFACE(MinimalApi, meta = (CannotImplementInterfaceInBlueprint))
class UForwardingChannelFactory : public UInterface
{
	GENERATED_BODY()

};

/**
 * Forwarding Channel Factories provide provide a simple way to create all
 * necessary Forwarding Channels for a given connection / network interface type.
 *
 * Typically, each FForwardingGroup would have its own Factory Type, but that's
 * not a requirement.
 */
class IForwardingChannelFactory
{
	GENERATED_BODY()

public:

	/**
	 * Create a forwarding channel for the given UNetConnection if necessary.
	 * Factories may ignore this request if they are not configured properly
	 * or are otherwise disabled.
	 *
	 * @param	InNetConnection		The connection that will own the UChannel that wraps
	 *								the Forwarding Channel.
	 */
	virtual void CreateForwardingChannel(class UNetConnection* InNetConnection) = 0;

	/**
	 * Whether or not channels created by this factory should accept incoming client
	 * packets. This can be useful, for example, if you want to allow only certain
	 * servers to accept client packets and then to replicate them down to additional
	 * clients.
	 *
	 * @param	bAcceptClientPackets	Whether or not we should accept client packets.
	 */
	virtual void SetAcceptClientPackets(bool bShouldAcceptClientPackets) = 0;
};