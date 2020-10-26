// Copyright Epic Games, Inc. All Rights Reserved.

#include "SwitchboardListener.h"

#include "SwitchboardListenerApp.h"
#include "SwitchboardMessageFuture.h"
#include "SwitchboardPacket.h"
#include "SwitchboardProtocol.h"
#include "SwitchboardTasks.h"
#include "SyncStatus.h"

#include "Async/Async.h"
#include "Async/AsyncWork.h"
#include "Common/TcpListener.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "IPAddress.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Templates/Atomic.h"

#if PLATFORM_WINDOWS

#pragma warning(push)
#pragma warning(disable : 4005)	// Disable macro redefinition warning for compatibility with Windows SDK 8+

#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include <shellapi.h>
#include "nvapi.h"
#include "Windows/HideWindowsPlatformTypes.h"

#pragma warning(pop)

#endif // PLATFORM_WINDOWS


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
	FString Path;
	FString Name;
	FString Caller;

	TAtomic<bool> bPendingKill;

	FRunningProcess()
		: bPendingKill(false)
	{}

	FRunningProcess(const FRunningProcess& InProcess)
	{
		PID       = InProcess.PID;
		UUID      = InProcess.UUID;
		Handle    = InProcess.Handle;
		WritePipe = InProcess.WritePipe;
		ReadPipe  = InProcess.ReadPipe;
		Output    = InProcess.Output;
		Recipient = InProcess.Recipient;
		Path      = InProcess.Path;
		Name      = InProcess.Name;
		Caller    = InProcess.Caller;

		bPendingKill.Store(InProcess.bPendingKill);
	}
};

FSwitchboardListener::FSwitchboardListener(const FIPv4Endpoint& InEndpoint)
	: Endpoint(MakeUnique<FIPv4Endpoint>(InEndpoint))
	, SocketListener(nullptr)
{
#if PLATFORM_WINDOWS
	// initialize NvAPI
	{
		const NvAPI_Status Result = NvAPI_Initialize();
		if (Result != NVAPI_OK)
		{
			UE_LOG(LogSwitchboard, Fatal, TEXT("NvAPI_Initialize failed. Error code: %d"), Result);
		}
	}
#endif // PLATFORM_WINDOWS
}

FSwitchboardListener::~FSwitchboardListener()
{
	KillAllProcessesNow();
}

bool FSwitchboardListener::Init()
{
	SocketListener = MakeUnique<FTcpListener>(*Endpoint, FTimespan::FromSeconds(1), false);
	if (SocketListener->IsActive())
	{
		SocketListener->OnConnectionAccepted().BindRaw(this, &FSwitchboardListener::OnIncomingConnection);
		UE_LOG(LogSwitchboard, Display, TEXT("Started listening on %s:%d"), *SocketListener->GetLocalEndpoint().Address.ToString(), SocketListener->GetLocalEndpoint().Port);
		return true;
	}

	UE_LOG(LogSwitchboard, Error, TEXT("Could not create Tcp Listener!"));
	return false;
}

bool FSwitchboardListener::Tick()
{
	// Dequeue pending connections
	{
		TPair<FIPv4Endpoint, TSharedPtr<FSocket>> Connection;
		while (PendingConnections.Dequeue(Connection))
		{
			Connections.Add(Connection);

			FIPv4Endpoint ClientEndpoint = Connection.Key;
			LastActivityTime.FindOrAdd(ClientEndpoint, FPlatformTime::Seconds());

			// Send current state upon connection
			{
				FSwitchboardStatePacket StatePacket;

				for (const FRunningProcess& RunningProcess : RunningProcesses)
				{
					FSwitchboardStateRunningProcess StateRunningProcess;

					StateRunningProcess.Uuid = RunningProcess.UUID.ToString();
					StateRunningProcess.Name = RunningProcess.Name;
					StateRunningProcess.Path = RunningProcess.Path;
					StateRunningProcess.Caller = RunningProcess.Caller;

					StatePacket.RunningProcesses.Add(MoveTemp(StateRunningProcess));
				}

				SendMessage(CreateMessage(StatePacket), ClientEndpoint);
			}
		}
	}

	// Parse incoming data from remote connections
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
	}

	// Run the next queued task
	if (!ScheduledTasks.IsEmpty())
	{
		TUniquePtr<FSwitchboardTask> Task;
		ScheduledTasks.Dequeue(Task);
		RunScheduledTask(*Task);
	}

	CleanUpDisconnectedSockets();
	HandleRunningProcesses(RunningProcesses, true);
	HandleRunningProcesses(FlipModeMonitors, false);
	SendMessageFutures();

	return true;
}

