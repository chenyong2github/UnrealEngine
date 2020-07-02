// Copyright Epic Games, Inc. All Rights Reserved.

#include "VehicleUtility.h"
#include "HAL/PlatformTime.h"

#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_DISABLE_OPTIMIZATION
#endif

namespace Chaos
{
	FTimeAndDistanceMeasure::FTimeAndDistanceMeasure(const FString& DescriptionIn, float InitialVelocityIn, float TargetVelocityIn, float TargetDistanceIn)
	{
		Description = DescriptionIn;
		InitialVelocityMPH = InitialVelocityIn;
		FinalTargetVelocityMPH = TargetVelocityIn;
		FinalTargetDistanceMiles = TargetDistanceIn;
		Reset();
	}

	void FTimeAndDistanceMeasure::Reset()
	{
		PreStartConditionsMet = false;
		StartConditionsMet = false;
		MeasurementComplete = false;
		VelocityResultMPH = 0.f;
		DistanceResultMiles = 0.f;
		TimeResultSeconds = 0.f;
	}

	void FTimeAndDistanceMeasure::Update(float DeltaTime, const FVector& CurrentLocation, float CurrentVelocity)
	{
		if (MeasurementComplete)
		{
			return;
		}

		float CurrentVelocityMPH = CmSToMPH(CurrentVelocity);
		float Tolerance = 0.1f;

		if (!PreStartConditionsMet)
		{
			if (FinalTargetDistanceMiles || FinalTargetVelocityMPH > InitialVelocityMPH)
			{
				if (CurrentVelocityMPH < (InitialVelocityMPH + Tolerance))
				{
					PreStartConditionsMet = true;
				}
			}
			else
			{
				if (CurrentVelocityMPH >= InitialVelocityMPH)
				{
					PreStartConditionsMet = true;
				}
			}

			return;
		}

		if (!StartConditionsMet)
		{
			if (FinalTargetDistanceMiles || FinalTargetVelocityMPH > InitialVelocityMPH)
			{
				if (CurrentVelocityMPH >= (InitialVelocityMPH + Tolerance))
				{
					StartConditionsMet = true;
					InitialTime = FPlatformTime::Seconds();
					InitialLocation = CurrentLocation;
				}
			}
			else
			{
				if (CurrentVelocityMPH < InitialVelocityMPH)
				{
					StartConditionsMet = true;
					InitialTime = FPlatformTime::Seconds();
					InitialLocation = CurrentLocation;
				}
			}

			return;
		}

		if (FinalTargetDistanceMiles)
		{
			// distance measure
			float DistanceTravelledMiles = CmToMiles((CurrentLocation - InitialLocation).Size());
			if (DistanceTravelledMiles > FinalTargetDistanceMiles)
			{
				MeasurementComplete = true;
			}
		}
		else if (FinalTargetVelocityMPH > InitialVelocityMPH)
		{
			// acceleration measure
			if (CurrentVelocityMPH >= FinalTargetVelocityMPH)
			{
				MeasurementComplete = true;
			}
		}
		else
		{
			// deceleration measure
			if (CurrentVelocityMPH <= FinalTargetVelocityMPH)
			{
				MeasurementComplete = true;
			}

		}

		if (MeasurementComplete)
		{
			VelocityResultMPH = CurrentVelocityMPH;
			DistanceResultMiles = CmToMiles((CurrentLocation - InitialLocation).Size());
			TimeResultSeconds = (float)(FPlatformTime::Seconds() - InitialTime);
		}
	}

	FString FTimeAndDistanceMeasure::ToString() const
	{
		return FString::Printf(TEXT("%s   Time: %1.2f Sec,   Dist: %1.3f Miles,   Speed: %3.1f MPH")
			, *Description, TimeResultSeconds, DistanceResultMiles, VelocityResultMPH);
	}

	FPerformanceMeasure::FPerformanceMeasure() : IsEnabledThisFrame(false)
	{
		FTimeAndDistanceMeasure ZeroToThirtyMPH("0 to 30 MPH", 0.f, 30.f, 0.f);
		PerformanceMeasure.Add(ZeroToThirtyMPH);

		FTimeAndDistanceMeasure ZeroToSixtyMPH("0 to 60 MPH", 0.f, 60.f, 0.f);
		PerformanceMeasure.Add(ZeroToSixtyMPH);

		FTimeAndDistanceMeasure QuarterMile("Quarter Mile Drag", 0.f, 0.f, 0.25f);
		PerformanceMeasure.Add(QuarterMile);

		FTimeAndDistanceMeasure ThirtyToZeroMPH("30 to 0 MPH", 30.f, 0.f, 0.f);
		PerformanceMeasure.Add(ThirtyToZeroMPH);

		FTimeAndDistanceMeasure SixtyToZeroMPH("60 to 0 MPH", 60.f, 0.f, 0.f);
		PerformanceMeasure.Add(SixtyToZeroMPH);
	}

} // namespace Chaos

#if VEHICLE_DEBUGGING_ENABLED
PRAGMA_ENABLE_OPTIMIZATION
#endif
