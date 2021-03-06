// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AudioAnalyzer.h"
#include "Subsystems/EngineSubsystem.h"
#include "Tickable.h"
#include "AudioAnalyzerSubsystem.generated.h"

class UWorld;

/** 
* Class manages querying analysis results from various audio analyzers.
*/
UCLASS()
class AUDIOANALYZER_API UAudioAnalyzerSubsystem : public UEngineSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	UAudioAnalyzerSubsystem();
	~UAudioAnalyzerSubsystem();

	//~ Begin FTickableGameObject Interface
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual bool IsTickableInEditor() const { return true; }
	virtual TStatId GetStatId() const override;
	//~ End FTickableGameObject Interface

	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	void RegisterAudioAnalyzer(UAudioAnalyzer* InAnalyzer);
	void UnregisterAudioAnalyzer(UAudioAnalyzer* InAnalyzer);

	static UAudioAnalyzerSubsystem* Get();

private:

	UPROPERTY(Transient);
	TArray<TObjectPtr<UAudioAnalyzer>> AudioAnalyzers;
};



