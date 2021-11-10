// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundModulationParameter.h"

#include "AudioDevice.h"
#include "AudioDeviceManager.h"


TUniquePtr<Audio::IProxyData> USoundModulationParameter::CreateNewProxyData(const Audio::FProxyDataInitParams& InitParams)
{
	using namespace AudioModulation;
	return MakeUnique<FSoundModulationPluginParameterAssetProxy>(this);
}

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

Audio::FModulationUnitConversionFunction USoundModulationParameterFrequencyBase::GetUnitConversionFunction() const
{
	return [InUnitMin = GetUnitMin(), InUnitMax = GetUnitMax()](float& InOutValue)
	{
		static const FVector2D Domain(0.0f, 1.0f);
		const FVector2D Range(InUnitMin, InUnitMax);
		InOutValue = Audio::GetLogFrequencyClamped(InOutValue, Domain, Range);
	};
}

Audio::FModulationNormalizedConversionFunction USoundModulationParameterFrequencyBase::GetNormalizedConversionFunction() const
{
	return [InUnitMin = GetUnitMin(), InUnitMax = GetUnitMax()](float& InOutValue)
	{
		static const FVector2D Domain(0.0f, 1.0f);
		const FVector2D Range(InUnitMin, InUnitMax);
		InOutValue = Audio::GetLinearFrequencyClamped(InOutValue, Domain, Range);
	};
}

Audio::FModulationMixFunction USoundModulationParameterHPFFrequency::GetMixFunction() const
{
	return [](float& InOutValue, float InValue)
	{
		InOutValue = FMath::Max(InOutValue, InValue);
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
	return [](float& InOutValueA, float InValueB)
	{
		InOutValueA = FMath::Min(InOutValueA, InValueB);
	};
}

bool USoundModulationParameterScaled::RequiresUnitConversion() const
{
	return true;
}

Audio::FModulationUnitConversionFunction USoundModulationParameterScaled::GetUnitConversionFunction() const
{
	return [InUnitMin = UnitMin, InUnitMax = UnitMax](float& InOutValue)
	{
		InOutValue = FMath::Lerp(InUnitMin, InUnitMax, InOutValue);
	};
}

Audio::FModulationNormalizedConversionFunction USoundModulationParameterScaled::GetNormalizedConversionFunction() const
{
	return [InUnitMin = UnitMin, InUnitMax = UnitMax](float& InOutValue)
	{
		const float Denom = FMath::Max(SMALL_NUMBER, InUnitMax - InUnitMin);
		InOutValue = (InOutValue - InUnitMin) / Denom;
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
	return [](float& InOutValueA, float InValueB)
	{
		InOutValueA += InValueB - 0.5f;
	};
}

Audio::FModulationUnitConversionFunction USoundModulationParameterBipolar::GetUnitConversionFunction() const
{
	return [InUnitRange = UnitRange](float& InOutValue)
	{
		InOutValue = (InUnitRange * InOutValue) - (0.5f * InUnitRange);
	};
}

Audio::FModulationNormalizedConversionFunction USoundModulationParameterBipolar::GetNormalizedConversionFunction() const
{
	return [InUnitRange = UnitRange](float& InOutValue)
	{
		InOutValue = 0.5f + (InOutValue / FMath::Max(InUnitRange, SMALL_NUMBER));
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

Audio::FModulationUnitConversionFunction USoundModulationParameterVolume::GetUnitConversionFunction() const
{
	return [InUnitMin = GetUnitMin()](float& InOutValue)
	{
		InOutValue = InOutValue > 0.0f
			? Audio::ConvertToDecibels(InOutValue)
			: InUnitMin;
	};
}

Audio::FModulationNormalizedConversionFunction USoundModulationParameterVolume::GetNormalizedConversionFunction() const
{
	return [InUnitMin = GetUnitMin()](float& InOutValue)
	{
		InOutValue = InOutValue < InUnitMin || FMath::IsNearlyEqual(InOutValue, InUnitMin)
			? 0.0f
			: Audio::ConvertToLinear(InOutValue);
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

namespace AudioModulation
{
	FSoundModulationPluginParameterAssetProxy::FSoundModulationPluginParameterAssetProxy(USoundModulationParameter* InParameter)
	{
		using namespace Audio;

		if (!InParameter || !GEngine)
		{
			return;
		}

		FAudioDeviceHandle AudioDevice;
		if (UWorld* World = GEngine->GetWorldFromContextObject(InParameter, EGetWorldErrorMode::ReturnNull))
		{
			if (!World->bAllowAudioPlayback || World->IsNetMode(NM_DedicatedServer))
			{
				return;
			}

			AudioDevice = World->GetAudioDevice();
		}
		else
		{
			AudioDevice = GEngine->GetMainAudioDevice();
		}

		if (IAudioModulation* Modulation = AudioDevice->ModulationInterface.Get())
		{
			const FName ParameterName = InParameter->GetFName();
			Parameter = Modulation->GetParameter(ParameterName);
		}
	}

	Audio::IProxyDataPtr FSoundModulationPluginParameterAssetProxy::Clone() const
	{
		return TUniquePtr<FSoundModulationPluginParameterAssetProxy>(new FSoundModulationPluginParameterAssetProxy(*this));
	}

	const Audio::FModulationParameter& FSoundModulationPluginParameterAssetProxy::GetParameter() const
	{
		return Parameter;
	}
}