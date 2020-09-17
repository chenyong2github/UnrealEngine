// Copyright Epic Games, Inc. All Rights Reserved.

#include "SwitchboardListener.h"

#include "SwitchboardListenerApp.h"
#include "SwitchboardProtocol.h"
#include "SwitchboardSourceControl.h"
#include "SwitchboardTasks.h"

#include "Common/TcpListener.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "IPAddress.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	const double SecondsUntilInactiveClientDisconnect = 5.0;

	bool TryFindIdInBrokenMessage(const FString& InMessage, FGuid& OutGuid)
	{
		// Try to parse message that could not be parsed regularly and look for the message ID.
		// This way we can at least tell Switchboard which message was broken.
		const int32 IdIdx = InMessage.Find(TEXT("'id'"));
		if (IdIdx > 0)
		{
			const FString Chopped = InMessage.RightChop(IdIdx);
			FString LeftOfComma;
			FString RightOfComma;
			if (Chopped.Split(",", &LeftOfComma, &RightOfComma))
			{
				FString LeftOfColon;
				FString RightOfColon;
				if (LeftOfComma.Split(":", &LeftOfColon, &RightOfColon))
				{
					RightOfColon.TrimStartAndEndInline();
					bool bDoubleQuotesRemoved = false;
					RightOfColon.TrimQuotesInline(&bDoubleQuotesRemoved);
					if (!bDoubleQuotesRemoved)
					{
						// remove single quotes if there were no double quotes
						RightOfColon.LeftChopInline(1);
						RightOfColon.RightChopInline(1);
					}

					return FGuid::Parse(RightOfColon, OutGuid);
				}
			}
		}

		return false;
	}
}

struct FRunningProcess
{
	uint32 PID;
	FGuid UUID;
	FProcHandle Handle;

	void* WritePipe;
	void* ReadPipe;
	TArray<uint8> Output;

	FIPv4Endpoint Recipient;
};

FSwitchboardListener::FSwitchboardListener(const FIPv4Endpoint& InEndpoint)
	: Endpoint(MakeUnique<FIPv4Endpoint>(InEndpoint))
	, SocketListener(nullptr)
	, SourceControl(MakeUnique<FSwitchboardSourceControl>())
{
}

FSwitchboardListener::~FSwitchboardListener()
{
	KillAllProcesses();
}

bool FSwitchboardListener::Init()
{
	SocketListener = MakeUnique<FTcpListener>(*Endpoint);
	SocketListener->OnConnectionAccepted().BindRaw(this, &FSwitchboardListener::OnIncomingConnection);
	UE_LOG(LogSwitchboard, Display, TEXT("Started listening on %s:%d"), *SocketListener->GetLocalEndpoint().Address.ToString(), SocketListener->GetLocalEndpoint().Port);
	return true;
}

