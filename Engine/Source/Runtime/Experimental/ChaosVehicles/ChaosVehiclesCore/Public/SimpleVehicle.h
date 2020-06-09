// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VehicleUtility.h"
#include "EngineSystem.h"
#include "TransmissionSystem.h"
#include "SuspensionSystem.h"
#include "WheelSystem.h"
#include "AerodynamicsSystem.h"
//#include "AerofoilSystem.h"
//#include "FuelSystem.h"
//#include "BallastSystem.h"

#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_DISABLE_OPTIMIZATION
#endif

namespace Chaos
{

class IVehicleInterface
{
public:
//	virtual void Simulate(float DeltaTime) = 0;
};

/**
 * This class is currently just a container for the simulation components used by a wheeled vehicle
 * Keeping all the physics systems together and accessible through the one vehicle class
 */
class FSimpleWheeledVehicle : public IVehicleInterface
{
public:
	FSimpleWheeledVehicle()
	{

	}

	virtual ~FSimpleWheeledVehicle()
	{

	}

	/*void Simulate(float DeltaTime) override
	{
	}*/

	bool IsValid()
	{
		return (Transmission.Num() == 1) && (Engine.Num() == 1) && (Aerodynamics.Num() == 1);
	}

	FSimpleEngineSim& GetEngine()
	{
		check(Engine.Num() == 1);
		return Engine[0];
	}

	FSimpleTransmissionSim& GetTransmission()
	{
		check(Transmission.Num() == 1);
		return Transmission[0];
	}

	FSimpleWheelSim& GetWheel(int WheelIdx)
	{
		check(WheelIdx < Wheels.Num());
		return Wheels[WheelIdx];
	}

	FSimpleSuspensionSim& GetSuspension(int WheelIdx)
	{
		check(WheelIdx < Suspension.Num());
		return Suspension[WheelIdx];
	}

	FSimpleAerodynamicsSim& GetAerodynamics()
	{
		check(Aerodynamics.Num() == 1);
		return Aerodynamics[0];
	}

	TArray<FSimpleEngineSim> Engine;
	TArray<FSimpleTransmissionSim> Transmission;
	TArray<FSimpleWheelSim> Wheels;
	TArray<FSimpleSuspensionSim> Suspension;
	TArray<FSimpleAerodynamicsSim> Aerodynamics;
	//TArray<FSimpleAerofoilSim> Aerofoil;
	//TArray<FSimpleFuelSim> Fuel;
	//TArray<FSimpleBallastSim> Ballast;
	// turbo
	// .. 
};


} // namespace Chaos

#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_ENABLE_OPTIMIZATION
#endif
