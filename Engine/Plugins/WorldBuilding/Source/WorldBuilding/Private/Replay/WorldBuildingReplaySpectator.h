// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/PlayerController.h"
#include "Replay/WorldBuildingReplaySpectatorHUD.h"
#include "WorldBuildingReplaySpectator.generated.h"

/** 
 * AWorldBuildingReplaySpectator can be used as the replay player controller when setting up a game mode. 
 * 
 * Allows changing view point based on World Partition replay streaming sources.
 */
UCLASS(config = Game)
class AWorldBuildingReplaySpectator : public APlayerController
{
	GENERATED_UCLASS_BODY()

public:
	virtual void GetPlayerViewPoint(FVector& out_Location, FRotator& out_Rotation) const override;
	virtual void SpawnDefaultHUD() override;
	virtual void TickActor(float DeltaSeconds, ELevelTick TickType, FActorTickFunction& ThisTickFunction) override;
	virtual void SetupInputComponent() override;

private:
	void OnNextViewPoint();
	void OnPreviousViewPoint();

	UPROPERTY(EditAnywhere, Category = HUD)
	TSubclassOf<AWorldBuildingReplaySpectatorHUD> HUDClass;
};