bool FSwitchboardListener::Tick()
{
	{
		TPair<FIPv4Endpoint, TSharedPtr<FSocket>> Connection;
		while (PendingConnections.Dequeue(Connection))
		{
			Connections.Add(Connection);

			FIPv4Endpoint ClientEndpoint = Connection.Key;
			LastActivityTime.FindOrAdd(ClientEndpoint, FPlatformTime::Seconds());

			SourceControl->ConnectCompleteDelegate.BindLambda([this, ClientEndpoint](bool bInSuccess, FString InErrorMessage)
			{
				OnSourceControlConnectFinished(bInSuccess, InErrorMessage, ClientEndpoint);
			});
			SourceControl->ReportRevisionCompleteDelegate.BindLambda([this, ClientEndpoint](bool bInSuccess, FString InRevision, FString InErrorMessage)
			{
				OnSourceControlReportRevisionFinished(bInSuccess, InRevision, InErrorMessage, ClientEndpoint);
			});
			SourceControl->SyncCompleteDelegate.BindLambda([this, ClientEndpoint](bool bInSuccess, FString InRevision, FString InErrorMessage)
			{
				OnSourceControlSyncFinished(bInSuccess, InRevision, InErrorMessage, ClientEndpoint);
			});
		}
	}

	for (const TPair<FIPv4Endpoint, TSharedPtr<FSocket>>& Connection: Connections)
	{
		const FIPv4Endpoint& ClientEndpoint = Connection.Key;
		const TSharedPtr<FSocket>& ClientSocket = Connection.Value;

		uint32 PendingDataSize = 0;
		while (ClientSocket->HasPendingData(PendingDataSize))
		{
			TArray<uint8> Buffer;
			Buffer.AddUninitialized(PendingDataSize);
			int32 BytesRead = 0;
			if (!ClientSocket->Recv(Buffer.GetData(), PendingDataSize, BytesRead, ESocketReceiveFlags::None))
			{
				UE_LOG(LogSwitchboard, Error, TEXT("Error while receiving data via endpoint %s"), *ClientEndpoint.ToString());
				continue;
			}

			LastActivityTime[ClientEndpoint] = FPlatformTime::Seconds();
			TArray<uint8>& MessageBuffer = ReceiveBuffer.FindOrAdd(ClientEndpoint);
			for (int32 i = 0; i < BytesRead; ++i)
			{
				MessageBuffer.Add(Buffer[i]);
				if (Buffer[i] == '\x00')
				{
					const FString Message(UTF8_TO_TCHAR(MessageBuffer.GetData()));
					ParseIncomingMessage(Message, ClientEndpoint);
					MessageBuffer.Empty();
				}
			}
		}

		if (!ScheduledTasks.IsEmpty())
		{
			TUniquePtr<FSwitchboardTask> Task;
			ScheduledTasks.Dequeue(Task);

			RunScheduledTask(*Task);
		}
	}

	CleanUpDisconnectedSockets();
	HandleRunningProcesses();

	return true;
}

bool FSwitchboardListener::ParseIncomingMessage(const FString& InMessage, const FIPv4Endpoint& InEndpoint)
{
	TUniquePtr<FSwitchboardTask> Task;
	if (CreateTaskFromCommand(InMessage, InEndpoint, Task))
	{
		if (Task->Type == ESwitchboardTaskType::Disconnect)
		{
			DisconnectTasks.Enqueue(MoveTemp(Task));
		}
		else if (Task->Type == ESwitchboardTaskType::KeepAlive)
		{
			LastActivityTime[InEndpoint] = FPlatformTime::Seconds();
		}
		else
		{
			UE_LOG(LogSwitchboard, Display, TEXT("Received %s command"), *Task->Name);
			SendMessage(CreateCommandAcceptedMessage(Task->TaskID), InEndpoint);
			ScheduledTasks.Enqueue(MoveTemp(Task));
		}
		return true;
	}
	else
	{
		FGuid MessageID;
		if (TryFindIdInBrokenMessage(InMessage, MessageID))
		{
			static FString ParseError = FString::Printf(TEXT("Could not parse message %s with ID %s"), *InMessage, *MessageID.ToString());
			SendMessage(CreateCommandDeclinedMessage(MessageID, ParseError), InEndpoint);
		}
		else
		{
			static FString ParseError = FString::Printf(TEXT("Could not parse message %s with unknown ID"), *InMessage);
			UE_LOG(LogSwitchboard, Error, TEXT("%s"), *ParseError);
			
			// just use an empty ID if we couldn't find one
			SendMessage(CreateCommandDeclinedMessage(MessageID, ParseError), InEndpoint);
		}
		return false;
	}
}

