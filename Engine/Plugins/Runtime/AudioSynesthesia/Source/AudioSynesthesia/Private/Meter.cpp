// Copyright Epic Games, Inc. All Rights Reserved.

#include "Meter.h"
#include "MeterFactory.h"
#include "InterpolateSorted.h"
#include "AudioSynesthesiaLog.h"
#include "DSP/EnvelopeFollower.h"

TUniquePtr<Audio::IAnalyzerSettings> UMeterSettings::GetSettings(const int32 InSampleRate, const int32 InNumChannels) const
{
	TUniquePtr<Audio::FMeterSettings> Settings = MakeUnique<Audio::FMeterSettings>();

	Settings->AnalysisPeriod = AnalysisPeriod;

	switch (PeakMode)
	{
	case EMeterPeakType::MeanSquared:
		Settings->MeterPeakMode = Audio::EPeakMode::MeanSquared;
		break;

	case EMeterPeakType::RootMeanSquared:
		Settings->MeterPeakMode = Audio::EPeakMode::RootMeanSquared;
		break;

	default:
	case EMeterPeakType::Peak:
		Settings->MeterPeakMode = Audio::EPeakMode::Peak;
		break;
	}

	Settings->MeterAttackTime = MeterAttackTime;
	Settings->MeterReleaseTime = MeterReleaseTime;
	Settings->PeakAttackTime = PeakAttackTime;
	Settings->PeakReleaseTime = PeakReleaseTime;
	Settings->PeakHoldTime = PeakHoldTime;
	Settings->ClippingThreshold = ClippingThreshold;

	return Settings;
}

#if WITH_EDITOR
FText UMeterSettings::GetAssetActionName() const
{
	return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_AssetSoundSynesthesiaMeterSettings", "Synesthesia Real-Time Settings (Meter)");
}

UClass* UMeterSettings::GetSupportedClass() const
{
	return UMeterSettings::StaticClass();
}
#endif

UMeterAnalyzer::UMeterAnalyzer()
{
	Settings = CreateDefaultSubobject<UMeterSettings>(TEXT("DefaultMeterSettings"));
}

TUniquePtr<Audio::IAnalyzerSettings> UMeterAnalyzer::GetSettings(const int32 InSampleRate, const int32 InNumChannels) const
{
	TUniquePtr<Audio::IAnalyzerSettings> AnalyzerSettings;

	if (Settings)
	{
		AnalyzerSettings = Settings->GetSettings(InSampleRate, InNumChannels);
	}

	return AnalyzerSettings;
}

static TArray<FMeterResults> ConvertToBlueprintResults(UMeterSettings* Settings, const TArray<Audio::FMeterEntry>& InMeterArray)
{
	TArray<FMeterResults> ResultsArray;

	for (const Audio::FMeterEntry& MeterEntry : InMeterArray)
	{
		FMeterResults NewResults;

		NewResults.MeterValue = MeterEntry.MeterValue;
		NewResults.PeakValue = MeterEntry.PeakValue;
		NewResults.ClippingValue = MeterEntry.ClippingValue;
		NewResults.NumSamplesClipping = MeterEntry.NumSamplesClipping;
		NewResults.TimeSeconds = MeterEntry.Timestamp;

		ResultsArray.Add(NewResults);
	}

	// Sort by priority (lowest priority first).
	ResultsArray.Sort([](const FMeterResults& A, const FMeterResults& B) 		
	{ 
		return A.TimeSeconds < B.TimeSeconds;
	});

	return MoveTemp(ResultsArray);
}

void UMeterAnalyzer::BroadcastResults()
{
	TUniquePtr<const Audio::FMeterResult> MeterResults = GetResults<Audio::FMeterResult>();
	if (!MeterResults.IsValid())
	{
		return;
	}

	int32 NumChannels = MeterResults->GetNumChannels();

	if (NumChannels > 0)
	{
		bool bIsOnOverallMeterResultsBound = OnOverallMeterResults.IsBound();
		bool bIsOnLatestOverallMeterResultsBound = OnLatestOverallMeterResults.IsBound();
		if (bIsOnOverallMeterResultsBound || bIsOnLatestOverallMeterResultsBound)
		{
			const TArray<Audio::FMeterEntry>& OverallMeterArray = MeterResults->GetMeterArray();
			if (OverallMeterArray.Num() > 0)
			{
				TArray<FMeterResults> Results = ConvertToBlueprintResults(Settings, OverallMeterArray);
				check(Results.Num() > 0);

				if (bIsOnOverallMeterResultsBound)
				{
					OnOverallMeterResults.Broadcast(Results);
				}
				if (bIsOnLatestOverallMeterResultsBound)
				{
					FMeterResults& Latest = Results[Results.Num() - 1];
					OnLatestOverallMeterResults.Broadcast(Latest);
				}
			}
		}

		bool bIsOnPerChannelMeterResultsBound = OnPerChannelMeterResults.IsBound();
		bool bIsOnLatestPerChannelMeterResultsBound = OnLatestPerChannelMeterResults.IsBound();

		if (bIsOnPerChannelMeterResultsBound || bIsOnLatestPerChannelMeterResultsBound)
		{
			for (int32 ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex)
			{
				const TArray<Audio::FMeterEntry>& MeterArray = MeterResults->GetChannelMeterArray(ChannelIndex);
				if (MeterArray.Num() > 0)
				{
					TArray<FMeterResults> Results = ConvertToBlueprintResults(Settings, MeterArray);
					check(Results.Num() > 0);

					if (bIsOnPerChannelMeterResultsBound)
					{
						OnPerChannelMeterResults.Broadcast(ChannelIndex, Results);
					}

					if (bIsOnLatestPerChannelMeterResultsBound)
					{
						OnLatestPerChannelMeterResults.Broadcast(ChannelIndex, Results[Results.Num() - 1]);
					}
				}
			}
		}
	}
}

FName UMeterAnalyzer::GetAnalyzerFactoryName() const
{
	static const FName FactoryName(TEXT("MeterFactory"));
	return FactoryName;
}