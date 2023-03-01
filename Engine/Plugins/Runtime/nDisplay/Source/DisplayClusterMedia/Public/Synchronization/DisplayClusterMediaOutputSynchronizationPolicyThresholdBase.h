// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Synchronization/DisplayClusterMediaOutputSynchronizationPolicyEthernetBarrierBase.h"

#include "DisplayClusterMediaOutputSynchronizationPolicyThresholdBase.generated.h"


/*
 * Base class for threshold based media synchronization policies.
 * 
 * Basically it uses the same approach that we use in 'Ethernet' sync policy where v-blanks are used as the timepoints.
 */
UCLASS(Abstract, editinlinenew, BlueprintType, hidecategories = (Object))
class DISPLAYCLUSTERMEDIA_API UDisplayClusterMediaOutputSynchronizationPolicyThresholdBase
	: public UDisplayClusterMediaOutputSynchronizationPolicyEthernetBarrierBase
{
	GENERATED_BODY()

protected:
	/** Synchronization procedure implementation. */
	virtual void Synchronize() override;

	/** Returns amount of time before next synchronization point. */
	virtual double GetTimeBeforeNextSyncPoint() PURE_VIRTUAL(UDisplayClusterMediaOutputSynchronizationPolicyThresholdBase::GetTimeBeforeNextSyncPoint, return 0;)

protected:
	/** Synchronization margin (ms) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (DisplayName = "Margin (ms)", ClampMin = "1", ClampMax = "20", UIMin = "1", UIMax = "20"))
	int32 MarginMs = 5;
};
