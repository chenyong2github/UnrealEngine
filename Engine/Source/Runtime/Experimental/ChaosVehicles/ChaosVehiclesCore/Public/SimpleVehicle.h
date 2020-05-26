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
