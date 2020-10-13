// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundModulationParameter.h"


#if WITH_EDITOR
void USoundModulationParameter::RefreshNormalizedValue()
{
	const float NewNormalizedValue = ConvertUnitToNormalized(Settings.ValueUnit);
	const float NewNormalizedValueClamped = FMath::Clamp(NewNormalizedValue, 0.0f, 1.0f);
	if (!FMath::IsNearlyEqual(NewNormalizedValueClamped, Settings.ValueNormalized))
	{
		Settings.ValueNormalized = NewNormalizedValueClamped;
	}
}

void USoundModulationParameter::RefreshUnitValue()
{
	const float NewUnitValue = ConvertNormalizedToUnit(Settings.ValueNormalized);
	const float NewUnitValueClamped = FMath::Clamp(NewUnitValue, GetUnitMin(), GetUnitMax());
	if (!FMath::IsNearlyEqual(NewUnitValueClamped, Settings.ValueUnit))
	{
		Settings.ValueUnit = NewUnitValueClamped;
	}
}
#endif // WITH_EDITOR

bool USoundModulationParameterFrequencyBase::RequiresUnitConversion() const
{
	return true;
}

Audio::FModulationUnitConvertFunction USoundModulationParameterFrequencyBase::GetUnitConversionFunction() const
{
	const float InUnitMin = GetUnitMin();
	const float InUnitMax = GetUnitMax();
	return [InUnitMin, InUnitMax](float* RESTRICT OutValueBuffer, int32 InNumSamples)
	{
		for (int32 i = 0; i < InNumSamples; ++i)
		{
			OutValueBuffer[i] = Audio::GetLogFrequencyClamped(OutValueBuffer[i], FVector2D(0.0f, 1.0f), FVector2D(InUnitMin, InUnitMax));
		}
	};
}

Audio::FModulationNormalizedConversionFunction USoundModulationParameterFrequencyBase::GetNormalizedConversionFunction() const
{
	return [InUnitMin = GetUnitMin(), InUnitMax = GetUnitMax()](float* RESTRICT OutValueBuffer, int32 InNumSamples)
	{
		for (int32 i = 0; i < InNumSamples; ++i)
		{
			OutValueBuffer[i] = Audio::GetLinearFrequencyClamped(OutValueBuffer[i], FVector2D(0.0f, 1.0f), FVector2D(InUnitMin, InUnitMax));
		}
	};
}

Audio::FModulationMixFunction USoundModulationParameterHPFFrequency::GetMixFunction() const
{
	return [](float* RESTRICT OutValueBuffer, const float* RESTRICT InValueBuffer, int32 InNumSamples)
	{
		for (int32 i = 0; i < InNumSamples; ++i)
		{
			OutValueBuffer[i] = FMath::Max(OutValueBuffer[i], InValueBuffer[i]);
		}
	};
}

USoundModulationParameterHPFFrequency::USoundModulationParameterHPFFrequency(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Settings.ValueNormalized = 0.0f;

#if WITH_EDITORONLY_DATA
	Settings.ValueUnit = GetUnitDefault();
#endif // WITH_EDITORONLY_DATA
}

Audio::FModulationMixFunction USoundModulationParameterLPFFrequency::GetMixFunction() const
{
	return [](float* RESTRICT OutValueBuffer, const float* RESTRICT InValueBuffer, int32 InNumSamples)
	{
		for (int32 i = 0; i < InNumSamples; ++i)
		{
			OutValueBuffer[i] = FMath::Min(OutValueBuffer[i], InValueBuffer[i]);
		}
	};
}

bool USoundModulationParameterScaled::RequiresUnitConversion() const
{
	return true;
}

Audio::FModulationUnitConvertFunction USoundModulationParameterScaled::GetUnitConversionFunction() const
{
	return [InUnitMin = UnitMin, InUnitMax = UnitMax](float* RESTRICT OutValueBuffer, int32 InNumSamples)
	{
		for (int32 i = 0; i < InNumSamples; ++i)
		{
			OutValueBuffer[i] = FMath::Lerp(InUnitMin, InUnitMax, OutValueBuffer[i]);
		}
	};
}

