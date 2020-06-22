// Copyright Epic Games, Inc. All Rights Reserved.

#include "EngineSystem.h"

#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_DISABLE_OPTIMIZATION
#endif

namespace Chaos
{


	FSimpleEngineSim::FSimpleEngineSim(const FSimpleEngineConfig* StaticDataIn) : TVehicleSystem<FSimpleEngineConfig>(StaticDataIn)
		, ThrottlePosition(0.f)
		, EngineRPM(0.f)
		, DriveTorque(0.f)
		//	, Tce(0.f)
		//	, Omega(0.f)
		, EngineIdleSpeed(RPMToOmega(Setup().EngineIdleRPM))
	{

	}

	float FSimpleEngineSim::GetTorqueFromRPM(float RPM, bool LimitToIdle /*= true*/)
	{
		if (RPM >= Setup().MaxRPM || Setup().MaxRPM == 0)
		{
			return 0.f;
		}

		if (LimitToIdle)
		{
			RPM = FMath::Clamp(RPM, (float)Setup().EngineIdleRPM, (float)Setup().MaxRPM);
		}

		float Step = Setup().MaxRPM / (Setup().TorqueCurve.Num() - 1);
		int StartIndex = RPM / Step;
		float NormalisedRamp = ((float)RPM - (float)StartIndex * Step) / Step;

		float NormYValue = 0.0f;
		if (StartIndex >= Setup().TorqueCurve.Num() - 1)
		{
			NormYValue = Setup().TorqueCurve[Setup().TorqueCurve.Num() - 1];
		}
		else
		{
			NormYValue = Setup().TorqueCurve[StartIndex] * (1.f - NormalisedRamp) + Setup().TorqueCurve[StartIndex + 1] * NormalisedRamp;
		}

		return NormYValue * Setup().MaxTorque;
	}

	void FSimpleEngineSim::Simulate(float DeltaTime)
	{
		//EngineIdleSpeed = RPMToOmega(Setup().EngineIdleRPM);

		//// we don't let the engine stall
		//if (Omega < EngineIdleSpeed)
		//{
		//	Omega = EngineIdleSpeed;
		//}

		//if (Omega > RPMToOmega(Setup().MaxRPM))
		//{
		//	Omega = RPMToOmega(Setup().MaxRPM);
		//}

		// EngineSpeed == Omega
		//EngineRPM = OmegaToRPM(Omega);
	}


} // namespace Chaos


#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_ENABLE_OPTIMIZATION
#endif
