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
	 *	Simple Normally Aspirated Engine
	 *  Output defined by a single torque curve over the rev range
	 *	No turbo/ turbo lag
	 *
	 * #todo: proper default values
	 * #todo: replace hardcoded graph with curve data
	 */

	struct CHAOSVEHICLESCORE_API FSimpleEngineConfig
	{
		FSimpleEngineConfig()
			: MaxTorque(0.f)
			, MaxRPM(6000)
			, EngineIdleRPM(1200)
			, EngineBrakeEffect(0.2f)
		{

			// #todo: this is a hard coded graph, use one from data setup
			for (float x = -1.f; x <= 1.0f; x += 0.1f)
			{
				float Value = (1.f - (x * x)) * 0.5f + 0.5f;
				TorqueCurve.Add(Value);
			}
			//TorqueCurve.Add(0.f);
		}

		// #todo: want something like UCurveFloat / FRuntimeFloatCurve / FTorqueCurveEditor
		TArray<float> TorqueCurve;	// [Normalized 0..1] Need some low level curve representation compatible with UCurveFloat??
		float MaxTorque;			// [N.m]
		uint16 MaxRPM;				// [RPM]
		uint16 EngineIdleRPM; 		// [RPM]			
		float EngineBrakeEffect;	// [0..1]
	};

	class CHAOSVEHICLESCORE_API FSimpleEngineSim : public TVehicleSystem<FSimpleEngineConfig>
	{
	public:

		FSimpleEngineSim(const FSimpleEngineConfig* StaticDataIn);

		/** Pass in the throttle position to the engine */
		void SetThrottle(float InThrottle)
		{
			FVehicleUtility::ClampNormalRange(InThrottle);
			ThrottlePosition = InThrottle;
		}

		/** When the wheels are in contact with the ground and clutch engaged then the load 
		 * on the engine from the wheels determines the engine speed. With no clutch simulation
		 * just setting the engine RPM directly to match the wheel speed.
		 */
		void SetEngineRPM(float InEngineRPM)
		{
			EngineRPM = FMath::Clamp(FMath::Abs(InEngineRPM), (float)Setup().EngineIdleRPM, (float)Setup().MaxRPM);
		}

		float GetEngineTorque()
		{
			return ThrottlePosition * GetTorqueFromRPM();
		}

		float GetTorqueFromRPM(bool LimitToIdle = true)
		{
			return GetTorqueFromRPM(EngineRPM);
		}

		/* get torque value from torque curve based on input RPM */
		float GetTorqueFromRPM(float RPM, bool LimitToIdle = true);

		/** get the Engine speed in radians/sec */
		float GetEngineSpeed() const
		{
			check(false);
			return 0;// Omega;
		}

		/** get the Engine speed in Revolutions Per Minute */
		float GetEngineRPM() const
		{
			return EngineRPM;
		}


		/** 
		 * Simulate - NOP at the moment
		 */
		void Simulate(float DeltaTime);

	protected:

		float ThrottlePosition; // [0..1 Normalized position]
		float EngineRPM;		// current RPM
		float DriveTorque;		// current torque [N.m]

	//	float Tce; // torque from clutch/transmission system
	//	float Omega;
		float EngineIdleSpeed;
	};

} // namespace Chaos

#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_ENABLE_OPTIMIZATION
#endif
