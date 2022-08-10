// Copyright Epic Games, Inc. All Rights Reserved.

#include "CookWorkerServer.h"

#include "Commandlets/AssetRegistryGenerator.h"
#include "CompactBinaryTCP.h"
#include "CookDirector.h"
#include "CookMPCollector.h"
#include "CookPackageData.h"
#include "CookPlatformManager.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Math/NumericLimits.h"
#include "Misc/AssertionMacros.h"
#include "PackageResultsMessage.h"
#include "PackageTracker.h"
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

	if (IsConnected() || ConnectStatus == EConnectStatus::WaitForDisconnect)
	{
		UE_LOG(LogCook, Error, TEXT("CookWorkerServer %d was destroyed before it finished Disconnect. The remote process may linger and may interfere with writes of future packages."),
			WorkerId.GetRemoteIndex());
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
	if (PendingPackages.Num())
	{
		if (IsConnected())
		{
			TArray<FName> PackageNames;
			PackageNames.Reserve(PendingPackages.Num());
			for (FPackageData* PackageData : PendingPackages)
			{
				PackageNames.Add(PackageData->GetPackageName());
			}
			SendMessage(FAbortPackagesMessage(MoveTemp(PackageNames)));
		}
		OutPendingPackages.Append(MoveTemp(PendingPackages));
		PendingPackages.Empty();
	}
	OutPendingPackages.Append(PackagesToAssign);
	PackagesToAssign.Empty();
}

void FCookWorkerServer::AbortAssignment(FPackageData& PackageData)
{
	if (PendingPackages.Remove(&PackageData))
	{
		if (IsConnected())
		{
			TArray<FName> PackageNames;
			PackageNames.Add(PackageData.GetPackageName());
			SendMessage(FAbortPackagesMessage(MoveTemp(PackageNames)));
		}
	}

	PackagesToAssign.Remove(&PackageData);
}

