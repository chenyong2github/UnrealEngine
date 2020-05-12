// Copyright Epic Games, Inc. All Rights Reserved.


#include "SynthComponents/SynthComponentMoto.h"
#include "MotoSynthSourceAsset.h"
#include "DSP/Granulator.h"
#include "SynthesisModule.h"


USynthComponentMoto::USynthComponentMoto(const FObjectInitializer& ObjInitializer)
	: Super(ObjInitializer)
{
	NumChannels = 1;
}

USynthComponentMoto::~USynthComponentMoto()
{
}

bool USynthComponentMoto::IsEnabled() const
{
	return FMotoSynthEngine::IsMotoSynthEngineEnabled();
}

void USynthComponentMoto::SetRPM(float InRPM, float InTimeSec)
{
	RPM = InRPM;
	if (MotoSynthEngine.IsValid())
	{
		if (FMotoSynthEngine* MS = static_cast<FMotoSynthEngine*>(MotoSynthEngine.Get()))
		{
			float NewRPM = FMath::Clamp(InRPM, RPMRange.X, RPMRange.Y);
			MS->SetRPM(NewRPM, InTimeSec);
		}
	}
}

void USynthComponentMoto::GetRPMRange(float& OutMinRPM, float& OutMaxRPM)
{
	OutMinRPM = RPMRange.X;
	OutMaxRPM = RPMRange.Y;
}

void USynthComponentMoto::SetSynthToneEnabled(bool bInEnabled)
{
	bEnableSynthTone = bInEnabled;
	if (MotoSynthEngine.IsValid())
	{
		if (FMotoSynthEngine* MS = static_cast<FMotoSynthEngine*>(MotoSynthEngine.Get()))
		{
			MS->SetSynthToneEnabled(bEnableSynthTone);
		}
	}
}

void USynthComponentMoto::SetSynthToneVolume(float Volume)
{
	SynthToneVolume = Volume;
	if (MotoSynthEngine.IsValid())
	{
		if (FMotoSynthEngine* MS = static_cast<FMotoSynthEngine*>(MotoSynthEngine.Get()))
		{
			MS->SetSynthToneVolume(SynthToneVolume);
		}
	}
}

void USynthComponentMoto::SetGranularEngineEnabled(bool bInEnabled)
{
	bEnableGranularEngine = bInEnabled;
	if (MotoSynthEngine.IsValid())
	{
		if (FMotoSynthEngine* MS = static_cast<FMotoSynthEngine*>(MotoSynthEngine.Get()))
		{
			MS->SetGranularEngineEnabled(bEnableGranularEngine);
		}
	}
}

void USynthComponentMoto::SetGranularEngineVolume(float Volume)
{
	GranularEngineVolume = Volume;
	if (MotoSynthEngine.IsValid())
	{
		if (FMotoSynthEngine* MS = static_cast<FMotoSynthEngine*>(MotoSynthEngine.Get()))
		{
			MS->SetGranularEngineVolume(GranularEngineVolume);
		}
	}
}

ISoundGeneratorPtr USynthComponentMoto::CreateSoundGenerator(int32 InSampleRate, int32 InNumChannels)
{
	if (!FMotoSynthEngine::IsMotoSynthEngineEnabled())
	{
		return ISoundGeneratorPtr(new FSoundGeneratorNull());
	}

	if (AccelerationSource && DecelerationSource)
	{
		MotoSynthEngine = ISoundGeneratorPtr(new FMotoSynthEngine());

		if (FMotoSynthEngine* MS = static_cast<FMotoSynthEngine*>(MotoSynthEngine.Get()))
		{
			MS->Init(InSampleRate);

			FMotoSynthData AccelerationSourceData;
			AccelerationSource->GetData(AccelerationSourceData);

			FMotoSynthData DecelerationSourceData;
			DecelerationSource->GetData(DecelerationSourceData);

			MS->SetSourceData(AccelerationSourceData, DecelerationSourceData);
			MS->GetRPMRange(RPMRange);
			float NewRPM = FMath::Clamp(RPM, RPMRange.X, RPMRange.Y);
			MS->SetRPM(NewRPM, 0.0f);
			MS->SetSynthToneEnabled(bEnableSynthTone);
			MS->SetSynthToneVolume(SynthToneVolume);
			MS->SetGranularEngineEnabled(bEnableGranularEngine);
			MS->SetGranularEngineVolume(GranularEngineVolume);
		}

		return MotoSynthEngine;
	}
	else
	{
		UE_LOG(LogSynthesis, Warning, TEXT("Can't play moto synth without an acceleration source or without a deceleration source."));
		return ISoundGeneratorPtr(new FSoundGeneratorNull());
	}

	return nullptr;
}