Audio::FModulationNormalizedConversionFunction USoundModulationParameterScaled::GetNormalizedConversionFunction() const
{
	return [InUnitMin = UnitMin, InUnitMax = UnitMax](float* RESTRICT OutValueBuffer, int32 InNumSamples)
	{
		const float Denom = FMath::Max(SMALL_NUMBER, InUnitMax - InUnitMin);
		for (int32 i = 0; i < InNumSamples; ++i)
		{
			OutValueBuffer[i] = (OutValueBuffer[i] - InUnitMin) / Denom;
		}
	};
}

float USoundModulationParameterScaled::GetUnitMin() const
{
	return UnitMin;
}

float USoundModulationParameterScaled::GetUnitMax() const
{
	return UnitMax;
}

USoundModulationParameterBipolar::USoundModulationParameterBipolar(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Settings.ValueNormalized = 0.5f;

#if WITH_EDITORONLY_DATA
	Settings.ValueUnit = GetUnitDefault();
#endif // WITH_EDITORONLY_DATA
}

bool USoundModulationParameterBipolar::RequiresUnitConversion() const
{
	return true;
}

Audio::FModulationMixFunction USoundModulationParameterBipolar::GetMixFunction() const
{
	return [](float* RESTRICT OutValueBuffer, const float* RESTRICT InValueBuffer, int32 InNumSamples)
	{
		for (int32 i = 0; i < InNumSamples; ++i)
		{
			OutValueBuffer[i] += InValueBuffer[i] - 0.5f;
		}
	};
}

Audio::FModulationUnitConvertFunction USoundModulationParameterBipolar::GetUnitConversionFunction() const
{
	return [InUnitRange = UnitRange](float* RESTRICT OutValueBuffer, int32 InNumSamples)
	{
		for (int32 i = 0; i < InNumSamples; ++i)
		{
			OutValueBuffer[i] = (InUnitRange * OutValueBuffer[i]) - (0.5f * InUnitRange);
		}
	};
}

Audio::FModulationNormalizedConversionFunction USoundModulationParameterBipolar::GetNormalizedConversionFunction() const
{
	return [InUnitRange = UnitRange](float* RESTRICT OutValueBuffer, int32 InNumSamples)
	{
		for (int32 i = 0; i < InNumSamples; ++i)
		{
			OutValueBuffer[i] = 0.5f + (OutValueBuffer[i] / FMath::Max(InUnitRange, SMALL_NUMBER));
		}
	};
}

float USoundModulationParameterBipolar::GetUnitMax() const
{
	return UnitRange * 0.5f;
}

float USoundModulationParameterBipolar::GetUnitMin() const
{
	return UnitRange * -0.5f;
}

bool USoundModulationParameterVolume::RequiresUnitConversion() const
{
	return true;
}

Audio::FModulationUnitConvertFunction USoundModulationParameterVolume::GetUnitConversionFunction() const
{
	return [InUnitMin = GetUnitMin()](float* RESTRICT OutValueBuffer, int32 InNumSamples)
	{
		for (int32 i = 0; i < InNumSamples; ++i)
		{
			OutValueBuffer[i] = OutValueBuffer[i] > 0.0f
				? Audio::ConvertToDecibels(OutValueBuffer[i])
				: InUnitMin;
		}
	};
}

Audio::FModulationNormalizedConversionFunction USoundModulationParameterVolume::GetNormalizedConversionFunction() const
{
	return [InUnitMin = GetUnitMin()](float* RESTRICT OutValueBuffer, int32 InNumSamples)
	{
		for (int32 i = 0; i < InNumSamples; ++i)
		{
			OutValueBuffer[i] = OutValueBuffer[i] < InUnitMin
				? 0.0f
				: Audio::ConvertToLinear(OutValueBuffer[i]);
		}
	};
}

float USoundModulationParameterVolume::GetUnitMin() const
{
	return MinVolume;
}

float USoundModulationParameterVolume::GetUnitMax() const
{
	return 0.0f;
}