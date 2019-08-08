// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SolverEventFilters.h"

namespace Chaos
{

	bool FSolverCollisionEventFilter::Pass(const TCollisionData<float, 3>& InData) const
	{
		const float MinVelocitySquared = FMath::Square(Settings.MinSpeed);
		const float MinImpulseSquared = FMath::Square(Settings.MinImpulse);

		if (Settings.MinMass > 0.0f && InData.Mass1 < Settings.MinMass && InData.Mass2 < Settings.MinMass)
			return false;

		if (Settings.MinSpeed > 0.0f && InData.Velocity1.SizeSquared() < MinVelocitySquared && InData.Velocity2.SizeSquared() < MinVelocitySquared)
			return false;

		if (Settings.MinImpulse > 0.0f && InData.AccumulatedImpulse.SizeSquared() < MinImpulseSquared)
			return false;

		return true;
	}

	bool FSolverTrailingEventFilter::Pass(const TTrailingData<float, 3>& InData) const
	{
		const float MinSpeedThresholdSquared = Settings.MinSpeed * Settings.MinSpeed;

		if (Settings.MinMass > 0.0f && InData.Mass < Settings.MinMass)
			return false;

		if (Settings.MinSpeed > 0 && InData.Velocity.SizeSquared() < MinSpeedThresholdSquared)
			return false;

		if (Settings.MinVolume > 0)
		{
			TVector<float, 3> Extents = InData.BoundingBox.Extents();
			float Volume = Extents[0] * Extents[1] * Extents[2];

			if (Volume < Settings.MinVolume)
				return false;
		}

		return true;
	}

	bool FSolverBreakingEventFilter::Pass(const TBreakingData<float, 3>& InData) const
	{
		const float MinSpeedThresholdSquared = Settings.MinSpeed * Settings.MinSpeed;

		if (Settings.MinMass > 0.0f && InData.Mass < Settings.MinMass)
			return false;

		if (Settings.MinSpeed > 0 && InData.Velocity.SizeSquared() < MinSpeedThresholdSquared)
			return false;

		if (Settings.MinVolume > 0)
		{
			TVector<float, 3> Extents = InData.BoundingBox.Extents();
			float Volume = Extents[0] * Extents[1] * Extents[2];

			if (Volume < Settings.MinVolume)
				return false;
		}

		return true;
	}

} // namespace Chaos
