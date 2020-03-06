// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/PlayerController.h"
#include "MagicLeapSharedWorldTypes.h"
#include "MagicLeapSharedWorldGameState.h"
#include "MagicLeapSharedWorldPlayerController.generated.h"

UCLASS()
class MAGICLEAPSHAREDWORLD_API AMagicLeapSharedWorldPlayerController : public APlayerController
{
	GENERATED_UCLASS_BODY()

public:
	/**
	 * Updates list of pins local to a client on the server.
	 * 
	 * This makes the list of local pins available to AMagicLeapSharedWorldGameMode to determine
	 * which ones to share across all clients and use for global coordinate space alignment.
	 */
	UFUNCTION(Reliable, Server, BlueprintCallable, Category="AR Shared World|Magic Leap")
	void ServerSetLocalWorldData(const FMagicLeapSharedWorldLocalData& LocalWorldReplicationData);

	/**
	 * Marks the client ready to start sending local data to the server.
	 * Currently this function or the ::CanSendLocalDataToServer() function is not that relevant to the implementation.
	 * The data will reach the server even if ::ServerSetLocalWorldData() is called before this function.
	 */
	UFUNCTION(Reliable, Client)
	void ClientMarkReadyForSendingLocalData();

	/**
	 * Marks the client as the "chosen-one" or pseudo-authoritative for this shared world session.
	 * 
	 * Means that this client is responsible for sending the list of it's pin transforms (in world space so its own alignment is unaffected)
	 * to the server via ServerSetAlignmentTransforms(). Everyone will align to this client's coordinate space
	 * using those pin transforms. The game mode or the player controller doesnt directly use this property. It is just a helper
	 * data point to determine which client should send the alignment transforms. Apps can choose to implement their own ways
	 * to select which client should should everyone align with.
	 * @see ServerSetAlignmentTransforms
	 */
	UFUNCTION(Reliable, Client, BlueprintCallable, Category="AR Shared World|Magic Leap")
	void ClientSetChosenOne(bool bChosenOne);

	/** 
	 * Getter to check if the current client is the chosen one
	 * @return true if this client is the chosen one, false otherwise
	 * @see ClientSetChosenOne()
	 */
	UFUNCTION(BlueprintCallable, Category="AR Shared World|Magic Leap")
	bool IsChosenOne() const;

	/** 
	 * Getter to check if the current client can start sending local pins to the server
	 * @return true if it is now safe to send local data to server, false otherwise
	 * @see ServerSetLocalWorldData()
	 */
	UFUNCTION(BlueprintCallable, Category="AR Shared World|Magic Leap")
	bool CanSendLocalDataToServer() const;

	/**
	 * Sets list of transforms to be used by all clients to align coordinate spaces.
	 * 
	 * This list is redirected to AMagicLeapSharedWorldGameState which performs the replication.
	 * Bind an event to AMagicLeapSharedWorldGameState::OnAlignmentTransformsUpdated to get a
	 * notification when new alignment transforms are available.
	 * If performing shared world alignment on-the-fly (i.e. without any prior setup like in a museum app),
	 * these transforms should be sent by a single selected client. Apps can make use of the "chosen one"
	 * client for this purpose. Override AMagicLeapSharedWorldGameMode::SelectChosenOne() to select which
	 * of the connected clients should send the alignment transforms. By default the first connected client
	 * is considered the "chosen one".
	 * 
	 * @see AMagicLeapSharedWorldGameMode::SelectChosenOne()
	 * @see AMagicLeapSharedWorldGameState::OnAlignmentTransformsUpdated
	 * @see AMagicLeapSharedWorldGameState::CalculateXRCameraRootTransform()
	 */
	UFUNCTION(BlueprintCallable, Reliable, Server, Category="AR Shared World|Magic Leap")
	void ServerSetAlignmentTransforms(const FMagicLeapSharedWorldAlignmentTransforms& InAlignmentTransforms);

	/** Used by AMagicLeapSharedWorldGameMode to check if this client has updated local data. */
	bool HasNewLocalWorldData() const { return bHasNewLocalWorldData; }
	void ResetHasNewLocalWorldData() { bHasNewLocalWorldData = false; }
	const FMagicLeapSharedWorldLocalData& GetLocalWorldData() const { return LocalWorldData; }

private:
	AMagicLeapSharedWorldGameState* GetGameState();
	bool IsGameStateReady();

	bool bHasNewLocalWorldData;
	bool bIsChosenOne;
	bool bCanSendLocalData;

	FMagicLeapSharedWorldLocalData LocalWorldData;
};
