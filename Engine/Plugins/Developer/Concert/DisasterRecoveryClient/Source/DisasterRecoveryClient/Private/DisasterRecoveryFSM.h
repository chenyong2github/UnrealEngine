// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConcertMessageData.h"
#include "IConcertModule.h"
#include "Templates/Function.h"

class IConcertSyncClient;
class IDisasterRecoverySessionManager;

namespace DisasterRecoveryUtil
{
/**
 * Start the recovery flow. This should be called when the previous instance of the editor exited unexpectedly.
 * @param SyncClient The client configured with the recovery server URL, the recovery session name and recovery archive name.
 * @param RecoverySessionManager The manager used to interact with the available recovery sessions.
 * @param bLiveDataOnly Filter the recovery data to only recover live transaction data. (Transaction that were not saved to disk yet)
 */
void StartRecovery(TSharedRef<IConcertSyncClient> SyncClient, IDisasterRecoverySessionManager& RecoverySessionManager, bool bLiveDataOnly);

/**
 * End the recovery flow. This can be called to abort the recovery process.
 * @return True if the recovery already completed successfully, false if it was not completed and it was aborted.
 */
bool EndRecovery();

/**
 * Return the name of the executable hosting disaster recovery service, like 'UnrealDisasterRecoveryService' without the extension.
 */
FString GetDisasterRecoveryServiceExeName();
}
