// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IEyeTrackerModule.h"
#include "EyeTrackerTypes.h"
#include "IEyeTracker.h"
#include "IMagicLeapVREyeTracker.h"
#include "MagicLeapEyeTrackerTypes.h"
#include "Containers/Ticker.h"
#include "GameFramework/HUD.h"
#include "Lumin/CAPIShims/LuminAPIEyeTracking.h"
#include "IMagicLeapTrackerEntity.h"

class FMagicLeapVREyeTracker : public IMagicLeapVREyeTracker, public FTickerObjectBase, public IMagicLeapTrackerEntity
{
public:
	FMagicLeapVREyeTracker();
	virtual ~FMagicLeapVREyeTracker();

	/** IMagicLeapTrackerEntity interface */
	virtual void DestroyEntityTracker() override;

	void SetDefaultDataValues();

	void SetActivePlayerController(APlayerController* NewActivePlayerController);
	APlayerController* GetActivePlayerController() const { return ActivePlayerController.Get(); }

	virtual bool Tick(float DeltaTime) override;

	void DrawDebug(AHUD* HUD, UCanvas* Canvas, const FDebugDisplayInfo& DisplayInfo, float& YL, float& YPos);

	EMagicLeapEyeTrackingCalibrationStatus GetCalibrationStatus() const;

public:
	virtual const FMagicLeapVREyeTrackingData& GetVREyeTrackingData() override;

	virtual EMagicLeapEyeTrackingStatus GetEyeTrackingStatus() override;

private:
	TWeakObjectPtr<APlayerController> ActivePlayerController;
	EMagicLeapEyeTrackingStatus EyeTrackingStatus;
	EMagicLeapEyeTrackingCalibrationStatus EyeCalibrationStatus;

	FMagicLeapVREyeTrackingData UnfilteredEyeTrackingData;

	bool bReadyToInit;
	bool bInitialized;

#if WITH_MLSDK
	MLHandle EyeTrackingHandle;
	MLEyeTrackingStaticData EyeTrackingStaticData;
#endif //WITH_MLSDK
};

