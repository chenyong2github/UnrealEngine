// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/IPv4/IPv4Endpoint.h"

struct FSwitchboardTask;

//~ Messages sent from Listener to Switchboard
FString CreateCommandAcceptedMessage(const FGuid& InMessageID);
FString CreateCommandDeclinedMessage(const FGuid& InMessageID, const FString& InErrorMessage);

FString CreateProgramStartedMessage(const FString& InProgramID, const FString& InMessageID);
FString CreateProgramStartFailedMessage(const FString& InErrorMessage, const FString& InMessageID);

FString CreateProgramKilledMessage(const FString& InProgramID);
FString CreateProgramKillFailedMessage(const FString& InProgramID, const FString& InErrorMessage);

FString CreateProgramEndedMessage(const FString& InProgramID, int InReturnCode, const FString& InProgramOutput);

FString CreateVcsInitCompletedMessage();
FString CreateVcsInitFailedMessage(const FString& InError);
FString CreateVcsReportRevisionCompletedMessage(const FString& InRevision);
FString CreateVcsReportRevisionFailedMessage(const FString& InError);
FString CreateVcsSyncCompletedMessage(const FString& InSyncedChange);
FString CreateVcsSyncFailedMessage(const FString& InErrorMessage);

FString CreateReceiveFileFromClientCompletedMessage(const FString& InDestinationPath);
FString CreateReceiveFileFromClientFailedMessage(const FString& InDestinationPath, const FString& InError);

FString CreateSendFileToClientCompletedMessage(const FString& InSourcePath, const FString& InFileContent);
FString CreateSendFileToClientFailedMessage(const FString& InSourcePath, const FString& InError);
//~

bool CreateTaskFromCommand(const FString& InCommand, const FIPv4Endpoint& InEndpoint, TUniquePtr<FSwitchboardTask>& OutTask);