void FCookWorkerServer::AbortWorker(TSet<FPackageData*>& OutPendingPackages)
{
	AbortAssignments(OutPendingPackages);
	if (IsConnected())
	{
		SendMessage(FAbortWorkerMessage(FAbortWorkerMessage::EType::Abort));
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
	case EConnectStatus::PumpingCookComplete:
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

bool FCookWorkerServer::IsConnected() const
{
	return EConnectStatus::ConnectedFirst <= ConnectStatus && ConnectStatus <= EConnectStatus::ConnectedLast;
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

	SendToState(EConnectStatus::Connected);
	HandleReceiveMessages(MoveTemp(OtherPacketMessages));
	FInitialConfigMessage ConfigMessage;
	const TArray<const ITargetPlatform*>& SessionPlatforms = COTFS.PlatformManager->GetSessionPlatforms();
	OrderedSessionPlatforms.Reset(SessionPlatforms.Num());
	for (const ITargetPlatform* TargetPlatform : SessionPlatforms)
	{
		OrderedSessionPlatforms.Add(const_cast<ITargetPlatform*>(TargetPlatform));
	}
	ConfigMessage.ReadFromLocal(COTFS, OrderedSessionPlatforms,
		*COTFS.CookByTheBookOptions, *COTFS.CookOnTheFlyOptions);
	SendMessage(ConfigMessage);
	return true;
}

void FCookWorkerServer::TickFromSchedulerThread()
{
	if (IsConnected())
	{
		PumpReceiveMessages();
		if (IsConnected())
		{
			SendPendingPackages();
			PumpSendMessages();
		}
	}
	else
	{
		PumpConnect();
		if (IsConnected())
		{
			// Recursively call this function to call PumpReceive and PumpSend
			TickFromSchedulerThread();
		}
	}
}

void FCookWorkerServer::PumpCookComplete()
{
	if (ConnectStatus == EConnectStatus::Connected)
	{
		SendMessage(FAbortWorkerMessage(FAbortWorkerMessage::EType::CookComplete));
		SendToState(EConnectStatus::PumpingCookComplete);
	}
	else if (ConnectStatus == EConnectStatus::PumpingCookComplete)
	{
		TickFromSchedulerThread();
		if (IsConnected())
		{
			constexpr float WaitForPumpCompleteTimeout = 10.f * 60;
			if (FPlatformTime::Seconds()  - ConnectStartTimeSeconds > WaitForPumpCompleteTimeout && !IsCookIgnoreTimeouts())
			{
				UE_LOG(LogCook, Error, TEXT("CookWorker process of CookWorkerServer %d failed to finalize its cook within %.0f seconds; we will tell it to shutdown."),
					WorkerId.GetRemoteIndex(), WaitForPumpCompleteTimeout);
				SendMessage(FAbortWorkerMessage(FAbortWorkerMessage::EType::Abort));
				SendToState(EConnectStatus::WaitForDisconnect);
				return;
			}
		}
	}
}
void FCookWorkerServer::PumpConnect()
{
	for (;;)
	{
		if (IsConnected())
		{
			// Nothing further to do
			return;
		}
		switch (ConnectStatus)
		{
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
		case EConnectStatus::LostConnection:
			return; // Nothing further to do
		default:
			checkNoEntry();
			return;
		}
	}
}

void FCookWorkerServer::LaunchProcess()
{
	bool bShowCookWorkers = Director.GetShowWorkerOption() == FCookDirector::EShowWorker::SeparateWindows;

	FString CommandletExecutable = FUnrealEdMisc::Get().GetProjectEditorBinaryPath();
	FString CommandLine = Director.GetWorkerCommandLine(WorkerId);
	CookWorkerHandle = FPlatformProcess::CreateProc(*CommandletExecutable, *CommandLine,
		true /* bLaunchDetached */, !bShowCookWorkers /* bLaunchHidden */, !bShowCookWorkers /* bLaunchReallyHidden */,
		&CookWorkerProcessId, 0 /* PriorityModifier */, *FPaths::GetPath(CommandletExecutable),
		nullptr /* PipeWriteChild */);
	if (CookWorkerHandle.IsValid())
	{
		UE_LOG(LogCook, Display, TEXT("CookWorkerServer %d launched CookWorker as PID %u with commandline \"%s\"."),
			WorkerId.GetRemoteIndex(), CookWorkerProcessId, *CommandLine);
		SendToState(EConnectStatus::WaitForConnect);
	}
	else
	{
		// GetLastError information was logged by CreateProc
		UE_LOG(LogCook, Error, TEXT("CookWorkerServer %d failed to create CookWorker process. Assigned packages will be returned to the director."),
			WorkerId.GetRemoteIndex());
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
				WorkerId.GetRemoteIndex());
			SendToState(EConnectStatus::LostConnection);
			return;
		}
		ConnectTestStartTimeSeconds = FPlatformTime::Seconds();
	}

	if (CurrentTime - ConnectStartTimeSeconds > WaitForConnectTimeout && !IsCookIgnoreTimeouts())
	{
		UE_LOG(LogCook, Error, TEXT("CookWorkerServer %d process failed to connect within %.0f seconds. Assigned packages will be returned to the director."),
			WorkerId.GetRemoteIndex(), WaitForConnectTimeout);
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

	if (bTerminateImmediately || (CurrentTime - ConnectStartTimeSeconds > WaitForDisconnectTimeout && !IsCookIgnoreTimeouts()))
	{
		UE_CLOG(!bTerminateImmediately, LogCook, Warning,
			TEXT("CookWorker process of CookWorkerServer %d failed to disconnect within %.0f seconds; we will terminate it."),
			WorkerId.GetRemoteIndex(), WaitForDisconnectTimeout);
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
			WorkerId.GetRemoteIndex());
		SendToState(EConnectStatus::WaitForDisconnect);
		bTerminateImmediately = true;
	}
}

void FCookWorkerServer::SendPendingPackages()
{
	if (PackagesToAssign.IsEmpty())
	{
		return;
	}

	TArray<FConstructPackageData> ConstructDatas;
	ConstructDatas.Reserve(PackagesToAssign.Num());
	for (FPackageData* PackageData : PackagesToAssign)
	{
		ConstructDatas.Add(PackageData->CreateConstructData());
	}
	PendingPackages.Append(PackagesToAssign);
	PackagesToAssign.Empty();
	SendMessage(FAssignPackagesMessage(MoveTemp(ConstructDatas)));
}

