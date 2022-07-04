// Copyright Epic Games, Inc. All Rights Reserved.

#include "CookWorkerServer.h"

#include "CookDirector.h"
#include "CookPackageData.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AssertionMacros.h"
#include "Serialization/CompactBinaryWriter.h"
#include "UnrealEdMisc.h"

namespace UE::Cook
{

FCookWorkerServer::FCookWorkerServer(FCookDirector& InDirector, FWorkerId InWorkerId)
	: Director(InDirector)
	, COTFS(InDirector.COTFS)
	, WorkerId(InWorkerId)
{
}

FCookWorkerServer::~FCookWorkerServer()
{
	PendingPackages.Append(PackagesToAssign);
	PackagesToAssign.Empty();
	for (FPackageData* PackageData : PendingPackages)
	{
		check(PackageData->IsInProgress()); // Packages that were assigned to a worker should be in the AssignedToWorker state
		PackageData->SetWorkerAssignment(FWorkerId::Invalid());
		PackageData->SendToState(UE::Cook::EPackageState::Request, ESendFlags::QueueAddAndRemove);
	}

	if (ConnectStatus == EConnectStatus::Connected || ConnectStatus == EConnectStatus::WaitForDisconnect)
	{
		UE_LOG(LogCook, Error, TEXT("CookWorkerServer %d was destroyed before it finished Disconnect. The remote process may linger and may interfere with writes of future packages."),
			WorkerId.GetLocalOrRemoteIndex());
	}
	DetachFromRemoteProcess();
}

void FCookWorkerServer::DetachFromRemoteProcess()
{
	Sockets::CloseSocket(Socket);
	CookWorkerHandle = FProcHandle();
	CookWorkerProcessId = 0;
	bTerminateImmediately = false;
	SendBuffer.Reset();
	ReceiveBuffer.Reset();
}

void FCookWorkerServer::ShutdownRemoteProcess()
{
	Sockets::CloseSocket(Socket);
	if (CookWorkerHandle.IsValid())
	{
		FPlatformProcess::TerminateProc(CookWorkerHandle, /* bKillTree */true);
	}
	DetachFromRemoteProcess();
}

void FCookWorkerServer::AppendAssignments(TArrayView<FPackageData*> Assignments)
{
	PackagesToAssign.Append(Assignments);
}

void FCookWorkerServer::AbortAssignments(TSet<FPackageData*>& OutPendingPackages)
{
	if (ConnectStatus == EConnectStatus::Connected)
	{
		if (PendingPackages.Num())
		{
			TArray<FName> PackageNames;
			PackageNames.Reserve(PendingPackages.Num());
			for (FPackageData* PackageData : PendingPackages)
			{
				PackageNames.Add(PackageData->GetPackageName());
			}
			SendMessage(FAbortPackagesMessage(MoveTemp(PackageNames)));
			OutPendingPackages.Append(MoveTemp(PendingPackages));
			PendingPackages.Reset();
		}
	}
	else
	{
		check(PendingPackages.IsEmpty());
	}
	OutPendingPackages.Append(PackagesToAssign);
	PackagesToAssign.Empty();
}

void FCookWorkerServer::AbortAssignment(FPackageData& PackageData)
{
	if (ConnectStatus == EConnectStatus::Connected)
	{
		if (PendingPackages.Remove(&PackageData))
		{
			TArray<FName> PackageNames;
			PackageNames.Add(PackageData.GetPackageName());
			SendMessage(FAbortPackagesMessage(MoveTemp(PackageNames)));
		}
	}
	else
	{
		check(PendingPackages.IsEmpty());
	}

	PackagesToAssign.Remove(&PackageData);
}

void FCookWorkerServer::AbortWorker(TSet<FPackageData*>& OutPendingPackages)
{
	AbortAssignments(OutPendingPackages);
	if (ConnectStatus == EConnectStatus::Connected)
	{
		SendMessage(FAbortWorkerMessage());
		SendToState(EConnectStatus::WaitForDisconnect);
	}
}

void FCookWorkerServer::SendToState(EConnectStatus TargetStatus)
{
	switch (TargetStatus)
	{
	case EConnectStatus::WaitForConnect:
		ConnectStartTimeSeconds = FPlatformTime::Seconds();
		ConnectTestStartTimeSeconds = ConnectStartTimeSeconds;
		break;
	case EConnectStatus::WaitForDisconnect:
		ConnectStartTimeSeconds = FPlatformTime::Seconds();
		ConnectTestStartTimeSeconds = ConnectStartTimeSeconds;
		break;
	case EConnectStatus::LostConnection:
		DetachFromRemoteProcess();
		break;
	default:
		break;
	}
	ConnectStatus = TargetStatus;
}

bool FCookWorkerServer::IsShuttingDown() const
{
	return ConnectStatus == EConnectStatus::WaitForDisconnect || ConnectStatus == EConnectStatus::LostConnection;
}

bool FCookWorkerServer::IsShutdownComplete() const
{
	return ConnectStatus == EConnectStatus::LostConnection;
}

bool FCookWorkerServer::TryHandleConnectMessage(FWorkerConnectMessage& Message, FSocket* InSocket, TArray<UE::CompactBinaryTCP::FMarshalledMessage>&& OtherPacketMessages)
{
	if (ConnectStatus != EConnectStatus::WaitForConnect)
	{
		return false;
	}
	check(!Socket);
	Socket = InSocket;

	UE_LOG(LogCook, Error, TEXT("CookWorkerServer failure: writing Settings information is not yet implemented."));

	SendToState(EConnectStatus::Connected);
	HandleReceiveMessages(MoveTemp(OtherPacketMessages));
	return true;
}

void FCookWorkerServer::TickFromSchedulerThread()
{
	PumpConnect();
	if (ConnectStatus == EConnectStatus::Connected)
	{
		PumpReceiveMessages();
		PumpSendMessages();
	}
}

void FCookWorkerServer::PumpConnect()
{
	for (;;)
	{
		switch (ConnectStatus)
		{
		case EConnectStatus::Connected:
			return; // Nothing further to do
		case EConnectStatus::LostConnection:
			return; // Nothing further to do
		case EConnectStatus::Uninitialized:
			LaunchProcess();
			break;
		case EConnectStatus::WaitForConnect:
			TickWaitForConnect();
			if (ConnectStatus == EConnectStatus::WaitForConnect)
			{
				return; // Try again later
			}
			break;
		case EConnectStatus::WaitForDisconnect:
			TickWaitForDisconnect();
			if (ConnectStatus == EConnectStatus::WaitForDisconnect)
			{
				return; // Try again later
			}
			break;
		default:
			checkNoEntry();
			return;
		}
	}
}

void FCookWorkerServer::LaunchProcess()
{
	FString CommandletExecutable = FUnrealEdMisc::Get().GetProjectEditorBinaryPath();
	FString CommandLine = Director.GetWorkerCommandLine(WorkerId);
	CookWorkerHandle = FPlatformProcess::CreateProc(*CommandletExecutable, *CommandLine,
		true /* bLaunchDetached */, true /* bLaunchHidden */, true /* bLaunchReallyHidden */,
		&CookWorkerProcessId, 0 /* PriorityModifier */, *FPaths::GetPath(CommandletExecutable),
		nullptr /* PipeWriteChild */);
	if (CookWorkerHandle.IsValid())
	{
		SendToState(EConnectStatus::WaitForConnect);
	}
	else
	{
		// GetLastError information was logged by CreateProc
		UE_LOG(LogCook, Error, TEXT("CookWorkerServer %d failed to create CookWorker process. Assigned packages will be returned to the director."),
			WorkerId.GetLocalOrRemoteIndex());
		SendToState(EConnectStatus::LostConnection);
	}
}

void FCookWorkerServer::TickWaitForConnect()
{
	constexpr float TestProcessExistencePeriod = 1.f;
	constexpr float WaitForConnectTimeout = 60.f * 10;

	check(!Socket); // When the Socket is assigned we leave the WaitForConnect state, and we set it to null before entering

	double CurrentTime = FPlatformTime::Seconds();
	if (CurrentTime - ConnectTestStartTimeSeconds > TestProcessExistencePeriod)
	{
		if (!FPlatformProcess::IsProcRunning(CookWorkerHandle))
		{
			UE_LOG(LogCook, Error, TEXT("CookWorkerServer %d process terminated before connecting. Assigned packages will be returned to the director."),
				WorkerId.GetLocalOrRemoteIndex());
			SendToState(EConnectStatus::LostConnection);
			return;
		}
		ConnectTestStartTimeSeconds = FPlatformTime::Seconds();
	}

	if (CurrentTime - ConnectStartTimeSeconds > WaitForConnectTimeout && !IsCookIgnoreTimeouts())
	{
		UE_LOG(LogCook, Error, TEXT("CookWorkerServer %d process failed to connect within %.0f seconds. Assigned packages will be returned to the director."),
			WorkerId.GetLocalOrRemoteIndex(), WaitForConnectTimeout);
		ShutdownRemoteProcess();
		SendToState(EConnectStatus::LostConnection);
		return;
	}
}

void FCookWorkerServer::TickWaitForDisconnect()
{
	constexpr float TestProcessExistencePeriod = 1.f;
	constexpr float WaitForDisconnectTimeout = 60.f * 10;

	double CurrentTime = FPlatformTime::Seconds();
	if (CurrentTime - ConnectTestStartTimeSeconds > TestProcessExistencePeriod)
	{
		if (!FPlatformProcess::IsProcRunning(CookWorkerHandle))
		{
			SendToState(EConnectStatus::LostConnection);
			return;
		}
		ConnectTestStartTimeSeconds = FPlatformTime::Seconds();
	}

	// We might have been blocked from sending the disconnect, so keep trying to flush the buffer
	UE::CompactBinaryTCP::TryFlushBuffer(Socket, SendBuffer);
	TArray<UE::CompactBinaryTCP::FMarshalledMessage> Messages;
	TryReadPacket(Socket, ReceiveBuffer, Messages);
	for (UE::CompactBinaryTCP::FMarshalledMessage& Message : Messages)
	{
		if (Message.MessageType == FAbortWorkerMessage::MessageType)
		{
			SendToState(EConnectStatus::LostConnection);
			return;
		}
	}

	if ((bTerminateImmediately || CurrentTime - ConnectStartTimeSeconds > WaitForDisconnectTimeout) && !IsCookIgnoreTimeouts())
	{
		UE_LOG(LogCook, Warning, TEXT("CookWorkerServer %d's remote process failed to disconnect within %.0f seconds; we will terminate process."),
			WorkerId.GetLocalOrRemoteIndex(), WaitForDisconnectTimeout);
		ShutdownRemoteProcess();
		SendToState(EConnectStatus::LostConnection);
	}
}

void FCookWorkerServer::PumpSendMessages()
{
	UE::CompactBinaryTCP::EConnectionStatus Status = UE::CompactBinaryTCP::TryFlushBuffer(Socket, SendBuffer);
	if (Status == UE::CompactBinaryTCP::Failed)
	{
		UE_LOG(LogCook, Error, TEXT("CookWorkerServer %d failed to write to socket, we will shutdown the remote process. Assigned packages will be returned to the director."),
			WorkerId.GetLocalOrRemoteIndex());
		SendToState(EConnectStatus::WaitForDisconnect);
		bTerminateImmediately = true;
	}
}

void FCookWorkerServer::PumpReceiveMessages()
{
	using namespace UE::CompactBinaryTCP;
	TArray<FMarshalledMessage> Messages;
	TryReadPacket(Socket, ReceiveBuffer, Messages);
	HandleReceiveMessages(MoveTemp(Messages));
}

void FCookWorkerServer::HandleReceiveMessages(TArray<UE::CompactBinaryTCP::FMarshalledMessage>&& Messages)
{
	for (UE::CompactBinaryTCP::FMarshalledMessage& Message : Messages)
	{
		if (Message.MessageType == FAbortWorkerMessage::MessageType)
		{
			UE_LOG(LogCook, Error, TEXT("CookWorkerServer %d remote process shut down unexpectedly. Assigned packages will be returned to the director."),
				WorkerId.GetLocalOrRemoteIndex());
			SendToState(EConnectStatus::LostConnection);
			break;
		}
	}
}

void FCookWorkerServer::SendMessage(const UE::CompactBinaryTCP::IMessage& Message)
{
	UE::CompactBinaryTCP::TryWritePacket(Socket, SendBuffer, Message);
}

FAssignPackagesMessage::FAssignPackagesMessage(TArray<FName>&& InPackageNames)
	: PackageNames(MoveTemp(InPackageNames))
{
}

void FAssignPackagesMessage::Write(FCbWriter& Writer) const
{
	WriteArrayOfNames(Writer, "PackageNames", PackageNames);
}

bool FAssignPackagesMessage::TryRead(FCbObjectView Object)
{
	return TryReadArrayOfNames(Object, "PackageNames", PackageNames);
}

FGuid FAssignPackagesMessage::MessageType(TEXT("B7B1542B73254B679319D73F753DB6F8"));

FAbortPackagesMessage::FAbortPackagesMessage(TArray<FName>&& InPackageNames)
	: PackageNames(MoveTemp(InPackageNames))
{
}

void FAbortPackagesMessage::Write(FCbWriter& Writer) const
{
	WriteArrayOfNames(Writer, "PackageNames", PackageNames);
}

bool FAbortPackagesMessage::TryRead(FCbObjectView Object)
{
	return TryReadArrayOfNames(Object, "PackageNames", PackageNames);
}

FGuid FAbortPackagesMessage::MessageType(TEXT("D769F1BFF2F34978868D70E3CAEE94E7"));

FGuid FAbortWorkerMessage::MessageType(TEXT("83FD99DFE8DB4A9A8E71684C121BE6F3"));

void WriteArrayOfNames(FCbWriter& Writer, const char* ArrayName, TConstArrayView<FName> Names)
{
	Writer.BeginArray(ArrayName);
	for (FName Name : Names)
	{
		Writer.AddString(WriteToString<FName::StringBufferSize>(Name).ToView());
	}
	Writer.EndArray();
}

bool TryReadArrayOfNames(FCbObjectView Object, const char* ArrayName, TArray<FName>& OutNames)
{
	FCbFieldView ArrayField = Object["ArrayName"];
	FCbArrayView ArrayView = ArrayField.AsArrayView();
	if (ArrayField.HasError())
	{
		return false;
	}
	OutNames.Reserve(OutNames.Num() + ArrayView.Num());
	for (FCbFieldView ElementView : ArrayView)
	{
		FUtf8StringView StringView = ElementView.AsString();
		if (ElementView.HasError())
		{
			return false;
		}
		OutNames.Add(FName(StringView));
	}
	return true;
}

}