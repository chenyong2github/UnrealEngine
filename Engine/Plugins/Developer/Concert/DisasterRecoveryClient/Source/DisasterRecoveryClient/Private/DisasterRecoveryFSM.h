// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IConcertModule.h"

class IConcertSyncClient;

namespace DisasterRecoveryUtil
{

/**
 * Start the recovery flow. This should be called when the previous instance of the editor exited unexpectedly.
 * @param SyncClient The client configured with the recovery server URL, the recovery session name and recovery archive name.
 * @param SessionNameToRecover The name of the session to recover.
 * @param bLiveDataOnly Filter the recovery data to only recover live transaction data. (Transaction that were not saved to disk yet)
 */
void StartRecovery(TSharedRef<IConcertSyncClient> SyncClient, const FString& SessionNameToRecover, bool bLiveDataOnly);

/** End the recovery flow. This can be called to abort the recovery process. */
void EndRecovery();

}