void FCookWorkerServer::PumpReceiveMessages()
{
	using namespace UE::CompactBinaryTCP;
	TArray<FMarshalledMessage> Messages;
	EConnectionStatus SocketStatus = TryReadPacket(Socket, ReceiveBuffer, Messages);
	if (SocketStatus != EConnectionStatus::Okay && SocketStatus != EConnectionStatus::Incomplete)
	{
		UE_LOG(LogCook, Error, TEXT("CookWorkerServer %d failed to read from socket, we will shutdown the remote process. Assigned packages will be returned to the director."),
			WorkerId.GetRemoteIndex());
		SendToState(EConnectStatus::WaitForDisconnect);
		bTerminateImmediately = true;
		return;
	}
	HandleReceiveMessages(MoveTemp(Messages));
}

void FCookWorkerServer::HandleReceiveMessages(TArray<UE::CompactBinaryTCP::FMarshalledMessage>&& Messages)
{
	for (UE::CompactBinaryTCP::FMarshalledMessage& Message : Messages)
	{
		if (Message.MessageType == FAbortWorkerMessage::MessageType)
		{
			UE_CLOG(ConnectStatus != EConnectStatus::PumpingCookComplete && ConnectStatus != EConnectStatus::WaitForDisconnect,
				LogCook, Error, TEXT("CookWorkerServer %d remote process shut down unexpectedly. Assigned packages will be returned to the director."),
				WorkerId.GetRemoteIndex());
			SendToState(EConnectStatus::LostConnection);
			break;
		}
		else if (Message.MessageType == FPackageResultsMessage::MessageType)
		{
			FPackageResultsMessage ResultsMessage;
			if (!ResultsMessage.TryRead(MoveTemp(Message.Object)))
			{
				LogInvalidMessage(TEXT("FPackageResultsMessage"));
			}
			else
			{
				RecordResults(ResultsMessage);
			}
		}
		else if (Message.MessageType == FDiscoveredPackagesMessage::MessageType)
		{
			FDiscoveredPackagesMessage DiscoveredMessage;
			if (!DiscoveredMessage.TryRead(MoveTemp(Message.Object)))
			{
				LogInvalidMessage(TEXT("FDiscoveredPackagesMessage"));
			}
			else
			{
				for (FDiscoveredPackage& DiscoveredPackage : DiscoveredMessage.Packages)
				{
					AddDiscoveredPackage(MoveTemp(DiscoveredPackage));
				}
			}
		}
		else
		{
			TRefCountPtr<IMPCollector>* Collector = Director.MessageHandlers.Find(Message.MessageType);
			if (Collector)
			{
				check(*Collector);
				IMPCollector::FServerContext Context;
				Context.Platforms = OrderedSessionPlatforms;
				(*Collector)->ReceiveMessage(Context, Message.Object);
			}
			else
			{
				UE_LOG(LogCook, Error, TEXT("CookWorkerServer received message of unknown type %s from CookWorker. Ignoring it."),
					*Message.MessageType.ToString());
			}
		}
	}
}

void FCookWorkerServer::HandleReceivedPackagePlatformMessages(FPackageData& PackageData, const ITargetPlatform* TargetPlatform, TArray<UE::CompactBinaryTCP::FMarshalledMessage>&& Messages)
{
	for (UE::CompactBinaryTCP::FMarshalledMessage& Message : Messages)
	{
		if (Message.MessageType == FAssetRegistryPackageMessage::MessageType)
		{
			FAssetRegistryPackageMessage ARMessage;
			if (!ARMessage.TryRead(MoveTemp(Message.Object), PackageData, TargetPlatform))
			{
				LogInvalidMessage(TEXT("FAssetRegistryPackageMessage"));
			}
			else
			{
				FAssetRegistryGenerator* RegistryGenerator = COTFS.PlatformManager->GetPlatformData(TargetPlatform)->RegistryGenerator.Get();
				check(RegistryGenerator); // The TargetPlatform came from OrderedSessionPlatforms, and the RegistryGenerator should exist for any of those platforms
				RegistryGenerator->UpdateAssetRegistryPackageData(PackageData, MoveTemp(ARMessage));
			}
		}
	}
}

