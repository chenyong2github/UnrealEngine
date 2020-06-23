// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ChaosVehicleMovementComponent.h"
#include "PhysicsPublic.h"

class UChaosTireConfig;
class UChaosVehicleMovementComponent;


// #todo: We probably want this to be associated with a physics solver. So simulation can be isolated if desired.
// Physics scene based currently

/**
 * Manages vehicles and tire surface data for all scenes
 */
class CHAOSVEHICLES_API FChaosVehicleManager
{
public:

	// Updated when vehicles need to recreate their physics state.
	// Used when designer tweaks values while the game is running.
	static uint32 VehicleSetupTag;

	FChaosVehicleManager(FPhysScene* PhysScene);
	~FChaosVehicleManager();

	/**
	 * Register a Physics vehicle for processing
	 */
	void AddVehicle( TWeakObjectPtr<UChaosVehicleMovementComponent> Vehicle );

	/**
	 * Unregister a Physics vehicle from processing
	 */
	void RemoveVehicle( TWeakObjectPtr<UChaosVehicleMovementComponent> Vehicle );

	/**
	 * Update vehicle data before the scene simulates
	 */
	void Update(FPhysScene* PhysScene, float DeltaTime);
	
	/**
	 * Update vehicle tuning and other state such as input 
	 */
	void PreTick(FPhysScene* PhysScene, float DeltaTime);

	/** Detach this vehicle manager from a FPhysScene (remove delegates, remove from map etc) */
	void DetachFromPhysScene(FPhysScene* PhysScene);

#if WITH_CHAOS
	/** Get Physics Scene */
	FPhysScene_Chaos& GetScene() const { return Scene; }
#endif

	/** Find a vehicle manager from an FPhysScene */
	static FChaosVehicleManager* GetVehicleManagerFromScene(FPhysScene* PhysScene);

	/** Gets a transient default TireConfig object */
	static UChaosTireConfig* GetDefaultTireConfig();

private:

	/** Map of physics scenes to corresponding vehicle manager */
	static TMap<FPhysScene*, FChaosVehicleManager*> SceneToVehicleManagerMap;

#if WITH_CHAOS
	// The physics scene we belong to
	FPhysScene_Chaos& Scene;
#endif

	// All instanced vehicles
	TArray<TWeakObjectPtr<UChaosVehicleMovementComponent>> Vehicles;

	FDelegateHandle OnPhysScenePreTickHandle;
	FDelegateHandle OnPhysSceneStepHandle;

};
