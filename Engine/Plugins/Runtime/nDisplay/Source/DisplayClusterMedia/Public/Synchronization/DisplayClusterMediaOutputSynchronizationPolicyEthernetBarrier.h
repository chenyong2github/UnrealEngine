// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Synchronization/DisplayClusterMediaOutputSynchronizationPolicyEthernetBarrierBase.h"

#include "DisplayClusterMediaOutputSynchronizationPolicyEthernetBarrier.generated.h"


/*
 * EthernetBarrier media synchronization policy implementation
 */
UCLASS(editinlinenew, Blueprintable, meta = (DisplayName = "Ethernet Barrier"))
class DISPLAYCLUSTERMEDIA_API UDisplayClusterMediaOutputSynchronizationPolicyEthernetBarrier
	: public UDisplayClusterMediaOutputSynchronizationPolicyEthernetBarrierBase
{
	GENERATED_BODY()

protected:
	/** Synchronization procedure implementation. */
	virtual void Synchronize() override;
};
