// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SimModule/ThrusterModule.h"
#include "SimModule/SimModuleTree.h"
#include "VehicleUtility.h"

#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_DISABLE_OPTIMIZATION
#endif

namespace Chaos
{

	FThrusterSimModule::FThrusterSimModule(const FThrusterSettings& Settings)
		: TSimModuleSettings<FThrusterSettings>(Settings)
	{

	}

	void FThrusterSimModule::Simulate(float DeltaTime, const FAllInputs& Inputs, FSimModuleTree& VehicleModuleSystem)
	{
		// applies continuous force
		AddLocalForceAtPosition(FVector(Setup().MaxThrustForce * Inputs.Throttle, 0, 0), Setup().ForceOffset);

	}

} // namespace Chaos

#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_ENABLE_OPTIMIZATION
#endif