void FCookWorkerServer::SendMessage(const UE::CompactBinaryTCP::IMessage& Message)
{
	UE::CompactBinaryTCP::TryWritePacket(Socket, SendBuffer, Message);
}

void FCookWorkerServer::RecordResults(FPackageResultsMessage& Message)
{
	for (FPackageRemoteResult& Result : Message.Results)
	{
		FPackageData* PackageData = COTFS.PackageDatas->FindPackageDataByPackageName(Result.PackageName);
		if (!PackageData)
		{
			UE_LOG(LogCook, Warning, TEXT("CookWorkerServer %d received FPackageResultsMessage for invalid package %s. Ignoring it."),
				WorkerId.GetRemoteIndex() , *Result.PackageName.ToString());
			continue;
		}
		if (PendingPackages.Remove(PackageData) != 1)
		{
			UE_LOG(LogCook, Warning, TEXT("CookWorkerServer %d received FPackageResultsMessage for package %s which is not a pending package. Ignoring it."),
				WorkerId.GetRemoteIndex() , *Result.PackageName.ToString());
			continue;
		}
		PackageData->SetWorkerAssignment(FWorkerId::Invalid());

		// MPCOOKTODO: Refactor FSaveCookedPackageContext::FinishPlatform and ::FinishPackage so we can call them from here
		// to reduce duplication
		if (Result.SuppressCookReason == ESuppressCookReason::InvalidSuppressCookReason)
		{
			int32 NumPlatforms = OrderedSessionPlatforms.Num();
			if (Result.Platforms.Num() != NumPlatforms)
			{
				UE_LOG(LogCook, Warning, TEXT("CookWorkerServer %d received FPackageResultsMessage for package %s with an invalid number of platform results: expected %d, actual %d. Ignoring it."),
					WorkerId.GetRemoteIndex(), *Result.PackageName.ToString(), NumPlatforms, Result.Platforms.Num());
				continue;
			}
			for (int32 PlatformIndex = 0; PlatformIndex < NumPlatforms; ++PlatformIndex)
			{
				ITargetPlatform* TargetPlatform = OrderedSessionPlatforms[PlatformIndex];
				FPackageRemoteResult::FPlatformResult& PlatformResult = Result.Platforms[PlatformIndex];
				PackageData->SetPlatformCooked(TargetPlatform, PlatformResult.bSuccessful);
				// MPCOOKTODO: Call CommitRemotePackage on the PackageWriter
				HandleReceivedPackagePlatformMessages(*PackageData, TargetPlatform, MoveTemp(PlatformResult.Messages));
			}
			if (Result.bReferencedOnlyByEditorOnlyData)
			{
				COTFS.PackageTracker->UncookedEditorOnlyPackages.AddUnique(Result.PackageName);
			}
			COTFS.PromoteToSaveComplete(*PackageData, ESendFlags::QueueAddAndRemove);
		}
		else
		{
			COTFS.DemoteToIdle(*PackageData, ESendFlags::QueueAddAndRemove, Result.SuppressCookReason);
		}
	}
}

void FCookWorkerServer::LogInvalidMessage(const TCHAR* MessageTypeName)
{
	UE_LOG(LogCook, Error, TEXT("CookWorkerServer received invalidly formatted message for type %s from CookWorker. Ignoring it."),
		MessageTypeName);
}

void FCookWorkerServer::AddDiscoveredPackage(FDiscoveredPackage&& DiscoveredPackage)
{
	FPackageData& PackageData = COTFS.PackageDatas->FindOrAddPackageData(DiscoveredPackage.PackageName,
		DiscoveredPackage.NormalizedFileName);
	if (PackageData.IsInProgress() || PackageData.HasAnyCookedPlatform())
	{
		// The CookWorker thought this was a new package, but the Director already knows about it; ignore the report
		return;
	}

	if (DiscoveredPackage.Instigator.Category == EInstigator::GeneratedPackage)
	{
		PackageData.SetGenerated(true);
		PackageData.SetWorkerAssignmentConstraint(GetWorkerId());
	}
	COTFS.QueueDiscoveredPackageData(PackageData, MoveTemp(DiscoveredPackage.Instigator));
}