bool FSwitchboardListener::RunScheduledTask(const FSwitchboardTask& InTask)
{
	switch (InTask.Type)
	{
		case ESwitchboardTaskType::Start:
		{
			const FSwitchboardStartTask& StartTask = static_cast<const FSwitchboardStartTask&>(InTask);
			return StartProcess(StartTask);
		}
		case ESwitchboardTaskType::Kill:
		{
			const FSwitchboardKillTask& KillTask = static_cast<const FSwitchboardKillTask&>(InTask);
			FRunningProcess* Process = RunningProcesses.FindByPredicate([&KillTask](const FRunningProcess& Process)
			{
				return Process.UUID == KillTask.ProgramID;
			});
			if (!KillProcess(Process))
			{
				const FString ProgramID = KillTask.ProgramID.ToString();
				static const FString KillError = FString::Printf(TEXT("Could not find program with ID %s"), *ProgramID);
				SendMessage(CreateProgramKillFailedMessage(ProgramID, KillError), InTask.Recipient);
				UE_LOG(LogSwitchboard, Error, TEXT("%s"), *KillError);
				return false;
			}
			return true;
		}
		case ESwitchboardTaskType::KillAll:
			return KillAllProcesses();
		case ESwitchboardTaskType::ReceiveFileFromClient:
		{
			const FSwitchboardReceiveFileFromClientTask& ReceiveFileFromClientTask = static_cast<const FSwitchboardReceiveFileFromClientTask&>(InTask);
			return ReceiveFileFromClient(ReceiveFileFromClientTask);
		}
		case ESwitchboardTaskType::SendFileToClient:
		{
			const FSwitchboardSendFileToClientTask& SendFileToClientTask = static_cast<const FSwitchboardSendFileToClientTask&>(InTask);
			return SendFileToClient(SendFileToClientTask);
		}
		case ESwitchboardTaskType::VcsInit:
		{
			const FSwitchboardVcsInitTask& VcsInitTask = static_cast<const FSwitchboardVcsInitTask&>(InTask);
			return InitVersionControlSystem(VcsInitTask);
		}
		case ESwitchboardTaskType::VcsReportRevision:
		{
			const FSwitchboardVcsReportRevisionTask& VcsRevisionTask = static_cast<const FSwitchboardVcsReportRevisionTask&>(InTask);
			return ReportVersionControlRevision(VcsRevisionTask);
		}
		case ESwitchboardTaskType::VcsSync:
		{
			const FSwitchboardVcsSyncTask& VcsSyncTask = static_cast<const FSwitchboardVcsSyncTask&>(InTask);
			return SyncVersionControl(VcsSyncTask);
		}
		case ESwitchboardTaskType::KeepAlive:
		{
			return true;
		}
		default:
		{
			static const FString Response = TEXT("Unknown Command detected");
			CreateCommandDeclinedMessage(InTask.TaskID, Response);
			return false;
		}
	}
	return false;
}

bool FSwitchboardListener::StartProcess(const FSwitchboardStartTask& InRunTask)
{
	FRunningProcess NewProcess = {};
	NewProcess.Recipient = InRunTask.Recipient;
	if (!FPlatformProcess::CreatePipe(NewProcess.ReadPipe, NewProcess.WritePipe))
	{
		UE_LOG(LogSwitchboard, Error, TEXT("Could not create pipe to read process output!"));
		return false;
	}
	
	const bool bLaunchDetached = false;
	const bool bLaunchHidden = false;
	const bool bLaunchReallyHidden = false;
	const int32 PriorityModifier = 0;
	TCHAR* WorkingDirectory = nullptr;
	NewProcess.Handle = FPlatformProcess::CreateProc(*InRunTask.Command, *InRunTask.Arguments, bLaunchDetached, bLaunchHidden, bLaunchReallyHidden, &NewProcess.PID, PriorityModifier, WorkingDirectory, NewProcess.WritePipe, NewProcess.ReadPipe);

	if (NewProcess.Handle.IsValid() && FPlatformProcess::IsProcRunning(NewProcess.Handle))
	{
		UE_LOG(LogSwitchboard, Display, TEXT("Started process %d: %s %s"), NewProcess.PID, *InRunTask.Command, *InRunTask.Arguments);

		FGenericPlatformMisc::CreateGuid(NewProcess.UUID);
		RunningProcesses.Add(NewProcess);

		SendMessage(CreateProgramStartedMessage(NewProcess.UUID.ToString(), InRunTask.TaskID.ToString()), InRunTask.Recipient);
		return true;
	}
	else
	{
		const FString ErrorMsg = FString::Printf(TEXT("Could not start program %s"), *InRunTask.Command);
		UE_LOG(LogSwitchboard, Error, TEXT("%s"), *ErrorMsg);
		SendMessage(CreateProgramStartFailedMessage(ErrorMsg, InRunTask.TaskID.ToString()), InRunTask.Recipient);
		return false;
	}
}

