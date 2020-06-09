// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVehicleManager.h"
#include "UObject/UObjectIterator.h"
#include "ChaosTireConfig.h"

#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Physics/PhysicsFiltering.h"
#include "Physics/PhysicsInterfaceCore.h"
#include "VehicleContactModification.h"


DECLARE_STATS_GROUP(TEXT("ChaosVehicleManager"), STATGROUP_ChaosVehicleManager, STATGROUP_Advanced);
DECLARE_CYCLE_STAT(TEXT("VehicleSuspensionRaycasts"), STAT_ChaosVehicleManager_VehicleSuspensionRaycasts, STATGROUP_ChaosVehicleManager);
DECLARE_CYCLE_STAT(TEXT("UpdatePhysicsVehicles"), STAT_ChaosVehicleManager_UpdatePhysicsVehicles, STATGROUP_ChaosVehicleManager);
DECLARE_CYCLE_STAT(TEXT("TickVehicles"), STAT_ChaosVehicleManager_TickVehicles, STATGROUP_ChaosVehicleManager);
DECLARE_CYCLE_STAT(TEXT("VehicleManagerUpdate"), STAT_ChaosVehicleManager_Update, STATGROUP_ChaosVehicleManager);
DECLARE_CYCLE_STAT(TEXT("PretickVehicles"), STAT_ChaosVehicleManager_PretickVehicles, STATGROUP_Physics);


TMap<FPhysScene*, FChaosVehicleManager*> FChaosVehicleManager::SceneToVehicleManagerMap;
uint32 FChaosVehicleManager::VehicleSetupTag = 0;

FChaosVehicleManager::FChaosVehicleManager(FPhysScene* PhysScene)
#if WITH_CHAOS
	: Scene(PhysScene->GetScene())
#endif
{
	// Set up delegates
	OnPhysScenePreTickHandle = PhysScene->OnPhysScenePreTick.AddRaw(this, &FChaosVehicleManager::PreTick);
	OnPhysSceneStepHandle = PhysScene->OnPhysSceneStep.AddRaw(this, &FChaosVehicleManager::Update);

	// Add to Scene-To-Manager map
	FChaosVehicleManager::SceneToVehicleManagerMap.Add(PhysScene, this);

#if WITH_CHAOS
	FPhysScene::CollisionModifierCallback = FVehicleContactModificationFactory::Create();
#endif
}

void FChaosVehicleManager::DetachFromPhysScene(FPhysScene* PhysScene)
{
	PhysScene->OnPhysScenePreTick.Remove(OnPhysScenePreTickHandle);
	PhysScene->OnPhysSceneStep.Remove(OnPhysSceneStepHandle);

	FChaosVehicleManager::SceneToVehicleManagerMap.Remove(PhysScene);
}

FChaosVehicleManager::~FChaosVehicleManager()
{
	while( Vehicles.Num() > 0 )
	{
		RemoveVehicle( Vehicles.Last() );
	}
}

FChaosVehicleManager* FChaosVehicleManager::GetVehicleManagerFromScene(FPhysScene* PhysScene)
{
	FChaosVehicleManager* Manager = nullptr;
	FChaosVehicleManager** ManagerPtr = SceneToVehicleManagerMap.Find(PhysScene);
	if (ManagerPtr != nullptr)
	{
		Manager = *ManagerPtr;
	}
	return Manager;
}

void FChaosVehicleManager::AddVehicle( TWeakObjectPtr<UChaosVehicleMovementComponent> Vehicle )
{
	check(Vehicle != NULL);
	check(Vehicle->PVehicle);

	Vehicles.Add( Vehicle );
}

void FChaosVehicleManager::RemoveVehicle( TWeakObjectPtr<UChaosVehicleMovementComponent> Vehicle )
{
	check(Vehicle != NULL);
	check(Vehicle->PVehicle);

	int32 RemovedIndex = Vehicles.Find(Vehicle);

	Vehicles.Remove( Vehicle );

	if (Vehicle->PVehicle.IsValid())
	{
		Vehicle->PVehicle.Reset(nullptr);
	}
}

void FChaosVehicleManager::Update(FPhysScene* PhysScene, float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_ChaosVehicleManager_Update);

	if (Vehicles.Num() == 0 )
	{
		return;
	}

	//	Suspension raycasts
	{
		SCOPE_CYCLE_COUNTER(STAT_ChaosVehicleManager_VehicleSuspensionRaycasts);
		// #todo: batch all the vehicle raycasts here
		// #todo: possibly cache raycasts. Seen x4 factor improvement in scene query perf in previous games
	}
	
	// Tick vehicles
	{
		SCOPE_CYCLE_COUNTER(STAT_ChaosVehicleManager_TickVehicles);
		for (int32 i = Vehicles.Num() - 1; i >= 0; --i)
		{
			Vehicles[i]->TickVehicle(DeltaTime);
		}
	}

}

void FChaosVehicleManager::PreTick(FPhysScene* PhysScene, float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_ChaosVehicleManager_PretickVehicles);

	for (int32 i = 0; i < Vehicles.Num(); ++i)
	{
		Vehicles[i]->PreTick(DeltaTime);
	}
}
