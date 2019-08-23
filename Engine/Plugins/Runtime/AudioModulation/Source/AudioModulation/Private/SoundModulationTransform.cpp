// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SoundModulationTransform.h"

#include "Audio.h"


FSoundModulationInputTransform::FSoundModulationInputTransform()
	: InputMin(0.0f)
	, InputMax(1.0f)
	, OutputMin(0.0f)
	, OutputMax(1.0f)
{
}

void FSoundModulationInputTransform::Apply(float& Value) const
{
	const float Alpha = FMath::Clamp((Value - InputMin) / (InputMax - InputMin), 0.0f, 1.0f);
	Value = FMath::Lerp(OutputMin, OutputMax, Alpha);
}

FSoundModulationOutputTransform::FSoundModulationOutputTransform()
	: InputMin(0.0f)
	, InputMax(1.0f)
	, Curve(ESoundModulatorOutputCurve::Exp)
	, Scalar(2.5f)
	, OutputMin(0.0f)
	, OutputMax(1.0f)
{
}

void FSoundModulationOutputTransform::Apply(float& Value) const
{
	// Clamp the input
	Value = FMath::Clamp(Value, InputMin, InputMax);

	EvaluateCurve(Value);

	// Clamp the output
	Value = FMath::Clamp(Value, OutputMin, OutputMax);
}

void FSoundModulationOutputTransform::EvaluateCurve(float& Value) const
{
	// If custom curve, evaluate curve and return before calculating alpha
	if (Curve == ESoundModulatorOutputCurve::Custom)
	{
		if (const FRichCurve* RichCurve = FloatCurve.GetRichCurveConst())
		{
			Value = RichCurve->Eval(Value);
		}
		return;
	}

	// Avoid divide-by-zero & return max if input range is limited to single value
	if (InputMax == InputMin)
	{
		Value = OutputMax;
		return;
	}
	
	const float Alpha = FMath::Clamp((Value - InputMin) / (InputMax - InputMin), 0.0f, 1.0f);
	switch (Curve)
	{
		case ESoundModulatorOutputCurve::Linear:
		{
			Value = Alpha;
		}
		break;
	
		case ESoundModulatorOutputCurve::Exp:
		{
			// Alpha is limited to between 0.0f and 1.0f and ExponentialScalar
			// between 0 and 10 to keep values "sane" and avoid float boundary.
			Value = Alpha * (FMath::Pow(10.0f, Scalar * (Alpha - 1.0f)));
		}
		break;

		case ESoundModulatorOutputCurve::Exp_Inverse:
		{
			// Alpha is limited to between 0.0f and 1.0f and ExponentialScalar
			// between 0 and 10 to keep values "sane" and avoid float boundary.
			Value = ((Alpha - 1.0f) * (Scalar * FMath::Pow(10.0f, Alpha))) + 1.0f;
		}
		break;

		case ESoundModulatorOutputCurve::Log:
		{
			Value = (Scalar * FMath::LogX(10.0f, Alpha)) + 1.0f;
		}
		break;

		case ESoundModulatorOutputCurve::Sin:
		{
			Value = Audio::FastSin(HALF_PI * Alpha);
		}
		break;

		case ESoundModulatorOutputCurve::SCurve:
		{
			Value = 0.5f * Audio::FastSin(((PI * Alpha) - HALF_PI)) + 0.5f;
		}
		break;

		default:
		{
			static_assert(static_cast<int32>(ESoundModulatorOutputCurve::Count) == 7, "Possible missing case coverage for output curve.");
		}
		break;
	}

	Value = FMath::Lerp(OutputMin, OutputMax, Value);
}