bool FSwitchboardListener::KillProcess(FRunningProcess* InProcess)
{
	if (InProcess && InProcess->Handle.IsValid() && FPlatformProcess::IsProcRunning(InProcess->Handle))
	{
		UE_LOG(LogSwitchboard, Display, TEXT("Killing app with PID %d"), InProcess->PID);
		FPlatformProcess::TerminateProc(InProcess->Handle);

		SendMessage(CreateProgramKilledMessage(InProcess->UUID.ToString()), InProcess->Recipient);
		return true;
	}
	return false;
}

bool FSwitchboardListener::KillAllProcesses()
{
	bool bAllKilled = true;
	for (FRunningProcess& Process : RunningProcesses)
	{
		bAllKilled &= KillProcess(&Process);
	}
	return bAllKilled;
}

bool FSwitchboardListener::ReceiveFileFromClient(const FSwitchboardReceiveFileFromClientTask& InReceiveFileFromClientTask)
{
	FString Destination = InReceiveFileFromClientTask.Destination;

	if (Destination.Contains(TEXT("%TEMP%")))
	{
		const FString TempDir = FPlatformMisc::GetEnvironmentVariable(TEXT("TEMP"));
		Destination.ReplaceInline(TEXT("%TEMP%"), *TempDir);
	}
	if (Destination.Contains(TEXT("%RANDOM%")))
	{
		const FString Path = FPaths::GetPath(Destination);
		const FString Extension = FPaths::GetExtension(Destination, true);
		Destination = FPaths::CreateTempFilename(*Path, TEXT(""), *Extension);
	}
	FPlatformMisc::NormalizePath(Destination);
	FPaths::MakePlatformFilename(Destination);

	if (FPaths::FileExists(Destination))
	{
		const FString ErrorMsg = FString::Printf(TEXT("Destination %s already exist"), *Destination);
		UE_LOG(LogSwitchboard, Error, TEXT("%s"), *ErrorMsg);
		SendMessage(CreateReceiveFileFromClientFailedMessage(Destination, ErrorMsg), InReceiveFileFromClientTask.Recipient);
		return false;
	}

	TArray<uint8> DecodedFileContent = {};
	FBase64::Decode(InReceiveFileFromClientTask.FileContent, DecodedFileContent);

	UE_LOG(LogSwitchboard, Display, TEXT("Writing %d bytes to %s"), DecodedFileContent.Num(), *Destination);
	if (FFileHelper::SaveArrayToFile(DecodedFileContent, *Destination))
	{
		SendMessage(CreateReceiveFileFromClientCompletedMessage(Destination), InReceiveFileFromClientTask.Recipient);
		return true;
	}

	const FString ErrorMsg = FString::Printf(TEXT("Error while trying to write to %s"), *Destination);
	UE_LOG(LogSwitchboard, Error, TEXT("%s"), *ErrorMsg);
	SendMessage(CreateReceiveFileFromClientFailedMessage(Destination, ErrorMsg), InReceiveFileFromClientTask.Recipient);
	return false;
}

bool FSwitchboardListener::SendFileToClient(const FSwitchboardSendFileToClientTask& InSendFileToClientTask)
{
	FString SourceFilePath = InSendFileToClientTask.Source;
	FPlatformMisc::NormalizePath(SourceFilePath);
	FPaths::MakePlatformFilename(SourceFilePath);

	if (!FPaths::FileExists(SourceFilePath))
	{
		const FString ErrorMsg = FString::Printf(TEXT("Could not find file %s"), *SourceFilePath);
		UE_LOG(LogSwitchboard, Error, TEXT("%s"), *ErrorMsg);
		SendMessage(CreateSendFileToClientFailedMessage(InSendFileToClientTask.Source, ErrorMsg), InSendFileToClientTask.Recipient);
		return false;
	}

	TArray<uint8> FileContent;
	if (!FFileHelper::LoadFileToArray(FileContent, *SourceFilePath))
	{
		const FString ErrorMsg = FString::Printf(TEXT("Error reading from file %s"), *SourceFilePath);
		UE_LOG(LogSwitchboard, Error, TEXT("%s"), *ErrorMsg);
		SendMessage(CreateSendFileToClientFailedMessage(InSendFileToClientTask.Source, ErrorMsg), InSendFileToClientTask.Recipient);
		return false;
	}

	const FString EncodedFileContent = FBase64::Encode(FileContent);
	return SendMessage(CreateSendFileToClientCompletedMessage(InSendFileToClientTask.Source, EncodedFileContent), InSendFileToClientTask.Recipient);
}

