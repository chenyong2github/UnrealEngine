// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioAnalyzerSubsystem.h"
#include "Engine/World.h"

UAudioAnalyzerSubsystem::UAudioAnalyzerSubsystem()
{
}

UAudioAnalyzerSubsystem::~UAudioAnalyzerSubsystem()
{
	AudioAnalyzers.Reset();
}

UAudioAnalyzerSubsystem* UAudioAnalyzerSubsystem::Get()
{
	if (GEngine)
	{
		return GEngine->GetEngineSubsystem<UAudioAnalyzerSubsystem>();
	}
	return nullptr;
}

TStatId UAudioAnalyzerSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UAudioAnalyzerSubsystem, STATGROUP_Tickables);
}

void UAudioAnalyzerSubsystem::Tick(float DeltaTime)
{
	// Loop through all analyzers and if they're ready to analyze, do it
	for (UAudioAnalyzer* Analyzer : AudioAnalyzers)
	{
		if (Analyzer->IsReadyForAnalysis())
		{
			if (Analyzer->DoAnalysis())
			{
				Analyzer->BroadcastResults();
			}
		}
	}
}

bool UAudioAnalyzerSubsystem::IsTickable() const
{
	// As soon as any analyzers are ready, say we're ok to be ticked
	for (UAudioAnalyzer* Analyzer : AudioAnalyzers)
	{
		if (Analyzer->IsReadyForAnalysis())
		{
			return true;
		}
	}

	return false;
}

void UAudioAnalyzerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UAudioAnalyzerSubsystem::Deinitialize()
{
	// Release our audio analyzers
	AudioAnalyzers.Reset();
}

void UAudioAnalyzerSubsystem::RegisterAudioAnalyzer(UAudioAnalyzer* InAnalyzer)
{
	AudioAnalyzers.AddUnique(InAnalyzer);
}

void UAudioAnalyzerSubsystem::UnregisterAudioAnalyzer(UAudioAnalyzer* InAnalyzer)
{
	AudioAnalyzers.Remove(InAnalyzer);
}