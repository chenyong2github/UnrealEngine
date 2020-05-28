// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVehicleWheel.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Vehicles/TireType.h"
#include "GameFramework/PawnMovementComponent.h"
#include "ChaosTireConfig.h"
#include "ChaosVehicleManager.h"
#include "ChaosWheeledVehicleMovementComponent.h"

#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_DISABLE_OPTIMIZATION
#endif


UChaosVehicleWheel::UChaosVehicleWheel(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CollisionMeshObj(TEXT("/Engine/EngineMeshes/Cylinder"));
	CollisionMesh = CollisionMeshObj.Object;

	WheelRadius = 30.0f;
	WheelWidth = 10.0f;
	bAutoAdjustCollisionSize = true;
	WheelMass = 20.0f;
	CheatFrictionForce = 2.0f;
	bAffectedByHandbrake = true;
	MaxSteerAngle = 50.0f;
	MaxBrakeTorque = 1500.f;
	MaxHandBrakeTorque = 3000.f;

	//DampingRate = 0.25f;
	//LatStiffMaxLoad = 2.0f;
	//LatStiffValue = 17.0f;
	//LongStiffValue = 1000.0f;
	SpringRate = 588000;

	SpringRate = 1000.0f;
	SpringPreload = 0.3f;
	SuspensionForceOffset = FVector::ZeroVector;
	SuspensionMaxRaise = 10.0f;
	SuspensionMaxDrop = 10.0f;
	CompressionDamping = 0.f;
	ReboundDamping = 0.f;
	//SuspensionNaturalFrequency = 7.0f;
	//SuspensionDampingRatio = 1.0f;
	SweepType = ESweepType::SimpleAndComplexSweep;
}


FChaosVehicleManager* UChaosVehicleWheel::GetVehicleManager() const
{
	UWorld* World = GEngine->GetWorldFromContextObject(VehicleSim, EGetWorldErrorMode::LogAndReturnNull);
	return World ? FChaosVehicleManager::GetVehicleManagerFromScene(World->GetPhysicsScene()) : nullptr;
}


float UChaosVehicleWheel::GetSteerAngle() const
{
	return MaxSteerAngle; // #todo: should be current steering angle
}

float UChaosVehicleWheel::GetRotationAngle() const
{
	float RotationAngle = -1.0f * FMath::RadiansToDegrees(VehicleSim->PVehicle->Wheels[WheelIndex].GetAngularPosition());
	ensure(!FMath::IsNaN(RotationAngle));

	return RotationAngle;
}

float UChaosVehicleWheel::GetSuspensionOffset() const
{
	//float SuspensionOffset = VehicleSim->PVehicle->Suspension[WheelIndex].GetSpringLength();

	return SuspensionOffset; // TEMP
}

bool UChaosVehicleWheel::IsInAir() const
{
	return InAir;
}


void UChaosVehicleWheel::Init( UChaosWheeledVehicleMovementComponent* InVehicleSim, int32 InWheelIndex )
{
	check(InVehicleSim);
	check(InVehicleSim->Wheels.IsValidIndex(InWheelIndex));

	VehicleSim = InVehicleSim;
	WheelIndex = InWheelIndex;

//#if WITH_PHYSX_VEHICLES
//	WheelShape = NULL;
//
//	FChaosVehicleManager* VehicleManager = FChaosVehicleManager::GetVehicleManagerFromScene(VehicleSim->GetWorld()->GetPhysicsScene());
//	SCOPED_SCENE_READ_LOCK(VehicleManager->GetScene());
//
//	const int32 WheelShapeIdx = VehicleSim->PVehicle->mWheelsSimData.getWheelShapeMapping( WheelIndex );
//	check(WheelShapeIdx >= 0);
//
//	VehicleSim->PVehicle->getRigidDynamicActor()->getShapes( &WheelShape, 1, WheelShapeIdx );
//	check(WheelShape);
//#endif // WITH_PHYSX

	Location = GetPhysicsLocation();
	OldLocation = Location;
}

void UChaosVehicleWheel::Shutdown()
{
//	WheelShape = NULL;
}

FChaosWheelSetup& UChaosVehicleWheel::GetWheelSetup()
{
	return VehicleSim->WheelSetups[WheelIndex];
}

void UChaosVehicleWheel::Tick( float DeltaTime )
{
	OldLocation = Location;
	Location = GetPhysicsLocation();
	Velocity = ( Location - OldLocation ) / DeltaTime;
}

FVector UChaosVehicleWheel::GetPhysicsLocation()
{
	return Location;
}

#if WITH_EDITOR

void UChaosVehicleWheel::PostEditChangeProperty( FPropertyChangedEvent& PropertyChangedEvent )
{
	Super::PostEditChangeProperty( PropertyChangedEvent );

	// Trigger a runtime rebuild of the Physics vehicle
	FChaosVehicleManager::VehicleSetupTag++;
}

#endif //WITH_EDITOR


UPhysicalMaterial* UChaosVehicleWheel::GetContactSurfaceMaterial()
{
	UPhysicalMaterial* PhysMaterial = NULL;

	// #todo:implement
	return PhysMaterial;
}


#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_ENABLE_OPTIMIZATION
#endif



