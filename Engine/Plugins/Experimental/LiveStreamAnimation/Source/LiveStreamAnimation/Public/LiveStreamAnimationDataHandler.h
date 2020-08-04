// Copyright Epic Games, Inc. All Rights Reserverd.

#pragma once

#include "UObject/Object.h"
#include "LiveStreamAnimationFwd.h"
#include "LiveStreamAnimationHandle.h"
#include "Serialization/BitReader.h"
#include "Serialization/BitWriter.h"
#include "LiveStreamAnimationDataHandler.generated.h"

/**
 * LiveStreamAnimationDataHandlers are abstract classes that can be used to provide various
 * types of streaming animation data.
 *
 * Handlers can be defined in any number of modules or plugins, and just need to be added to
 * ULiveStreamAnimationSettings::ConfiguredDataHandlers through Project Settings or INI files.
 *
 * When a LiveStreamAnimationSubsystem instance is created, it will determine which Data Handlers should
 * be used based on ULiveStreamAnimationSettings::ConfiguredDataHandlers.
 *
 * If the subsystem is enabled, a new Handler Instance will be created for each of the configured classes.
 *
 * To ensure things work correctly with PIE, global or static data *should not be used* in the Handlers,
 * with the exception of configuration data.
 *
 * @see LSALiveLink as an example implementation.
 */
UCLASS(MinimalAPI, DisplayName = "Live Stream Animation Data Handler", Transient, Config=Engine, Category="Live Stream Animation", Abstract)
class ULiveStreamAnimationDataHandler : public UObject
{
	GENERATED_BODY()

public:

	// @see OnStartup.
	void Startup(ULiveStreamAnimationSubsystem* InOwningSubsystem, const uint32 InAssignedId);

	// @see OnShutdown.
	void Shutdown();

	LIVESTREAMANIMATION_API ELiveStreamAnimationRole GetRole() const;

	/**
	 * Used to send data to our server to be forwarded on to connected clients.
	 * This won't do anything unless the current AnimationRole is set to Tracker.
	 *
	 * @param PacketData	The buffer of data we need to send.
	 * @param bReliable		Whether or not the packet should be sent reliably.
	 */
	LIVESTREAMANIMATION_API bool SendPacketToServer(TArray<uint8>&& PacketData, const bool bReliable);

	/**
	 * Called when a new instance of this Data Handler has been instantiated
	 * and the owning Live Stream Animation Subsystem wants the handler to start handling data.
	 */
	LIVESTREAMANIMATION_API virtual void OnStartup() PURE_VIRTUAL(ULiveStreamAnimationDataHandler::OnStartup, );

	/**
	 * Called when the owning Live Stream Animation Subsystem wants the handler to
	 * stop handling data.
	 */
	LIVESTREAMANIMATION_API virtual void OnShutdown() PURE_VIRTUAL(ULiveStreamAnimationDataHandler::OnShutdown, );

	/**
	 * Called when the owning Live Stream Animation Subsystem has received a packet of data for
	 * this handler.
	 *
	 * @param ReceivedPacket	The LiveStreamAnimation packet containing data for this handler.
	 */
	LIVESTREAMANIMATION_API virtual void OnPacketReceived(const TArrayView<const uint8> ReceivedPacket) PURE_VIRTUAL(ULiveStreamAnimationDataHandler::OnPacketReceived, );

	/**
	 * Called when the owning Live Stream Animation Subsystem has its Animation Role changed.
	 *
	 * @param NewRole	The new Animation Role.
	 */
	LIVESTREAMANIMATION_API virtual void OnAnimationRoleChanged(const ELiveStreamAnimationRole NewRole) PURE_VIRTUAL(ULiveStreamAnimationDataHandler::OnAnimationRoleChanged, );

	/**
	 * Called on the server whenever a new connection is added so we can gather any necessary
	 * data the new client will need to properly received and handle data.
	 *
	 * Note: It's usually not advisable to send *all* animation data that's ever been received,
	 *		but instead just the minimum set of data needed to receive new animation frames
	 *		(like skeleton data, etc.)
	 *
	 * @param	An array of individual packets that need to be sent to the client, or an empty
	 *			array if no JIP data is needed.
	 */
	LIVESTREAMANIMATION_API virtual void GetJoinInProgressPackets(TArray<TArray<uint8>>& OutPackets) PURE_VIRTUAL(ULiveStreamAnimationDataHandler::GetJoinInProgressPackets, );

private:

	UPROPERTY(Transient)
	ULiveStreamAnimationSubsystem* OwningSubsystem;

	uint32 PacketType;
};