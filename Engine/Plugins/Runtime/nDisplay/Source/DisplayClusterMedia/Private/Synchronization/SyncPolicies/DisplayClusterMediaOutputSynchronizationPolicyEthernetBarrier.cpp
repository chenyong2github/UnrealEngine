// Copyright Epic Games, Inc. All Rights Reserved.

#include "Synchronization/DisplayClusterMediaOutputSynchronizationPolicyEthernetBarrier.h"


void UDisplayClusterMediaOutputSynchronizationPolicyEthernetBarrier::Synchronize()
{
	// Just sync on the barrier
	SyncThreadOnBarrier();
}