bool FSwitchboardListener::InitVersionControlSystem(const FSwitchboardVcsInitTask& InVcsInitTask)
{
	if (!SourceControl->Connect(InVcsInitTask.ProviderName, InVcsInitTask.VcsSettings))
	{
		UE_LOG(LogSwitchboard, Error, TEXT("%s"), *SourceControl->GetLastError());
		SendMessage(CreateVcsInitFailedMessage(SourceControl->GetLastError()), InVcsInitTask.Recipient);
		return false;
	}
	return true;
}

bool FSwitchboardListener::ReportVersionControlRevision(const FSwitchboardVcsReportRevisionTask& InVcsRevisionTask)
{
	if (!SourceControl->ReportRevision(InVcsRevisionTask.Path))
	{
		UE_LOG(LogSwitchboard, Error, TEXT("%s"), *SourceControl->GetLastError());
		SendMessage(CreateVcsReportRevisionFailedMessage(SourceControl->GetLastError()), InVcsRevisionTask.Recipient);
		return false;
	}
	return true;
}

bool FSwitchboardListener::SyncVersionControl(const FSwitchboardVcsSyncTask& InSyncTask)
{
	if (!SourceControl->Sync(InSyncTask.Path, InSyncTask.Revision))
	{
		UE_LOG(LogSwitchboard, Error, TEXT("%s"), *SourceControl->GetLastError());
		SendMessage(CreateVcsSyncFailedMessage(SourceControl->GetLastError()), InSyncTask.Recipient);
		return false;
	}
	return true;
}

void FSwitchboardListener::CleanUpDisconnectedSockets()
{
	const double CurrentTime = FPlatformTime::Seconds();
	for (const TPair<FIPv4Endpoint, double>& LastActivity : LastActivityTime)
	{
		const FIPv4Endpoint& Client = LastActivity.Key;
		if (CurrentTime - LastActivity.Value > SecondsUntilInactiveClientDisconnect)
		{
			UE_LOG(LogSwitchboard, Warning, TEXT("Client %s has been inactive for more than %.1fs -- closing connection"), *Client.ToString(), SecondsUntilInactiveClientDisconnect);
			TUniquePtr<FSwitchboardDisconnectTask> DisconnectTask = MakeUnique<FSwitchboardDisconnectTask>(FGuid(), Client);
			DisconnectTasks.Enqueue(MoveTemp(DisconnectTask));
		}
	}

	while (!DisconnectTasks.IsEmpty())
	{
		TUniquePtr<FSwitchboardTask> Task;
		DisconnectTasks.Dequeue(Task);
		const FSwitchboardDisconnectTask& DisconnectTask = static_cast<const FSwitchboardDisconnectTask&>(*Task);
		DisconnectClient(DisconnectTask.Recipient);
	}
}

void FSwitchboardListener::DisconnectClient(const FIPv4Endpoint& InClientEndpoint)
{
	const FString Client = InClientEndpoint.ToString();
	UE_LOG(LogSwitchboard, Display, TEXT("Client %s disconnected"), *Client);
	Connections.Remove(InClientEndpoint);
	LastActivityTime.Remove(InClientEndpoint);
	ReceiveBuffer.Remove(InClientEndpoint);
}

