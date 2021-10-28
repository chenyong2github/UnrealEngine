// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassSettings.h"
#include "MassSimulationSettings.generated.h"

#define GET_MASS_CONFIG_VALUE(a) (GetMutableDefault<UMassSimulationSettings>()->a)

class UMassSchematic;

/**
 * Implements the settings for MassSimulation
 */
UCLASS(config = Mass, defaultconfig, DisplayName = "Mass Simulation")
class MASSSIMULATION_API UMassSimulationSettings : public UMassModuleSettings
{
	GENERATED_BODY()
public:
	DECLARE_MULTICAST_DELEGATE(FOnTickSchematicChangedDelegate);

#if WITH_EDITOR
public:
	FOnTickSchematicChangedDelegate& GetOnTickSchematicChanged() { return OnTickSchematicChanged; }

protected:
	virtual void PostInitProperties() override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	void OnAssetPropertiesChanged(UMassSchematic* MassSchematic, const FPropertyChangedEvent& PropertyChangedEvent);	
#endif // WITH_EDITOR

public:
	UPROPERTY(EditDefaultsOnly, Category = "Runtime", config, meta=(DisplayName="DEPRECATED_TickSchematics"))
	TArray<TSoftObjectPtr<UMassSchematic>> TickSchematics;

	/** The desired budget in seconds allowed to do actor spawning per frame */
	UPROPERTY(EditDefaultsOnly, Category = "Runtime", config)
	double DesiredActorSpawningTimeSlicePerTick = 0.0015;

	/** The desired budget in seconds allowed to do actor destruction per frame */
	UPROPERTY(EditDefaultsOnly, Category = "Runtime", config)
	double DesiredActorDestructionTimeSlicePerTick = 0.0005;

	/** The desired budget in seconds allowed to do entity compaction per frame */
	UPROPERTY(EditDefaultsOnly, Category = "Runtime", config)
	double DesiredEntityCompactionTimeSlicePerTick = 0.005;

	/** The time to wait before retrying to spawn an actor that previously failed, default 10 seconds */
	UPROPERTY(EditDefaultsOnly, Category = "Runtime", config)
	float DesiredActorFailedSpawningRetryTimeInterval = 5.0f;

	/** The distance a failed spawned actor needs to move before we retry, default 10 meters */
	UPROPERTY(EditDefaultsOnly, Category = "Runtime", config)
	float DesiredActorFailedSpawningRetryMoveDistance = 500.0f;

protected:
#if WITH_EDITOR
	FOnTickSchematicChangedDelegate OnTickSchematicChanged;
#endif // WITH_EDITOR
};
