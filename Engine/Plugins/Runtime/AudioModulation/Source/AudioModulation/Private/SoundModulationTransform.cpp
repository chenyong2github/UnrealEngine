// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundModulationTransform.h"

#include "Audio.h"
#include "DSP/Dsp.h"


void FSoundModulationTransform::Apply(float& OutValue) const
{
	// Clamp the input
	OutValue = FMath::Clamp(OutValue, 0.0f, 1.0f);

	switch (Curve)
	{
		case ESoundModulatorCurve::Custom:
		{
			OutValue = CurveCustom.Eval(OutValue);
			break;
		}

		case ESoundModulatorCurve::Shared:
		{
			if (CurveShared)
			{
				OutValue = CurveShared->FloatCurve.Eval(OutValue);
			}
			break;
		}

		case ESoundModulatorCurve::Linear:
		{
			// Do nothing, just linearly map output to incoming value
		}
		break;
	
		case ESoundModulatorCurve::Exp:
		{
			// Alpha is limited to between 0.0f and 1.0f and ExponentialScalar
			// between 0 and 10 to keep OutValues "sane" and avoid float boundary.
			OutValue *= (FMath::Pow(10.0f, Scalar * (OutValue - 1.0f)));
		}
		break;

		case ESoundModulatorCurve::Exp_Inverse:
		{
			// Alpha is limited to between 0.0f and 1.0f and ExponentialScalar
			// between 0 and 10 to keep OutValues "sane" and avoid float boundary.
			OutValue = ((OutValue - 1.0f) * FMath::Pow(10.0f, -1.0f * Scalar * OutValue)) + 1.0f;
		}
		break;

		case ESoundModulatorCurve::Log:
		{
			OutValue = (Scalar * FMath::LogX(10.0f, OutValue)) + 1.0f;
		}
		break;

		case ESoundModulatorCurve::Sin:
		{
			OutValue = Audio::FastSin(HALF_PI * OutValue);
		}
		break;

		case ESoundModulatorCurve::SCurve:
		{
			OutValue = 0.5f * Audio::FastSin(((PI * OutValue) - HALF_PI)) + 0.5f;
		}
		break;

		default:
		{
			static_assert(static_cast<int32>(ESoundModulatorCurve::Count) == 8, "Possible missing case coverage for output curve.");
		}
		break;
	}

	OutValue = FMath::Clamp(OutValue, 0.0f, 1.0f);
}
