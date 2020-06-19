// Copyright Epic Games, Inc. All Rights Reserved.


#include "SynthComponents/SynthComponentMoto.h"
#include "MotoSynthSourceAsset.h"
#include "DSP/Granulator.h"
#include "SynthesisModule.h"


USynthComponentMoto::USynthComponentMoto(const FObjectInitializer& ObjInitializer)
	: Super(ObjInitializer)
{
	// Moto synth upmixes mono to stereo
	NumChannels = 2;
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
	if (FMotoSynthEngine::IsMotoSynthEngineEnabled())
	{
		const float MinRPM = 100.0f;
		if (InRPM > MinRPM && !FMath::IsNaN(InRPM))
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
		else
		{
			UE_LOG(LogSynthesis, Verbose, TEXT("Moto synth SetRPM was given invalid RPM value: %f."), InRPM);
		}
	}
}

void USynthComponentMoto::GetRPMRange(float& OutMinRPM, float& OutMaxRPM)
{
	OutMinRPM = RPMRange.X;
	OutMaxRPM = RPMRange.Y;

	if (FMath::IsNearlyEqual(OutMinRPM, OutMaxRPM))
	{
		UE_LOG(LogSynthesis, Verbose, TEXT("Moto synth min and max RPMs are nearly identical. Min RPM: %f, Max RPM: %f"), OutMinRPM, OutMaxRPM);
		OutMaxRPM = OutMinRPM + 1.0f;
	}
}

ISoundGeneratorPtr USynthComponentMoto::CreateSoundGenerator(int32 InSampleRate, int32 InNumChannels)
{
	if (!FMotoSynthEngine::IsMotoSynthEngineEnabled())
	{
		UE_LOG(LogSynthesis, Verbose, TEXT("Moto synth has been disabled by cvar."));
		return ISoundGeneratorPtr(new FSoundGeneratorNull());
	}
 
	if (MotoSynthPreset && MotoSynthPreset->Settings.AccelerationSource && MotoSynthPreset->Settings.DecelerationSource)
	{
 		MotoSynthEngine = ISoundGeneratorPtr(new FMotoSynthEngine());
 
 		if (FMotoSynthEngine* MS = static_cast<FMotoSynthEngine*>(MotoSynthEngine.Get()))
 		{
 			MS->Init(InSampleRate);
 
 			FMotoSynthData AccelerationSourceData;
			MotoSynthPreset->Settings.AccelerationSource->GetData(AccelerationSourceData);
 
 			FMotoSynthData DecelerationSourceData;
			MotoSynthPreset->Settings.DecelerationSource->GetData(DecelerationSourceData);
 
 			MS->SetSourceData(AccelerationSourceData, DecelerationSourceData);
			MS->SetSettings(MotoSynthPreset->Settings);
			MS->GetRPMRange(RPMRange);
 		}
 
 		return MotoSynthEngine;
 	}
 	else
 	{
 		UE_LOG(LogSynthesis, Verbose, TEXT("Can't play moto synth without a preset UMotoSynthPreset object and both acceleration source and deceleration source set."));
 		return ISoundGeneratorPtr(new FSoundGeneratorNull());
 	}
 
	return nullptr;
}