bool FSwitchboardListener::ParseIncomingMessage(const FString& InMessage, const FIPv4Endpoint& InEndpoint)
{
	TUniquePtr<FSwitchboardTask> Task;
	bool bEcho = true;
	if (CreateTaskFromCommand(InMessage, InEndpoint, Task, bEcho))
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
			if (bEcho)
			{
				UE_LOG(LogSwitchboard, Display, TEXT("Received %s command"), *Task->Name);
			}

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
			return KillProcess(KillTask);
		}
		case ESwitchboardTaskType::KillAll:
		{
			const FSwitchboardKillAllTask& KillAllTask = static_cast<const FSwitchboardKillAllTask&>(InTask);
			return KillAllProcesses(KillAllTask);
		}
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
		case ESwitchboardTaskType::KeepAlive:
		{
			return true;
		}
		case ESwitchboardTaskType::GetSyncStatus:
		{
			const FSwitchboardGetSyncStatusTask& GetSyncStatusTask = static_cast<const FSwitchboardGetSyncStatusTask&>(InTask);
			return GetSyncStatus(GetSyncStatusTask);
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
	NewProcess.Path = InRunTask.Command;
	NewProcess.Name = InRunTask.Name;
	NewProcess.Caller = InRunTask.Caller;

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

	NewProcess.Handle = FPlatformProcess::CreateProc(
		*InRunTask.Command, 
		*InRunTask.Arguments, 
		bLaunchDetached, 
		bLaunchHidden, 
		bLaunchReallyHidden, 
		&NewProcess.PID, 
		PriorityModifier, 
		WorkingDirectory, 
		NewProcess.WritePipe, 
		NewProcess.ReadPipe
	);

	if (!NewProcess.Handle.IsValid() || !FPlatformProcess::IsProcRunning(NewProcess.Handle))
	{
		// Close process in case it just didn't run
		FPlatformProcess::CloseProc(NewProcess.Handle);

		// close pipes
		FPlatformProcess::ClosePipe(NewProcess.ReadPipe, NewProcess.WritePipe);

		// log error
		const FString ErrorMsg = FString::Printf(TEXT("Could not start program %s"), *InRunTask.Command);
		UE_LOG(LogSwitchboard, Error, TEXT("%s"), *ErrorMsg);

		// notify Switchboard
		SendMessage(CreateProgramStartFailedMessage(ErrorMsg, InRunTask.TaskID.ToString()), InRunTask.Recipient);

		return false;
	}

	UE_LOG(LogSwitchboard, Display, TEXT("Started process %d: %s %s"), NewProcess.PID, *InRunTask.Command, *InRunTask.Arguments);

	FGenericPlatformMisc::CreateGuid(NewProcess.UUID);
	RunningProcesses.Add(MoveTemp(NewProcess));

	SendMessage(CreateProgramStartedMessage(NewProcess.UUID.ToString(), InRunTask.TaskID.ToString()), InRunTask.Recipient);
	return true;

}

bool FSwitchboardListener::KillProcess(const FSwitchboardKillTask& KillTask)
{
	if (EquivalentTaskFutureExists(KillTask.GetEquivalenceHash()))
	{
		SendMessage(CreateTaskDeclinedMessage(KillTask, "Duplicate"), KillTask.Recipient);
		return false;
	}

	// Look in RunningProcesses

	FRunningProcess* Process = RunningProcesses.FindByPredicate([&KillTask](const FRunningProcess& Process) 
	{
		return !Process.bPendingKill && (Process.UUID == KillTask.ProgramID);
	});

	if (Process)
	{
		Process->bPendingKill = true;
	}

	// Look in FlipModeMonitors

	FRunningProcess* FlipModeMonitor = FlipModeMonitors.FindByPredicate([&](const FRunningProcess& FlipMonitor)
	{
		return !FlipMonitor.bPendingKill && (FlipMonitor.UUID == KillTask.ProgramID);
	});

	if (FlipModeMonitor)
	{
		FlipModeMonitor->bPendingKill = true;
	}

	// Create our future message

	FSwitchboardMessageFuture MessageFuture;

	MessageFuture.TaskType = KillTask.Type;
	MessageFuture.InEndpoint = KillTask.Recipient;
	MessageFuture.EquivalenceHash = KillTask.GetEquivalenceHash();

	const FGuid UUID = KillTask.ProgramID;

	MessageFuture.Future = Async(EAsyncExecution::Thread, [=]() {

		const float SoftKillTimeout = 2.0f;

		const bool bKilledProcess = KillProcessNow(Process, SoftKillTimeout);
		KillProcessNow(FlipModeMonitor, SoftKillTimeout);

		// Clear bPendingKill

		Process->bPendingKill = false;

		if (FlipModeMonitor)
		{
			FlipModeMonitor->bPendingKill = false;
		}

		// Return message

		const FString ProgramID = UUID.ToString();

		if (!bKilledProcess)
		{
			const FString KillError = FString::Printf(TEXT("Could not kill program with ID %s"), *ProgramID);
			return CreateProgramKillFailedMessage(ProgramID, *KillError);
		}

		return CreateProgramKilledMessage(ProgramID);
	});

	// Queue it to be sent when ready
	MessagesFutures.Emplace(MoveTemp(MessageFuture));

	return true;
}


