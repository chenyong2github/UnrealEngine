// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VehicleUtility.h"

#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_DISABLE_OPTIMIZATION
#endif

/**
 * #todo: 
 * -proper suspension setup for resting position - decide on parameters i.e. use SuspensionMaxRaise/SuspensionMaxDrop??
 * -natural frequency stuff
 * -defaults
 */

namespace Chaos
{

	struct FSimpleSuspensionConfig
	{
		FSimpleSuspensionConfig()
			: MaxLength(0.f)
			, MinLength(0.f)
			, SuspensionForceOffset(FVector::ZeroVector)
			, SuspensionMaxRaise(0.f)
			, SuspensionMaxDrop(0.f)
			, SpringRate(1.f)
			, SpringPreload(0.5f)
			, CompressionDamping(0.9f)
			, ReboundDamping(0.9f)
			, Swaybar(0.5f)
		{
			// #todo: setup suspension properly
			float Gravity = 980.0f;
			float VehicleMass = 1600.0f;
			float NumWheels = 4;
			SpringRate = 1.5f * VehicleMass * Gravity / NumWheels;
			SpringPreload = 0.3f * SpringRate;
			CompressionDamping = 1000.0f;
			ReboundDamping = 1000.0f;
			MaxLength = 20.0f;
		}

		float MaxLength;
		float MinLength;

		FVector SuspensionForceOffset;
		float SuspensionMaxRaise;
		float SuspensionMaxDrop;
		float SpringRate;			// spring constant
		float SpringPreload;		// Amount of Spring force (independent spring movement)
		float CompressionDamping;	// limit compression speed
		float ReboundDamping;		// limit rebound speed
		float Swaybar;				// Anti-roll bar

		//float CastorAngle;		// inclination of the steering axes from the vertical. [degrees, +ve inclined towards rear of vehicle]
		//float CamberAngle;		// inward or outward tilt of wheel at top relative to bottom [degrees, +ve leaning out]
		//float ToeOutAngle;		// Worth worrying about?, here for completeness
	};


	class FSimpleSuspensionSim : public TVehicleSystem<FSimpleSuspensionConfig>
	{
	public:
		FSimpleSuspensionSim(const FSimpleSuspensionConfig* SetupIn)
			: TVehicleSystem<FSimpleSuspensionConfig>(SetupIn)
			, DesiredLength(0.f)
			, CurrentLength(0.f)
			, LocalVelocity(FVector::ZeroVector)
			, SuspensionForce(0.f)
		{
		}

// Inputs

		/** Set suspension length after determined from raycast */
		void SetDesiredLength(float InLength)
		{
			DesiredLength = InLength;
		}

		/** set local velocity at suspension position */
		void SetLocalVelocity(FVector InVelocity)
		{
			LocalVelocity = InVelocity;
		}

// Outputs

		float GetSpringLength()
		{
			return CurrentLength;
		}

		float GetSuspensionForce()
		{
			return SuspensionForce;
		}

// Simulation

		void Simulate(float DeltaTime)
		{
			// use correct damping rate for direction of suspension movement
			float Damping = (DesiredLength < CurrentLength) ? Setup().CompressionDamping : Setup().ReboundDamping;
			
			//float SuspensionVelocity = (DesiredLength - CurrentLength) * Setup().MaxLength / DeltaTime;

			float InvLength = 1.0f - DesiredLength; // Normalized - range [0..1]

			// damping movement at spring
			float DampingEffect = LocalVelocity.Z * Damping/* / DeltaTime*/;
			float SpringForce = InvLength * Setup().SpringRate;

			SuspensionForce = Setup().SpringPreload + SpringForce - DampingEffect;

			CurrentLength = DesiredLength;
		}

	protected:

		float DesiredLength;	// Normalized Length [0..1]
		float CurrentLength;	// Normalized Length [0..1]
		FVector LocalVelocity;
		float SuspensionForce;	// Suspension Force calculated
	};

} // namespace Chaos

#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_ENABLE_OPTIMIZATION
#endif
