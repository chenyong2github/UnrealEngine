// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Synchronization/DisplayClusterMediaOutputSynchronizationPolicyThresholdBase.h"

#include "DisplayClusterMediaOutputSynchronizationPolicyVblank.generated.h"

class IDisplayClusterVblankMonitor;


/*
 * Vblank media synchronization policy implementation
 */
UCLASS(editinlinenew, Blueprintable, meta = (DisplayName = "V-blank"))
class DISPLAYCLUSTERMEDIA_API UDisplayClusterMediaOutputSynchronizationPolicyVblank
	: public UDisplayClusterMediaOutputSynchronizationPolicyThresholdBase
{
	GENERATED_BODY()

public:
	virtual bool StartSynchronization(UMediaCapture* MediaCapture, const FString& MediaId) override;

protected:
	/** Returns amount of time before next synchronization point. */
	virtual double GetTimeBeforeNextSyncPoint() override;

private:
	// V-blank monitor
	TSharedPtr<IDisplayClusterVblankMonitor, ESPMode::ThreadSafe> VblankMonitor;
};
