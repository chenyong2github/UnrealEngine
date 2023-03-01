// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Synchronization/DisplayClusterMediaOutputSynchronizationPolicyThresholdBase.h"

#include "DisplayClusterMediaOutputSynchronizationPolicyRivermax.generated.h"


/*
 * Rivermax media synchronization policy implementation
 */
UCLASS(editinlinenew, Blueprintable, meta = (DisplayName = "Rivermax (PTP)"))
class DISPLAYCLUSTERMEDIA_API UDisplayClusterMediaOutputSynchronizationPolicyRivermax
	: public UDisplayClusterMediaOutputSynchronizationPolicyThresholdBase
{
	GENERATED_BODY()

public:
	/** Returns true if specified media capture type can be synchonized by the policy implementation */
	virtual bool IsCaptureTypeSupported(UMediaCapture* MediaCapture) const override;

protected:
	/** Returns amount of time before next synchronization point. */
	virtual double GetTimeBeforeNextSyncPoint() override;
};
