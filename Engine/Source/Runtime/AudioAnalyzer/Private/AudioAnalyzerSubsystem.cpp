// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioAnalyzerSubsystem.h"
#include "AudioAnalyzerModule.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

UAudioAnalyzerSubsystem::UAudioAnalyzerSubsystem()
{
}

UAudioAnalyzerSubsystem::~UAudioAnalyzerSubsystem()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
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

bool UAudioAnalyzerSubsystem::Tick(float DeltaTime)
{
	LLM_SCOPE_BYTAG(AudioAnalysis);

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
	return true;
}

void UAudioAnalyzerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UAudioAnalyzerSubsystem::Deinitialize()
{
	LLM_SCOPE_BYTAG(AudioAnalysis);

	// Release our audio analyzers
	AudioAnalyzers.Reset();
}

void UAudioAnalyzerSubsystem::RegisterAudioAnalyzer(UAudioAnalyzer* InAnalyzer)
{
	LLM_SCOPE_BYTAG(AudioAnalysis);

	if (!TickerHandle.IsValid())
	{
		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UAudioAnalyzerSubsystem::Tick), 0.0f);
	}
	AudioAnalyzers.AddUnique(InAnalyzer);
}

void UAudioAnalyzerSubsystem::UnregisterAudioAnalyzer(UAudioAnalyzer* InAnalyzer)
{
	LLM_SCOPE_BYTAG(AudioAnalysis);

	AudioAnalyzers.Remove(InAnalyzer);
	if (AudioAnalyzers.IsEmpty() && TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
}