FAssignPackagesMessage::FAssignPackagesMessage(TArray<FConstructPackageData>&& InPackageDatas)
	: PackageDatas(MoveTemp(InPackageDatas))
{
}

void FAssignPackagesMessage::Write(FCbWriter& Writer) const
{
	Writer << "P" << PackageDatas;
}

bool FAssignPackagesMessage::TryRead(FCbObject&& Object)
{
	return LoadFromCompactBinary(Object["P"], PackageDatas);
}

FGuid FAssignPackagesMessage::MessageType(TEXT("B7B1542B73254B679319D73F753DB6F8"));

FAbortPackagesMessage::FAbortPackagesMessage(TArray<FName>&& InPackageNames)
	: PackageNames(MoveTemp(InPackageNames))
{
}

void FAbortPackagesMessage::Write(FCbWriter& Writer) const
{
	Writer << "PackageNames" <<  PackageNames;
}

bool FAbortPackagesMessage::TryRead(FCbObject&& Object)
{
	return LoadFromCompactBinary(Object["PackageNames"], PackageNames);
}

FGuid FAbortPackagesMessage::MessageType(TEXT("D769F1BFF2F34978868D70E3CAEE94E7"));

FAbortWorkerMessage::FAbortWorkerMessage(EType InType)
	: Type(InType)
{
}

void FAbortWorkerMessage::Write(FCbWriter& Writer) const
{
	Writer << "Type" << (uint8)Type;
}

bool FAbortWorkerMessage::TryRead(FCbObject&& Object)
{
	Type = static_cast<EType>(Object["Type"].AsUInt8((uint8)EType::Abort));
	return true;
}

FGuid FAbortWorkerMessage::MessageType(TEXT("83FD99DFE8DB4A9A8E71684C121BE6F3"));

void FInitialConfigMessage::ReadFromLocal(const UCookOnTheFlyServer& COTFS, const TArray<ITargetPlatform*>& InOrderedSessionPlatforms,
	const FCookByTheBookOptions& InCookByTheBookOptions, const FCookOnTheFlyOptions& InCookOnTheFlyOptions)
{
	InitialSettings.CopyFromLocal(COTFS);
	BeginCookSettings.CopyFromLocal(COTFS);
	OrderedSessionPlatforms = InOrderedSessionPlatforms;
	DirectorCookMode = COTFS.GetCookMode();
	CookInitializationFlags = COTFS.GetCookFlags();
	CookByTheBookOptions = InCookByTheBookOptions;
	CookOnTheFlyOptions = InCookOnTheFlyOptions;
	bZenStore = COTFS.IsUsingZenStore();
}

void FInitialConfigMessage::Write(FCbWriter& Writer) const
{
	int32 LocalCookMode = static_cast<int32>(DirectorCookMode);
	Writer << "DirectorCookMode" << LocalCookMode;
	int32 LocalCookFlags = static_cast<int32>(CookInitializationFlags);
	Writer << "CookInitializationFlags" << LocalCookFlags;
	Writer << "ZenStore" << bZenStore;

	Writer.BeginArray("TargetPlatforms");
	for (const ITargetPlatform* TargetPlatform : OrderedSessionPlatforms)
	{
		Writer << TargetPlatform->PlatformName();
	}
	Writer.EndArray();
	Writer << "InitialSettings" << InitialSettings;
	Writer << "BeginCookSettings" << BeginCookSettings;
	Writer << "CookByTheBookOptions" << CookByTheBookOptions;
	Writer << "CookOnTheFlyOptions" << CookOnTheFlyOptions;
}

