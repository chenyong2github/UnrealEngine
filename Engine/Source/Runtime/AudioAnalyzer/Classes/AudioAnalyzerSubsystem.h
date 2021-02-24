// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AudioAnalyzer.h"
#include "Subsystems/WorldSubsystem.h"
#include "AudioAnalyzerSubsystem.generated.h"

class UWorld;

/** 
* Class manages querying analysis results from various audio analyzers.
*/
UCLASS(DisplayName = "Audio Analysis")
class AUDIOANALYZER_API UAudioAnalyzerSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UAudioAnalyzerSubsystem();
	~UAudioAnalyzerSubsystem();

	//~ Begin FTickableGameObject Interface
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual bool IsTickableInEditor() const override { return true; }
	virtual TStatId GetStatId() const override;
	//~ End FTickableGameObject Interface

	void RegisterAudioAnalyzer(UAudioAnalyzer* InAnalyzer);
	void UnregisterAudioAnalyzer(UAudioAnalyzer* InAnalyzer);

	static UAudioAnalyzerSubsystem* Get(UWorld* World);

private:

	UPROPERTY(Transient);
	TArray<TObjectPtr<UAudioAnalyzer>> AudioAnalyzers;
};