bool FSwitchboardListener::KillProcessNow(FRunningProcess* InProcess, float SoftKillTimeout)
{
	if (InProcess && InProcess->Handle.IsValid() && FPlatformProcess::IsProcRunning(InProcess->Handle))
	{
		UE_LOG(LogSwitchboard, Display, TEXT("Killing app with PID %d"), InProcess->PID);

#if PLATFORM_WINDOWS
		// try a soft kill first
		if(SoftKillTimeout > 0)
		{
			const FString Params = FString::Printf(TEXT("/PID %d"), InProcess->PID);

			FString OutStdOut;

			FPlatformProcess::ExecProcess(TEXT("TASKKILL"), *Params, nullptr, &OutStdOut, nullptr);

			const double TimeoutTime = FPlatformTime::Seconds() + SoftKillTimeout;
			const float SleepTime = 0.050f;

			while(FPlatformTime::Seconds() < TimeoutTime && FPlatformProcess::IsProcRunning(InProcess->Handle))
			{
				FPlatformProcess::Sleep(SleepTime);
			}
		}
#endif // PLATFORM_WINDOWS

		if (FPlatformProcess::IsProcRunning(InProcess->Handle))
		{
			const bool bKillTree = true;
			FPlatformProcess::TerminateProc(InProcess->Handle, bKillTree);
		}

		// Pipes will be closed in HandleRunningProcesses
		return true;
	}

	return false;
}

void FSwitchboardListener::KillAllProcessesNow()
{
	for (FRunningProcess& Process : RunningProcesses)
	{
		while (Process.bPendingKill)
		{
			FPlatformProcess::Sleep(0.050);
		}

		KillProcessNow(&Process);
	}

	for (FRunningProcess& Process : FlipModeMonitors)
	{
		while (Process.bPendingKill)
		{
			FPlatformProcess::Sleep(0.050);
		}

		KillProcessNow(&Process);
	}
}

