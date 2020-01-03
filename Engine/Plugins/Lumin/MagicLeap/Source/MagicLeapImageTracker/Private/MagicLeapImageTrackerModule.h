// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Ticker.h"
#include "IMagicLeapImageTrackerModule.h"
#include "MagicLeapImageTrackerTypes.h"

class FMagicLeapImageTrackerModule : public IMagicLeapImageTrackerModule
{
public:
	FMagicLeapImageTrackerModule();
	void StartupModule() override;
	void ShutdownModule() override;
	bool Tick(float DeltaTime);
	void SetTargetAsync(const FMagicLeapImageTrackerTarget& ImageTarget);
	bool RemoveTargetAsync(const FString& InName);
	uint32 GetMaxSimultaneousTargets() const;
	void SetMaxSimultaneousTargets(uint32 NewNumTargets);
	bool GetImageTrackerEnabled() const;
	void SetImageTrackerEnabled(bool bEnabled);
	bool TryGetRelativeTransform(const FString& TargetName, FVector& OutLocation, FRotator& OutRotation);

private:
	FTickerDelegate TickDelegate;
	FDelegateHandle TickDelegateHandle;
	class FMagicLeapImageTrackerRunnable* Runnable;
};

inline FMagicLeapImageTrackerModule& GetMagicLeapImageTrackerModule()
{
	return FModuleManager::Get().GetModuleChecked<FMagicLeapImageTrackerModule>("MagicLeapImageTracker");
}
