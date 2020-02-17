// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Ticker.h"
#include "IMagicLeapImageTrackerModule.h"
#include "IMagicLeapTrackerEntity.h"
#include "MagicLeapImageTrackerTypes.h"

class FMagicLeapImageTrackerModule : public IMagicLeapImageTrackerModule, public IMagicLeapTrackerEntity
{
public:
	FMagicLeapImageTrackerModule();
	virtual ~FMagicLeapImageTrackerModule();
	void StartupModule() override;
	void ShutdownModule() override;
	void DestroyEntityTracker() override;
	bool Tick(float DeltaTime);
	virtual void SetTargetAsync(const FMagicLeapImageTrackerTarget& ImageTarget) override;
	bool RemoveTargetAsync(const FString& InName);
	uint32 GetMaxSimultaneousTargets() const;
	void SetMaxSimultaneousTargets(uint32 NewNumTargets);
	virtual bool GetImageTrackerEnabled() const override;
	virtual void SetImageTrackerEnabled(bool bEnabled) override;
	virtual void DestroyTracker() override;
	virtual bool TryGetRelativeTransform(const FString& TargetName, FVector& OutLocation, FRotator& OutRotation) override;
	virtual bool IsTracked(const FString& TargetName) const override;

private:
	FTickerDelegate TickDelegate;
	FDelegateHandle TickDelegateHandle;
	class FMagicLeapImageTrackerRunnable* Runnable;
};

inline FMagicLeapImageTrackerModule& GetMagicLeapImageTrackerModule()
{
	return FModuleManager::Get().GetModuleChecked<FMagicLeapImageTrackerModule>("MagicLeapImageTracker");
}