bool FSwitchboardListener::KillAllProcesses(const FSwitchboardKillAllTask& KillAllTask)
{
	for (FRunningProcess& Process : RunningProcesses)
	{
		FSwitchboardKillTask Task(KillAllTask.TaskID, KillAllTask.Recipient, Process.UUID);
		KillProcess(Task);
	}

	return true;
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

#if PLATFORM_WINDOWS
static FCriticalSection SwitchboardListenerMutexNvapi;
#endif // PLATFORM_WINDOWS

#if PLATFORM_WINDOWS
static void FillOutSyncTopologies(FSyncStatus& SyncStatus)
{
	FScopeLock LockNvapi(&SwitchboardListenerMutexNvapi);

	// Normally there is a single sync card. BUT an RTX Server could have more, and we need to account for that.

	// Detect sync cards

	NvU32 GSyncCount = 0;
	NvGSyncDeviceHandle GSyncHandles[NVAPI_MAX_GSYNC_DEVICES];
	NvAPI_GSync_EnumSyncDevices(GSyncHandles, &GSyncCount); // GSyncCount will be zero if error, so no need to check error.

	for (NvU32 GSyncIdx = 0; GSyncIdx < GSyncCount; GSyncIdx++)
	{
		NvU32 GSyncGPUCount = 0;
		NvU32 GSyncDisplayCount = 0;

		// gather info first with null data pointers, just to get the count and subsequently allocate necessary memory.
		{
			const NvAPI_Status Result = NvAPI_GSync_GetTopology(GSyncHandles[GSyncIdx], &GSyncGPUCount, nullptr, &GSyncDisplayCount, nullptr);

			if (Result != NVAPI_OK)
			{
				UE_LOG(LogSwitchboard, Warning, TEXT("NvAPI_GSync_GetTopology failed. Error code: %d"), Result);
				continue;
			}
		}

		// allocate memory for data
		TArray<NV_GSYNC_GPU> GSyncGPUs;
		TArray<NV_GSYNC_DISPLAY> GSyncDisplays;
		{
			GSyncGPUs.SetNumUninitialized(GSyncGPUCount, false);

			for (NvU32 GSyncGPUIdx = 0; GSyncGPUIdx < GSyncGPUCount; GSyncGPUIdx++)
			{
				GSyncGPUs[GSyncGPUIdx].version = NV_GSYNC_GPU_VER;
			}

			GSyncDisplays.SetNumUninitialized(GSyncDisplayCount, false);

			for (NvU32 GSyncDisplayIdx = 0; GSyncDisplayIdx < GSyncDisplayCount; GSyncDisplayIdx++)
			{
				GSyncDisplays[GSyncDisplayIdx].version = NV_GSYNC_DISPLAY_VER;
			}
		}

		// get real info
		{
			const NvAPI_Status Result = NvAPI_GSync_GetTopology(GSyncHandles[GSyncIdx], &GSyncGPUCount, GSyncGPUs.GetData(), &GSyncDisplayCount, GSyncDisplays.GetData());

			if (Result != NVAPI_OK)
			{
				UE_LOG(LogSwitchboard, Warning, TEXT("NvAPI_GSync_GetTopology failed. Error code: %d"), Result);
				continue;
			}
		}

		// Build outbound structure

		FSyncTopo SyncTopo;

		for (NvU32 GpuIdx = 0; GpuIdx < GSyncGPUCount; GpuIdx++)
		{
			FSyncGpu SyncGpu;

			SyncGpu.bIsSynced = GSyncGPUs[GpuIdx].isSynced;
			SyncGpu.Connector = int32(GSyncGPUs[GpuIdx].connector);

			SyncTopo.SyncGpus.Emplace(SyncGpu);
		}

		for (NvU32 DisplayIdx = 0; DisplayIdx < GSyncDisplayCount; DisplayIdx++)
		{
			FSyncDisplay SyncDisplay;

			switch (GSyncDisplays[DisplayIdx].syncState)
			{
			case NVAPI_GSYNC_DISPLAY_SYNC_STATE_UNSYNCED:
				SyncDisplay.SyncState = TEXT("Unsynced");
				break;
			case NVAPI_GSYNC_DISPLAY_SYNC_STATE_SLAVE:
				SyncDisplay.SyncState = TEXT("Slave");
				break;
			case NVAPI_GSYNC_DISPLAY_SYNC_STATE_MASTER:
				SyncDisplay.SyncState = TEXT("Master");
				break;
			default:
				SyncDisplay.SyncState = TEXT("Unknown");
				break;
			}

			// get color information for each display
			{
				NV_COLOR_DATA ColorData;

				ColorData.version = NV_COLOR_DATA_VER;
				ColorData.cmd = NV_COLOR_CMD_GET;
				ColorData.size = sizeof(NV_COLOR_DATA);

				const NvAPI_Status Result = NvAPI_Disp_ColorControl(GSyncDisplays[DisplayIdx].displayId, &ColorData);

				if (Result == NVAPI_OK)
				{
					SyncDisplay.Bpc = ColorData.data.bpc;
					SyncDisplay.Depth = ColorData.data.depth;
					SyncDisplay.ColorFormat = ColorData.data.colorFormat;
				}
			}

			SyncTopo.SyncDisplays.Emplace(SyncDisplay);
		}

		// Sync Status Parameters
		{
			NV_GSYNC_STATUS_PARAMS GSyncStatusParams;
			GSyncStatusParams.version = NV_GSYNC_STATUS_PARAMS_VER;

			const NvAPI_Status Result = NvAPI_GSync_GetStatusParameters(GSyncHandles[GSyncIdx], &GSyncStatusParams);

			if (Result != NVAPI_OK)
			{
				UE_LOG(LogSwitchboard, Warning, TEXT("NvAPI_GSync_GetStatusParameters failed. Error code: %d"), Result);
				continue;
			}

			SyncTopo.SyncStatusParams.RefreshRate = GSyncStatusParams.refreshRate;
			SyncTopo.SyncStatusParams.HouseSyncIncoming = GSyncStatusParams.houseSyncIncoming;
			SyncTopo.SyncStatusParams.bHouseSync = !!GSyncStatusParams.bHouseSync;
			SyncTopo.SyncStatusParams.bInternalSlave = GSyncStatusParams.bInternalSlave;
		}

		// Sync Control Parameters
		{
			NV_GSYNC_CONTROL_PARAMS GSyncControlParams;
			GSyncControlParams.version = NV_GSYNC_CONTROL_PARAMS_VER;

			const NvAPI_Status Result = NvAPI_GSync_GetControlParameters(GSyncHandles[GSyncIdx], &GSyncControlParams);

			if (Result != NVAPI_OK)
			{
				UE_LOG(LogSwitchboard, Warning, TEXT("NvAPI_GSync_GetControlParameters failed. Error code: %d"), Result);
				continue;
			}

			SyncTopo.SyncControlParams.bInterlaced = !!GSyncControlParams.interlaceMode;
			SyncTopo.SyncControlParams.bSyncSourceIsOutput = !!GSyncControlParams.syncSourceIsOutput;
			SyncTopo.SyncControlParams.Interval = GSyncControlParams.interval;
			SyncTopo.SyncControlParams.Polarity = GSyncControlParams.polarity;
			SyncTopo.SyncControlParams.Source = GSyncControlParams.source;
			SyncTopo.SyncControlParams.VMode = GSyncControlParams.vmode;

			SyncTopo.SyncControlParams.SyncSkew.MaxLines = GSyncControlParams.syncSkew.maxLines;
			SyncTopo.SyncControlParams.SyncSkew.MinPixels = GSyncControlParams.syncSkew.minPixels;
			SyncTopo.SyncControlParams.SyncSkew.NumLines = GSyncControlParams.syncSkew.numLines;
			SyncTopo.SyncControlParams.SyncSkew.NumPixels = GSyncControlParams.syncSkew.numPixels;

			SyncTopo.SyncControlParams.StartupDelay.MaxLines = GSyncControlParams.startupDelay.maxLines;
			SyncTopo.SyncControlParams.StartupDelay.MinPixels = GSyncControlParams.startupDelay.minPixels;
			SyncTopo.SyncControlParams.StartupDelay.NumLines = GSyncControlParams.startupDelay.numLines;
			SyncTopo.SyncControlParams.StartupDelay.NumPixels = GSyncControlParams.startupDelay.numPixels;
		}

		SyncStatus.SyncTopos.Emplace(SyncTopo);
	}
}
#endif // PLATFORM_WINDOWS

#if PLATFORM_WINDOWS
static void FillOutDriverVersion(FSyncStatus& SyncStatus)
{
	NvU32 DriverVersion;
	NvAPI_ShortString BuildBranchString;

	const NvAPI_Status Result = NvAPI_SYS_GetDriverAndBranchVersion(&DriverVersion, BuildBranchString);

	if (Result != NVAPI_OK)
	{
		UE_LOG(LogSwitchboard, Warning, TEXT("NvAPI_SYS_GetDriverAndBranchVersion failed. Error code: %d"), Result);
		return;
	}

	SyncStatus.DriverVersion = DriverVersion;
	SyncStatus.DriverBranch = UTF8_TO_TCHAR(BuildBranchString);
}
#endif // PLATFORM_WINDOWS

#if PLATFORM_WINDOWS
static void FillOutTaskbarAutoHide(FSyncStatus& SyncStatus)
{
	APPBARDATA AppBarData;

	AppBarData.cbSize = sizeof(APPBARDATA);
	AppBarData.hWnd = nullptr;
	
	const UINT Result = UINT(SHAppBarMessage(ABM_GETSTATE, &AppBarData));
	
	if (Result == ABS_AUTOHIDE)
	{
		SyncStatus.Taskbar = TEXT("AutoHide");
	}
	else
	{
		SyncStatus.Taskbar = TEXT("OnTop");
	}
}
#endif // PLATFORM_WINDOWS


#if PLATFORM_WINDOWS
static void FillOutMosaicTopologies(FSyncStatus& SyncStatus)
{
	FScopeLock LockNvapi(&SwitchboardListenerMutexNvapi);

	NvU32 GridCount = 0;
	TArray<NV_MOSAIC_GRID_TOPO> GridTopologies;

	// count how many grids
	{
		const NvAPI_Status Result = NvAPI_Mosaic_EnumDisplayGrids(nullptr, &GridCount);

		if (Result != NVAPI_OK)
		{
			UE_LOG(LogSwitchboard, Warning, TEXT("NvAPI_Mosaic_EnumDisplayGrids failed. Error code: %d"), Result);
			return;
		}
	}

	// get the grids
	{
		GridTopologies.SetNumUninitialized(GridCount, false);

		for (NvU32 TopoIdx = 0; TopoIdx < GridCount; TopoIdx++)
		{
			GridTopologies[TopoIdx].version = NV_MOSAIC_GRID_TOPO_VER;
		}

		const NvAPI_Status Result = NvAPI_Mosaic_EnumDisplayGrids(GridTopologies.GetData(), &GridCount);

		if (Result != NVAPI_OK)
		{
			UE_LOG(LogSwitchboard, Warning, TEXT("NvAPI_Mosaic_EnumDisplayGrids failed. Error code: %d"), Result);
			return;
		}

		for (NvU32 TopoIdx = 0; TopoIdx < GridCount; TopoIdx++)
		{
			FMosaicTopo MosaicTopo;
			NV_MOSAIC_GRID_TOPO& GridTopo = GridTopologies[TopoIdx];

			MosaicTopo.Columns = GridTopo.columns;
			MosaicTopo.Rows = GridTopo.rows;
			MosaicTopo.DisplayCount = GridTopo.displayCount;

			MosaicTopo.DisplaySettings.Bpp = GridTopo.displaySettings.bpp;
			MosaicTopo.DisplaySettings.Freq = GridTopo.displaySettings.freq;
			MosaicTopo.DisplaySettings.Height = GridTopo.displaySettings.height;
			MosaicTopo.DisplaySettings.Width = GridTopo.displaySettings.width;

			SyncStatus.MosaicTopos.Emplace(MosaicTopo);
		}
	}
}
#endif // PLATFORM_WINDOWS

FRunningProcess* FSwitchboardListener::FindOrStartFlipModeMonitorForUUID(const FGuid& UUID)
{
	// See if the associated FlipModeMonitor is running
	{
		FRunningProcess* FlipModeMonitor = FlipModeMonitors.FindByPredicate([&](const FRunningProcess& Process)
		{
			return Process.UUID == UUID;
		});

		if (FlipModeMonitor)
		{
			return FlipModeMonitor;
		}
	}

	// It wasn't in there, so let's find our target process

	const FRunningProcess* Process = RunningProcesses.FindByPredicate([&](const FRunningProcess& Process)
	{
		return Process.UUID == UUID;
	});

	// If the target process does not exist, no point in continuing
	if (!Process)
	{
		return nullptr;
	}

	// Ok, we need to create our monitor.

	FRunningProcess MonitorProcess = {};

	if (!FPlatformProcess::CreatePipe(MonitorProcess.ReadPipe, MonitorProcess.WritePipe))
	{
		UE_LOG(LogSwitchboard, Error, TEXT("Could not create pipe to read MonitorProcess output!"));
		return nullptr;
	}

	const bool bLaunchDetached = true;
	const bool bLaunchHidden = false;
	const bool bLaunchReallyHidden = false;
	const int32 PriorityModifier = 0;
	const TCHAR* WorkingDirectory = nullptr;

	MonitorProcess.Path = FPaths::EngineSourceDir() / TEXT("Programs") / TEXT("SwitchboardListener") / TEXT("ThirdParty") / TEXT("PresentMon") / TEXT("PresentMon64-1.5.2.exe");

	FString Arguments = 
		FString::Printf(TEXT("-session_name session_%d -output_stdout -dont_restart_as_admin -terminate_on_proc_exit -stop_existing_session -process_id %d"), 
		Process->PID, Process->PID);

	MonitorProcess.Handle = FPlatformProcess::CreateProc(
		*MonitorProcess.Path,
		*Arguments,
		bLaunchDetached,
		bLaunchHidden,
		bLaunchReallyHidden,
		&MonitorProcess.PID,
		PriorityModifier,
		WorkingDirectory,
		MonitorProcess.WritePipe,
		MonitorProcess.ReadPipe
	);

	if (!MonitorProcess.Handle.IsValid() || !FPlatformProcess::IsProcRunning(MonitorProcess.Handle))
	{
		// Close process in case it just didn't run
		FPlatformProcess::CloseProc(MonitorProcess.Handle);

		// Close unused pipes
		FPlatformProcess::ClosePipe(MonitorProcess.ReadPipe, MonitorProcess.WritePipe);

		// Log error
		const FString ErrorMsg = FString::Printf(TEXT("Could not start FlipMode monitor  %s"), *MonitorProcess.Path);
		UE_LOG(LogSwitchboard, Error, TEXT("%s"), *ErrorMsg);

		return nullptr;
	}

	// Log success
	UE_LOG(LogSwitchboard, Display, TEXT("Started FlipMode monitor %d: %s %s"), MonitorProcess.PID, *MonitorProcess.Path, *Arguments);

	// The UUID corresponds to the program being monitored. This will be used when looking for the Monitor of a given process.
	// The monitor auto-closes when monitored program closes.
	MonitorProcess.UUID = Process->UUID;

	FlipModeMonitors.Add(MoveTemp(MonitorProcess));

	return &FlipModeMonitors.Last();
}

#if PLATFORM_WINDOWS
static void FillOutFlipMode(FSyncStatus& SyncStatus, FRunningProcess* FlipModeMonitor)
{
	// See if the flip monitor is still there.
	if (!FlipModeMonitor || !FlipModeMonitor->Handle.IsValid())
	{
		SyncStatus.FlipModeHistory.Add(TEXT("n/a")); // this informs Switchboard that data is not valid
		return;
	}

	// Get stdout.
	const FString StdOut(UTF8_TO_TCHAR(FlipModeMonitor->Output.GetData()));

	// Clear out the StdOut array.
	FlipModeMonitor->Output.Empty();

	// Split into lines

	TArray<FString> Lines;
	StdOut.ParseIntoArrayLines(Lines, false);

	// Interpret the output as follows:
	//
	// Application,ProcessID,SwapChainAddress,Runtime,SyncInterval,PresentFlags,AllowsTearing,PresentMode,Dropped,
	// TimeInSeconds,MsBetweenPresents,MsBetweenDisplayChange,MsInPresentAPI,MsUntilRenderComplete,MsUntilDisplayed
	//
	// e.g.
	//   "UE4Editor.exe,10916,0x0000022096A0F830,DXGI,0,512,0,Composed: Flip,1,3.753577,22.845,0.000,0.880,0.946,0.000"

	TArray<FString> Fields;

	for (const FString& Line : Lines)
	{
		//UE_LOG(LogSwitchboard, Warning, TEXT("PresentMon: %s"), *Line);
		Line.ParseIntoArray(Fields, TEXT(","), false);
		
		if (Fields.Num() != 15)
		{
			continue;
		}

		const int32 PresentMonIdx = 7;

		SyncStatus.FlipModeHistory.Add(Fields[PresentMonIdx]); // The first one will be "PresentMode". This is ok. 
	}
}
#endif // PLATFORM_WINDOWS

#if PLATFORM_WINDOWS
static TArray<FString> RegistryGetSubkeys(const HKEY Key)
{
	TArray<FString> Subkeys;
	const uint32 MaxKeyLength = 1024;
	TCHAR SubkeyName[MaxKeyLength];
	DWORD KeyLength = MaxKeyLength;

	while (!RegEnumKeyEx(Key, Subkeys.Num(), SubkeyName, &KeyLength, nullptr, nullptr, nullptr, nullptr))
	{
		Subkeys.Add(SubkeyName);
		KeyLength = MaxKeyLength;
	}

	return Subkeys;
}
#endif // PLATFORM_WINDOWS

#if PLATFORM_WINDOWS
static TArray<FString> RegistryGetValueNames(const HKEY Key)
{
	TArray<FString> Names;
	const uint32 MaxLength = 1024;
	TCHAR ValueName[MaxLength];
	DWORD ValueLength = MaxLength;

	while (!RegEnumValue(Key, Names.Num(), ValueName, &ValueLength, nullptr, nullptr, nullptr, nullptr))
	{
		Names.Add(ValueName);
		ValueLength = MaxLength;
	}

	return Names;
}
#endif // PLATFORM_WINDOWS

#if PLATFORM_WINDOWS
static FString RegistryGetStringValueData(const HKEY Key, const FString& ValueName)
{
	const uint32 MaxLength = 4096;
	TCHAR ValueData[MaxLength];
	DWORD ValueLength = MaxLength;

	if (RegQueryValueEx(Key, *ValueName, 0, 0, LPBYTE(ValueData), &ValueLength))
	{
		return TEXT("");
	}

	ValueData[MaxLength - 1] = '\0';

	return FString(ValueData);
}
#endif // PLATFORM_WINDOWS

#if PLATFORM_WINDOWS
static void FillOutDisableFullscreenOptimizationForProcess(FSyncStatus& SyncStatus, const FRunningProcess* Process)
{
	// Reset output array just in case
	SyncStatus.ProgramLayers.Reset();

	// No point in continuing if there is no process to get the flags for.
	if (!Process)
	{
		return;
	}

	// This is the absolute path of the program we'll be looking for in the registry
	const FString ProcessAbsolutePath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*Process->Path);

	// We expect program layers to be in a location like the following:
	//   Computer\HKEY_USERS\S-1-5-21-4092791292-903758629-2457117007-1001\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers
	// But the guid looking number above may vary.

	// So we try all the keys immediately under HKEY_USERS
	TArray<FString> KeyPaths = RegistryGetSubkeys(HKEY_USERS);

	for (const FString& KeyPath : KeyPaths)
	{
		const FString LayersKeyPath = KeyPath + TEXT("\\Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers");

		HKEY LayersKey;
		
		// Check if the key exists
		if (RegOpenKeyExW(HKEY_USERS, *LayersKeyPath, 0, KEY_READ, &LayersKey))
		{
			continue;
		}

		// If the key exists, the Value Names are the paths to the programs

		const TArray<FString> ProgramPaths = RegistryGetValueNames(LayersKey);

		for (const FString& ProgramPath : ProgramPaths)
		{
			const FString ProgramAbsPath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*ProgramPath);
		
			// Check if this is the program we're looking for
			if (ProcessAbsolutePath != ProgramAbsPath)
			{
				continue;
			}

			// If so, get the layers from the Value Data.

			const FString ProgramLayers = RegistryGetStringValueData(LayersKey, ProgramPath);
			ProgramLayers.ParseIntoArray(SyncStatus.ProgramLayers, TEXT(" "));

			// No need to look further.
			break;
		}

		RegCloseKey(LayersKey);

		// If the already have the data we need, we can break.
		if (SyncStatus.ProgramLayers.Num())
		{
			break;
		}
	}
}
#endif // PLATFORM_WINDOWS

