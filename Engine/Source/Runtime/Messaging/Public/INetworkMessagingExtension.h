// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Features/IModularFeature.h"

/**
 * Interface for the messaging module network extension
 * Plugins or modules implementing messaging transport for MessageBus
 * can implement this modular feature to provide control on the service it provides.
 */
class INetworkMessagingExtension : public IModularFeature
{
public:
	/** The modular feature name to get the messaging extension. */
	static MESSAGING_API FName ModularFeatureName;

	/**
	 * Get the name of this messaging extension.
	 */
	virtual FName GetName() const = 0;

	/**
	 * Indicate if support is available for this extension
	 * @return true if the service can be successfully started.
	 */
	virtual bool IsSupportEnabled() const = 0;

	/**
	 * Start or restart this messaging extension service for MessageBus
	 * using its current running configuration which might include modifications to endpoints
	 * @see AddEndpoint, RemoveEndpoint
	 */
	virtual void RestartServices() = 0;

	/**
	 * Shutdown this messaging extension services for MessageBus
	 * and remove any configuration modification. 
	 * Using RestartServices after ShutdownServices will start the service with an unaltered configuration
	 * @see RestartServices
	 */
	virtual void ShutdownServices() = 0;

	/**
	 * Add an endpoint to the running configuration of this messaging service
	 * This change is transient and does not modified saved configuration.
	 * @param InEndpoint the endpoint string to add to the running service, should be in the form <ipv4:port>.
	 */
	virtual void AddEndpoint(const FString& InEndpoint) = 0;

	/**
	 * Remove a static endpoint from the running configuration of the UDP messaging service
	 * This change is transient and does not modified saved configuration.
	 * @param InEndpoint the endpoint to remove from the running service, should be in the form <ipv4:port>.
	 */
	virtual void RemoveEndpoint(const FString& InEndpoint) = 0;
};