bool FSwitchboardListener::HandleRunningProcesses()
{
	for (auto Iter = RunningProcesses.CreateIterator(); Iter; ++Iter)
	{
		FRunningProcess& Process = *Iter;
		if (Process.Handle.IsValid())
		{
			TArray<uint8> Output;
			if (FPlatformProcess::ReadPipeToArray(Process.ReadPipe, Output))
			{
				// make sure the output array always has exactly one trailing null terminator.
				// this way we can always convert to a valid string.
				if (Process.Output.Num() > 0)
				{
					Process.Output.RemoveAt(Process.Output.Num() - 1);
				}
				Process.Output.Append(Output);
				Process.Output.Add('\x00');
			}

			if (!FPlatformProcess::IsProcRunning(Process.Handle))
			{
				int32 ReturnCode = 0;
				FPlatformProcess::GetProcReturnCode(Process.Handle, &ReturnCode);
				UE_LOG(LogSwitchboard, Display, TEXT("Process exited with returncode: %d"), ReturnCode);

				const FString ProcessOutput(UTF8_TO_TCHAR(Process.Output.GetData()));
				SendMessage(CreateProgramEndedMessage(Process.UUID.ToString(), ReturnCode, ProcessOutput), Process.Recipient);

				FPlatformProcess::CloseProc(Process.Handle);
				FPlatformProcess::ClosePipe(Process.ReadPipe, Process.WritePipe);

				Iter.RemoveCurrent();
			}
		}
	}

	return true;
}

bool FSwitchboardListener::OnIncomingConnection(FSocket* InSocket, const FIPv4Endpoint& InEndpoint)
{
	UE_LOG(LogSwitchboard, Display, TEXT("Incoming connection via %s:%d"), *InEndpoint.Address.ToString(), InEndpoint.Port);

	InSocket->SetNoDelay(true);
	PendingConnections.Enqueue(TPair<FIPv4Endpoint, TSharedPtr<FSocket>>(InEndpoint, MakeShareable(InSocket)));

	return true;
}

bool FSwitchboardListener::SendMessage(const FString& InMessage, const FIPv4Endpoint& InEndpoint)
{
	if (Connections.Contains(InEndpoint))
	{
		TSharedPtr<FSocket> ClientSocket = Connections[InEndpoint];
		if (!ClientSocket.IsValid())
		{
			return false;
		}

		UE_LOG(LogSwitchboard, Verbose, TEXT("Sending message %s"), *InMessage);
		int32 BytesSent = 0;
		return ClientSocket->Send((uint8*)TCHAR_TO_UTF8(*InMessage), InMessage.Len() + 1, BytesSent);
	}

	// this happens when a client disconnects while a task it had issued is not finished
	UE_LOG(LogSwitchboard, Verbose, TEXT("Trying to send message to disconnected client %s"), *InEndpoint.ToString());
	return false;
}

void FSwitchboardListener::OnSourceControlConnectFinished(bool bInSuccess, FString InErrorMessage, const FIPv4Endpoint& InEndpoint)
{
	if (bInSuccess)
	{
		SendMessage(CreateVcsInitCompletedMessage(), InEndpoint);
	}
	else
	{
		SendMessage(CreateVcsInitFailedMessage(InErrorMessage), InEndpoint);
	}
}

void FSwitchboardListener::OnSourceControlReportRevisionFinished(bool bInSuccess, FString InRevision, FString InErrorMessage, const FIPv4Endpoint& InEndpoint)
{
	if (bInSuccess)
	{
		SendMessage(CreateVcsReportRevisionCompletedMessage(InRevision), InEndpoint);
	}
	else
	{
		SendMessage(CreateVcsReportRevisionFailedMessage(InErrorMessage), InEndpoint);
	}
}

void FSwitchboardListener::OnSourceControlSyncFinished(bool bInSuccess, FString InRevision, FString InErrorMessage, const FIPv4Endpoint& InEndpoint)
{
	if (bInSuccess)
	{
		SendMessage(CreateVcsSyncCompletedMessage(InRevision), InEndpoint);
	}
	else
	{
		SendMessage(CreateVcsSyncFailedMessage(InErrorMessage), InEndpoint);
	}
}
