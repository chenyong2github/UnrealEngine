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

	/** IModuleInterface */
	void StartupModule() override;
	void ShutdownModule() override;

	/** IMagicLeapTrackerEntity interface */
	void DestroyEntityTracker() override;

	bool Tick(float DeltaTime);

	uint32 GetMaxSimultaneousTargets() const;
	void SetMaxSimultaneousTargets(uint32 NewNumTargets);

	/** IMagicLeapImageTrackerModule interface */
	virtual bool GetImageTrackerEnabled() const override;
	virtual void SetImageTrackerEnabled(bool bEnabled) override;
	virtual void SetTargetAsync(const FMagicLeapImageTargetSettings& ImageTarget, const FMagicLeapSetImageTargetCompletedStaticDelegate& SucceededDelegate, const FMagicLeapSetImageTargetCompletedStaticDelegate& FailedDelegate) override;
	virtual void SetTargetAsync(const FMagicLeapImageTargetSettings& ImageTarget, const FMagicLeapSetImageTargetSucceededMulti& SucceededDelegate, const FMagicLeapSetImageTargetFailedMulti& FailedDelegate) override;
	virtual bool RemoveTargetAsync(const FString& TargetName) override;
	virtual void DestroyTracker() override;
	virtual void GetTargetState(const FString& TargetName, bool bProvideTransformInTrackingSpace, FMagicLeapImageTargetState& TargetState) const override;
	virtual FGuid GetTargetHandle(const FString& TargetName) const override;

private:
	FTickerDelegate TickDelegate;
	FDelegateHandle TickDelegateHandle;
	class FMagicLeapImageTrackerRunnable* Runnable;
};

inline FMagicLeapImageTrackerModule& GetMagicLeapImageTrackerModule()
{
	return FModuleManager::Get().GetModuleChecked<FMagicLeapImageTrackerModule>("MagicLeapImageTracker");
}