bool FSwitchboardListener::EquivalentTaskFutureExists(uint32 TaskEquivalenceHash) const
{
	return !!MessagesFutures.FindByPredicate([=](const FSwitchboardMessageFuture& MessageFuture)
	{
		return MessageFuture.EquivalenceHash == TaskEquivalenceHash;
	});
}

bool FSwitchboardListener::GetSyncStatus(const FSwitchboardGetSyncStatusTask& InGetSyncStatusTask)
{
#if PLATFORM_WINDOWS
	// Reject request if an equivalent one is already in our future
	if (EquivalentTaskFutureExists(InGetSyncStatusTask.GetEquivalenceHash()))
	{
		SendMessage(CreateTaskDeclinedMessage(InGetSyncStatusTask, "Duplicate"), InGetSyncStatusTask.Recipient);
		return false;
	}

	TSharedRef<FSyncStatus, ESPMode::ThreadSafe> SyncStatus = MakeShared<FSyncStatus, ESPMode::ThreadSafe>(); // Smart pointer to avoid potentially bigger copy to lambda below.

	// We need to run these on this thread to avoid threading issues.
	FillOutFlipMode(SyncStatus.Get(), FindOrStartFlipModeMonitorForUUID(InGetSyncStatusTask.ProgramID));

	// Fill out fullscreen optimization setting
	{
		FRunningProcess* Process = RunningProcesses.FindByPredicate([&](const FRunningProcess& Process)
		{
			return Process.UUID == InGetSyncStatusTask.ProgramID;
		});

		FillOutDisableFullscreenOptimizationForProcess(SyncStatus.Get(), Process);
	}

	// Create our future message

	FSwitchboardMessageFuture MessageFuture;

	MessageFuture.TaskType = InGetSyncStatusTask.Type;
	MessageFuture.InEndpoint = InGetSyncStatusTask.Recipient;
	MessageFuture.EquivalenceHash = InGetSyncStatusTask.GetEquivalenceHash();

	MessageFuture.Future = Async(EAsyncExecution::Thread, [=]() {
		FillOutDriverVersion(SyncStatus.Get());
		FillOutTaskbarAutoHide(SyncStatus.Get());
		FillOutSyncTopologies(SyncStatus.Get());
		FillOutMosaicTopologies(SyncStatus.Get());
		return CreateSyncStatusMessage(SyncStatus.Get());
	});

	// Queue it to be sent when ready
	MessagesFutures.Emplace(MoveTemp(MessageFuture));

	return true;
#else
	SendMessage(CreateTaskDeclinedMessage(InGetSyncStatusTask, "Platform not supported"), InGetSyncStatusTask.Recipient);
	return false;
#endif // PLATFORM_WINDOWS
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

void FSwitchboardListener::HandleRunningProcesses(TArray<FRunningProcess>& Processes, bool bNotifyThatProgramEnded)
{
	// Reads pipe and cleans up dead processes from the array.
	for (auto Iter = Processes.CreateIterator(); Iter; ++Iter)
	{
		FRunningProcess& Process = *Iter;

		if (Process.bPendingKill)
		{
			continue;
		}

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
				if (ReturnCode != 0)
				{
					UE_LOG(LogSwitchboard, Display, TEXT("Output:\n%s"), *ProcessOutput);
				}

				// Notify remote client, which implies that this is a program managed by it.
				if (bNotifyThatProgramEnded)
				{
					SendMessage(CreateProgramEndedMessage(Process.UUID.ToString(), ReturnCode, ProcessOutput), Process.Recipient);

					// Kill its monitor to avoid potential zombies (unless it is already pending kill)
					{
						FRunningProcess* FlipModeMonitor = FlipModeMonitors.FindByPredicate([&](const FRunningProcess& FlipMonitor)
						{
							return !FlipMonitor.bPendingKill && (FlipMonitor.UUID == Process.UUID);
						});

						if (FlipModeMonitor)
						{
							FSwitchboardKillTask Task(FGuid(), FlipModeMonitor->Recipient, FlipModeMonitor->UUID);
							KillProcess(Task);
						}
					}
				}

				FPlatformProcess::CloseProc(Process.Handle);
				FPlatformProcess::ClosePipe(Process.ReadPipe, Process.WritePipe);

				Iter.RemoveCurrent();
			}
		}
	}
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

void FSwitchboardListener::SendMessageFutures()
{
	for (auto Iter = MessagesFutures.CreateIterator(); Iter; ++Iter)
	{
		FSwitchboardMessageFuture& MessageFuture = *Iter;

		if (!MessageFuture.Future.IsReady())
		{
			continue;
		}

		FString Message = MessageFuture.Future.Get();
		SendMessage(Message, MessageFuture.InEndpoint);

		Iter.RemoveCurrent();
	}
}
