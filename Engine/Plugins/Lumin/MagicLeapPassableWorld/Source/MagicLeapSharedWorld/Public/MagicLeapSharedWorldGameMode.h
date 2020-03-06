// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/GameMode.h"
#include "MagicLeapSharedWorldTypes.h"
#include "MagicLeapSharedWorldGameMode.generated.h"

class AMagicLeapSharedWorldPlayerController;
class AMagicLeapSharedWorldGameState;

/**
 * Game mode to use for shared world experiences on MagicLeap capable XR devices.
 *
 * Requires the game state class to be or derived from AMagicLeapSharedWorldGameState.
 * Requires the player controller class to be or derived from AMagicLeapSharedWorldPlayerController.
 */
UCLASS(BlueprintType)
class MAGICLEAPSHAREDWORLD_API AMagicLeapSharedWorldGameMode : public AGameMode
{
	GENERATED_UCLASS_BODY()

public:
	/**
	 * Determine which pins should be used for shared world aligment of all clients.
	 *
	 * The result can be set to AMagicLeapSharedWorldGameMode::SharedWorldData.
	 * Calling SendSharedWorldDataToClients() will replicate these local pins to all clients.
	 * Calling SelectChosenOne() can select a certain client to be pseudo-authoritative i.e. all other clients will align to its coordinate space. 
	 * This function is a BlueprintNativeEvent, override to implement a custom behavior.
	 * Default implementation -> simple selection of pins common in all connected non-spectator clients,
	 * with their confidence value thresholded by PinSelectionConfidenceThreshold
	 * @param NewSharedWorldData Output param containing the list of common pins.
	 * @see SendSharedWorldDataToClients
	 * @see SelectChosenOne
	 * @see PinSelectionConfidenceThreshold
	 */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, BlueprintAuthorityOnly, Category="AR Shared World|Magic Leap")
	void DetermineSharedWorldData(FMagicLeapSharedWorldSharedData& NewSharedWorldData);

	/**
	 * Select a certain client to be the "chosen-one" or pseudo-authoritative for this shared world session.
	 * 
	 * Means that this client is responsible for sending the list of it's pin transforms (in world space so its own alignment is unaffected)
	 * to the server via AMagicLeapSharedWorldPlayerController::ServerSetAlignmentTransforms().
	 * Everyone will align to this client's coordinate space using those pin transforms.
	 * This function is a BlueprintNativeEvent, override to implement a custom behavior.
	 * Default implementation -> client with best confidence values for selected shared pins.
	 * @return Owning player controller of the client selected as the chosen one.
	 */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, BlueprintAuthorityOnly, Category="AR Shared World|Magic Leap")
	void SelectChosenOne();

	/**
	 * Replicate pins common among all clients via AMagicLeapSharedWorldGameState.
	 * 
	 * These pins can be selected by calling DetermineSharedWorldData()
	 * @return true if game state was valid, false otherwise
	 * @see DetermineSharedWorldData
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="AR Shared World|Magic Leap")
	bool SendSharedWorldDataToClients();

	/** Cache pins common among all clients */
	UPROPERTY(BlueprintReadWrite, Category="AR Shared World|Magic Leap")
	FMagicLeapSharedWorldSharedData SharedWorldData;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMagicLeapOnNewLocalDataFromClients);

	/** Event fired when server receives new local data from connected clients. */
	UPROPERTY(BlueprintAssignable)
	FMagicLeapOnNewLocalDataFromClients OnNewLocalDataFromClients;

	/** Confidence threshold for selecting shared Pins */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AR Shared World|Magic Leap")
	float PinSelectionConfidenceThreshold;

protected:
	virtual void Tick(float DeltaSeconds) override;
	virtual void Logout(AController* Exiting) override;
	AMagicLeapSharedWorldGameState* GetSharedWorldGameState() const;

	struct FMagicLeapPinOwnerAndState
	{
		AMagicLeapSharedWorldPlayerController* Owner;
		FMagicLeapARPinState State;
	};

	TMap<AMagicLeapSharedWorldPlayerController*, TArray<FGuid>> PlayerToLocalPins;
	TMap<FGuid, TMap<AMagicLeapSharedWorldPlayerController*, FMagicLeapARPinState>> PinToOwnersToStates;

	UPROPERTY(BlueprintReadWrite, Category="AR Shared World|Magic Leap")
	AMagicLeapSharedWorldPlayerController* ChosenOne;
};