bool FInitialConfigMessage::TryRead(FCbObject&& Object)
{
	bool bOk = true;
	int32 LocalCookMode;
	bOk = LoadFromCompactBinary(Object["DirectorCookMode"], LocalCookMode) & bOk;
	DirectorCookMode = static_cast<ECookMode::Type>(LocalCookMode);
	int32 LocalCookFlags;
	bOk = LoadFromCompactBinary(Object["CookInitializationFlags"], LocalCookFlags) & bOk;
	CookInitializationFlags = static_cast<ECookInitializationFlags>(LocalCookFlags);
	bOk = LoadFromCompactBinary(Object["ZenStore"], bZenStore) & bOk;

	ITargetPlatformManagerModule& TPM(GetTargetPlatformManagerRef());
	FCbFieldView TargetPlatformsField = Object["TargetPlatforms"];
	{
		bOk = TargetPlatformsField.IsArray() & bOk;
		OrderedSessionPlatforms.Reset(TargetPlatformsField.AsArrayView().Num());
		for (FCbFieldView ElementField : TargetPlatformsField)
		{
			TStringBuilder<128> KeyName;
			if (LoadFromCompactBinary(ElementField, KeyName))
			{
				ITargetPlatform* TargetPlatform = TPM.FindTargetPlatform(KeyName.ToView());
				if (TargetPlatform)
				{
					OrderedSessionPlatforms.Add(TargetPlatform);
				}
				else
				{
					UE_LOG(LogCook, Error, TEXT("Could not find TargetPlatform \"%.*s\" received from CookDirector."),
						KeyName.Len(), KeyName.GetData());
					bOk = false;
				}

			}
			else
			{
				bOk = false;
			}
		}
	}

	bOk = LoadFromCompactBinary(Object["InitialSettings"], InitialSettings) & bOk;
	bOk = LoadFromCompactBinary(Object["BeginCookSettings"], BeginCookSettings) & bOk;
	bOk = LoadFromCompactBinary(Object["CookByTheBookOptions"], CookByTheBookOptions) & bOk;
	bOk = LoadFromCompactBinary(Object["CookOnTheFlyOptions"], CookOnTheFlyOptions) & bOk;
	return bOk;
}

FGuid FInitialConfigMessage::MessageType(TEXT("340CDCB927304CEB9C0A66B5F707FC2B"));

FCbWriter& operator<<(FCbWriter& Writer, const FDiscoveredPackage& Package)
{
	Writer.BeginObject();
	Writer << "PackageName" << Package.PackageName;
	Writer << "NormalizedFileName" << Package.NormalizedFileName;
	Writer << "Instigator.Category" << static_cast<uint8>(Package.Instigator.Category);
	Writer << "Instigator.Referencer" << Package.Instigator.Referencer;
	Writer.EndObject();
	return Writer;
}

bool LoadFromCompactBinary(FCbFieldView Field, FDiscoveredPackage& OutPackage)
{
	bool bOk = LoadFromCompactBinary(Field["PackageName"], OutPackage.PackageName);
	bOk = LoadFromCompactBinary(Field["NormalizedFileName"], OutPackage.NormalizedFileName) & bOk;
	uint8 CategoryInt;
	if (LoadFromCompactBinary(Field["Instigator.Category"], CategoryInt) &&
		CategoryInt < static_cast<uint8>(EInstigator::Count))

	{
		OutPackage.Instigator.Category = static_cast<EInstigator>(CategoryInt);
	}
	else
	{
		bOk = false;
	}
	bOk = LoadFromCompactBinary(Field["Instigator.Referencer"], OutPackage.Instigator.Referencer) & bOk;
	if (!bOk)
	{
		OutPackage = FDiscoveredPackage();
	}
	return bOk;
}

void FDiscoveredPackagesMessage::Write(FCbWriter& Writer) const
{
	Writer << "Packages" << Packages;
}

bool FDiscoveredPackagesMessage::TryRead(FCbObject&& Object)
{
	return LoadFromCompactBinary(Object["Packages"], Packages);
}

FGuid FDiscoveredPackagesMessage::MessageType(TEXT("C9F5BC5C11484B06B346B411F1ED3090"));

}