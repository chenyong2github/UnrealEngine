// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConcertMessageData.h"
#include "IConcertModule.h"
#include "Templates/Function.h"

class IConcertSyncClient;

namespace DisasterRecoveryUtil
{
/**
 * Function invoked by by the recovery system to pin/lock a given session to be recovered by this instance.
 */
using PinSessionToRestoreFunc = TFunction<TPair<bool, const FConcertSessionInfo*>(const TArray<FConcertSessionInfo>& /*ArchivedSessions*/)>;

/**
 * Start the recovery flow. This should be called when the previous instance of the editor exited unexpectedly.
 * @param SyncClient The client configured with the recovery server URL, the recovery session name and recovery archive name.
 * @param PinSessionToRestoreFn A function used by the process to take ownership for recovering a session.
 * @param bLiveDataOnly Filter the recovery data to only recover live transaction data. (Transaction that were not saved to disk yet)
 */
void StartRecovery(TSharedRef<IConcertSyncClient> SyncClient, const PinSessionToRestoreFunc& PinSessionToRestoreFn, bool bLiveDataOnly);

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
