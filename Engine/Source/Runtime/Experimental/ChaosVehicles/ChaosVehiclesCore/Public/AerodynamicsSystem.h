// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VehicleSystemTemplate.h"
#include "VehicleUtility.h"

#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_DISABLE_OPTIMIZATION
#endif

namespace Chaos
{
	/*
	*	Simple Aerodynamics - calculates drag and down-force/lift-force for a given speed
	*
	*	#todo: Add options for drafting/Slipstreaming effect
	*	#todo: Proper defaults
	*/

	struct FSimpleAerodynamicsConfig
	{
		FSimpleAerodynamicsConfig()
			: AreaMetresSquared(2.0f)
			, DragCoefficient(0.1f)
			, DownforceCoefficient(0.1f)
		{

		}

	//	FVector Offet;				// local offset relative to CM
		float AreaMetresSquared;	// [meters squared]
		float DragCoefficient;		// always positive
		float DownforceCoefficient;	// positive for downforce, negative lift
								
	};


	class FSimpleAerodynamicsSim : public TVehicleSystem<FSimpleAerodynamicsConfig>
	{
	public:
		FSimpleAerodynamicsSim(const FSimpleAerodynamicsConfig* SetupIn)
			: TVehicleSystem< FSimpleAerodynamicsConfig>(SetupIn)
			, DensityOfMedium(RealWorldConsts::AirDensity())
		{
			// pre-calculate static values
			EffectiveDragConstant = 0.5f * Setup().AreaMetresSquared * Setup().DragCoefficient;
			EffectiveLiftConstant = 0.5f * Setup().AreaMetresSquared * Setup().DownforceCoefficient;
		}


		/** set the density of the medium through which you are traveling Air/Water, etc */
		void SetDensityOfMedium(float DensityIn)
		{
			DensityOfMedium = DensityIn;
		}

		/** get the drag force generated at the given velocity */
		float GetDragForceFromVelocity(float VelocityIn)
		{
			return -EffectiveDragConstant * DensityOfMedium * VelocityIn * VelocityIn;
		}

		/** get the lift/down-force generated at the given velocity */
		float GetLiftForceFromVelocity(float VelocityIn)
		{
			return -EffectiveLiftConstant * DensityOfMedium * VelocityIn * VelocityIn;
		}

		/** Get the drag and down forces combined in a 3D vector, drag on X-axis, down-force on Z-axis*/
		FVector GetCombinedForces(float VelocityIn)
		{
			// -ve as forces applied in opposite direction to velocity
			float CommonSum = -DensityOfMedium * VelocityIn* VelocityIn;
			FVector CombinedForces(EffectiveDragConstant * CommonSum, 0.f, EffectiveLiftConstant * CommonSum);
			return CombinedForces;
		}

	protected:
		float DensityOfMedium;
		float EffectiveDragConstant;
		float EffectiveLiftConstant;

	};

} // namespace Chaos

#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_ENABLE_OPTIMIZATION
#endif
