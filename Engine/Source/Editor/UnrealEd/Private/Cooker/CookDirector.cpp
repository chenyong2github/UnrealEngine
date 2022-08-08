// Copyright Epic Games, Inc. All Rights Reserved.

#include "CookDirector.h"

#include "CompactBinaryTCP.h"
#include "CookMPCollector.h"
#include "CookPackageData.h"
#include "CookWorkerServer.h"
#include "CookOnTheSide/CookOnTheFlyServer.h"
#include "CoreGlobals.h"
#include "Math/NumericLimits.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinaryWriter.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "String/ParseTokens.h"

namespace UE::Cook
{

FCookDirector::FCookDirector(UCookOnTheFlyServer& InCOTFS)
	: COTFS(InCOTFS)
{
	WorkersStalledStartTimeSeconds = MAX_flt;
	WorkersStalledWarnTimeSeconds = MAX_flt;

	ParseDesiredNumRemoteWorkers();
	WorkerConnectPort = Sockets::COOKDIRECTOR_DEFAULT_REQUEST_CONNECTION_PORT;
	FParse::Value(FCommandLine::Get(), TEXT("-CookDirectorListenPort="), WorkerConnectPort);
	ParseShowWorkerOption();

	if (DesiredNumRemoteWorkers > 0)
	{
		ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get();
		if (!SocketSubsystem)
		{
			UE_LOG(LogCook, Error, TEXT("CookDirector initialization failure: platform does not support network sockets. CookWorkers will be disabled."));
			DesiredNumRemoteWorkers = 0;
		}
	}

	if (DesiredNumRemoteWorkers > 0)
	{
		TryCreateWorkerConnectSocket();
	}
	UE_LOG(LogCook, Display, TEXT("MultiprocessCook is enabled with %d CookWorker processes."), DesiredNumRemoteWorkers);
}

void FCookDirector::ParseDesiredNumRemoteWorkers()
{
	DesiredNumRemoteWorkers = 4;
	GConfig->GetInt(TEXT("CookSettings"), TEXT("CookWorkerCount"), DesiredNumRemoteWorkers, GEditorIni);
	FParse::Value(FCommandLine::Get(), TEXT("-CookWorkerCount="), DesiredNumRemoteWorkers);
}

void FCookDirector::ParseShowWorkerOption()
{
	FString Text;
	const TCHAR* CommandLine = FCommandLine::Get();
	if (!FParse::Value(CommandLine, TEXT("-ShowCookWorker="), Text))
	{
		if (FParse::Param(CommandLine, TEXT("ShowCookWorker")))
		{
			Text = TEXT("SeparateWindows");
		}
	}

	if (Text == TEXT("CombinedLogs")) { ShowWorkerOption = EShowWorker::CombinedLogs; }
	else if (Text == TEXT("SeparateLogs")) { ShowWorkerOption = EShowWorker::SeparateLogs; }
	else if (Text == TEXT("SeparateWindows")) { ShowWorkerOption = EShowWorker::SeparateWindows; }
	else
	{
		if (!Text.IsEmpty())
		{
			UE_LOG(LogCook, Warning, TEXT("Invalid selection \"%s\" for -ShowCookWorker."), *Text);
		}
		ShowWorkerOption = EShowWorker::CombinedLogs;
	}
}

FCookDirector::~FCookDirector()
{
	TSet<FPackageData*> AbortedAssignments;
	for (TPair<int32, TUniquePtr<FCookWorkerServer>>& Pair : RemoteWorkers)
	{
		Pair.Value->AbortWorker(AbortedAssignments);
	}
	for (FPackageData* PackageData : AbortedAssignments)
	{
		check(PackageData->IsInProgress()); // Packages that were assigned to workers should be in the AssignedToWorker state
		PackageData->SetWorkerAssignment(FWorkerId::Invalid());
		PackageData->SendToState(UE::Cook::EPackageState::Request, ESendFlags::QueueAddAndRemove);
	}
	RemoteWorkers.Empty();
	PendingConnections.Empty();
	Sockets::CloseSocket(WorkerConnectSocket);
}

void FCookDirector::AssignRequests(TArrayView<UE::Cook::FPackageData*> Requests, TArray<FWorkerId>& OutAssignments)
{
	InitializeWorkers();

	int32 NumRemoteWorkers = RemoteWorkers.Num();
	if (NumRemoteWorkers == 0)
	{
		OutAssignments.SetNum(Requests.Num());
		for (FWorkerId& Assignment : OutAssignments)
		{
			Assignment = FWorkerId::Local();
		}
		return;
	}

	// Convert the Map of RemoteWorkers into an array sorted by WorkerIndex
	TArray<FCookWorkerServer*> SortedWorkers;
	SortedWorkers.Reserve(NumRemoteWorkers);
	int32 MaxRemoteIndex = -1;
	for (TPair<int32, TUniquePtr<FCookWorkerServer>>& Pair : RemoteWorkers)
	{
		SortedWorkers.Add(Pair.Value.Get());
		MaxRemoteIndex = FMath::Max(Pair.Key, MaxRemoteIndex);
	}
	check(MaxRemoteIndex >= 0);
	SortedWorkers.Sort([](const FCookWorkerServer& A, const FCookWorkerServer& B) { return A.GetWorkerId() < B.GetWorkerId(); });

	// Call a LoadBalancing algorithm to split the requests among the LocalWorker and RemoteWorkers
	// MPCOOKTODO: Implement LoadBalanceGreedy
	LoadBalanceStriped(SortedWorkers, Requests, OutAssignments);

	// Split the output array of WorkerId assignments into a batch for each of the RemoteWorkers 
	TArray<TArray<FPackageData*>> RemoteBatches;
	RemoteBatches.SetNum(MaxRemoteIndex+1);
	for (int32 RequestIndex = 0; RequestIndex < Requests.Num(); ++RequestIndex)
	{
		FWorkerId WorkerId = OutAssignments[RequestIndex];
		if (!WorkerId.IsLocal())
		{
			uint8 RemoteIndex = WorkerId.GetRemoteIndex();
			check(RemoteIndex < RemoteBatches.Num());
			TArray<FPackageData*>& RemoteBatch = RemoteBatches[RemoteIndex];
			if (RemoteBatch.Num() == 0)
			{
				RemoteBatch.Reserve(2 * Requests.Num() / (NumRemoteWorkers + 1));
			}
			RemoteBatch.Add(Requests[RequestIndex]);
		}
	}

	// MPCOOKTODO: Sort each batch from leaf to root

	// Assign each batch to the FCookWorkerServer in RemoteWorkers;
	// the CookWorkerServer's tick will handle sending the message to the remote process
	for (TPair<int32, TUniquePtr<FCookWorkerServer>>& Pair : RemoteWorkers)
	{
		Pair.Value->AppendAssignments(RemoteBatches[Pair.Key]);
	}
	TickWorkerConnects();
}

void FCookDirector::RemoveFromWorker(FPackageData& PackageData)
{
	for (TPair<int32, TUniquePtr<FCookWorkerServer>>& Pair : RemoteWorkers)
	{
		Pair.Value->AbortAssignment(PackageData);
	}
}


void FCookDirector::TickFromSchedulerThread()
{
	TickWorkerConnects();
	for (TPair<int32, TUniquePtr<FCookWorkerServer>>& Pair : RemoteWorkers)
	{
		FCookWorkerServer& RemoteWorker = *Pair.Value;
		RemoteWorker.TickFromSchedulerThread();
		if (RemoteWorker.IsShuttingDown())
		{
			TUniquePtr<FCookWorkerServer>& Existing = ShuttingDownWorkers.FindOrAdd(&RemoteWorker);
			check(!Existing); // We should not be able to send the same pointer into ShuttingDown twice
		}
	}

	TickWorkerShutdowns();

	bool bIsStalled = COTFS.IsMultiprocessLocalWorkerIdle() && !COTFS.PackageDatas->GetAssignedToWorkerSet().IsEmpty();
	SetWorkersStalled(bIsStalled);
}

void FCookDirector::PumpCookComplete(bool& bCompleted)
{
	TickWorkerConnects();
	for (TPair<int32, TUniquePtr<FCookWorkerServer>>& Pair : RemoteWorkers)
	{
		FCookWorkerServer& RemoteWorker = *Pair.Value;
		RemoteWorker.PumpCookComplete();
		if (RemoteWorker.IsShuttingDown())
		{
			TUniquePtr<FCookWorkerServer>& Existing = ShuttingDownWorkers.FindOrAdd(&RemoteWorker);
			check(!Existing); // We should not be able to send the same pointer into ShuttingDown twice
		}
	}
	TickWorkerShutdowns();
	bCompleted = RemoteWorkers.Num() == 0;
	SetWorkersStalled(!bCompleted);
}

void FCookDirector::ShutdownCookSession()
{
	while (!RemoteWorkers.IsEmpty())
	{
		AbortWorker(TMap<int32, TUniquePtr<FCookWorkerServer>>::TIterator(RemoteWorkers).Value()->GetWorkerId());
	}
	for (;;)
	{
		TickWorkerShutdowns();
		if (ShuttingDownWorkers.IsEmpty())
		{
			break;
		}
		constexpr double SleepSeconds = 0.010;
		FPlatformProcess::Sleep(SleepSeconds);
	}
	PendingConnections.Reset();

	// Restore the FCookDirector to its original state so that it is ready for a new session
	ParseDesiredNumRemoteWorkers();
}

void FCookDirector::Register(IMPCollector* Collector)
{
	TRefCountPtr<IMPCollector>& Existing = MessageHandlers.FindOrAdd(Collector->GetMessageType());
	if (Existing)
	{
		UE_LOG(LogCook, Error, TEXT("Duplicate IMPCollectors registered. Guid: %s, Existing: %s, Registering: %s. Keeping the Existing."),
			*Collector->GetMessageType().ToString(), Existing->GetDebugName(), Collector->GetDebugName());
		return;
	}
	Existing = Collector;
}

void FCookDirector::Unregister(IMPCollector* Collector)
{
	TRefCountPtr<IMPCollector> Existing;
	MessageHandlers.RemoveAndCopyValue(Collector->GetMessageType(), Existing);
	if (Existing && Existing.GetReference() != Collector)
	{
		UE_LOG(LogCook, Error, TEXT("Duplicate IMPCollector during Unregister. Guid: %s, Existing: %s, Unregistering: %s. Ignoring the Unregister."),
			*Collector->GetMessageType().ToString(), Existing->GetDebugName(), Collector->GetDebugName());
		MessageHandlers.Add(Collector->GetMessageType(), MoveTemp(Existing));
	}
}

void FCookDirector::SetWorkersStalled(bool bInWorkersStalled)
{
	if (bInWorkersStalled != bWorkersStalled)
	{
		bWorkersStalled = bInWorkersStalled;
		if (bWorkersStalled)
		{
			const double CurrentTime = FPlatformTime::Seconds();
			WorkersStalledStartTimeSeconds = CurrentTime;
			WorkersStalledWarnTimeSeconds = CurrentTime + GCookProgressWarnBusyTime;
		}
		else
		{
			WorkersStalledStartTimeSeconds = MAX_flt;
			WorkersStalledWarnTimeSeconds = MAX_flt;
		}
	}
	else if (bWorkersStalled)
	{
		const double CurrentTime = FPlatformTime::Seconds();
		if (CurrentTime >= WorkersStalledWarnTimeSeconds)
		{
			UE_LOG(LogCook, Warning, TEXT("Cooker has been blocked with no results from remote CookWorkers for %.0f seconds."),
				(float)(CurrentTime - WorkersStalledStartTimeSeconds));
			WorkersStalledWarnTimeSeconds = CurrentTime + GCookProgressWarnBusyTime;
		}
	}
}

FCookDirector::FPendingConnection::FPendingConnection(FPendingConnection&& Other)
{
	Swap(Socket, Other.Socket);
	Buffer = MoveTemp(Other.Buffer);
}

FCookDirector::FPendingConnection::~FPendingConnection()
{
	Sockets::CloseSocket(Socket);
}

FSocket* FCookDirector::FPendingConnection::DetachSocket()
{
	FSocket* Result = Socket;
	Socket = nullptr;
	return Result;
}

void FWorkerConnectMessage::Write(FCbWriter& Writer) const
{
	Writer << "RemoteIndex" << RemoteIndex;
}

bool FWorkerConnectMessage::TryRead(FCbObject&& Object)
{
	RemoteIndex = Object["RemoteIndex"].AsInt32(-1);
	return RemoteIndex >= 0;
}

FGuid FWorkerConnectMessage::MessageType(TEXT("302096E887DA48F7B079FAFAD0EE5695"));

bool FCookDirector::TryCreateWorkerConnectSocket()
{
	FString ErrorReason;
	TSharedPtr<FInternetAddr> ListenAddr;
	WorkerConnectSocket = Sockets::CreateListenSocket(WorkerConnectPort, ListenAddr, WorkerConnectAuthority,
		TEXT("FCookDirector-WorkerConnect"), ErrorReason);
	if (!WorkerConnectSocket)
	{
		UE_LOG(LogCook, Error, TEXT("CookDirector could not create listen socket, CookWorkers will be disabled. Reason: %s."),
			*ErrorReason);
		DesiredNumRemoteWorkers = 0;
		return false;
	}
	return true;
}


void FCookDirector::InitializeWorkers()
{
	if (RemoteWorkers.Num() >= DesiredNumRemoteWorkers)
	{
		return;
	}

	// Find any unused RemoteIndex less than the maximum used RemoteIndex
	TArray<int32> UnusedRemoteIndexes;
	RemoteWorkers.KeySort(TLess<>());
	int32 NextPossiblyOpenIndex = 0;
	for (TPair<int32, TUniquePtr<FCookWorkerServer>>& Pair : RemoteWorkers)
	{
		check(NextPossiblyOpenIndex <= Pair.Key);
		while (NextPossiblyOpenIndex != Pair.Key)
		{
			UnusedRemoteIndexes.Add(NextPossiblyOpenIndex++);
		}
	}
	// Add RemoteWorkers, pulling the RemoteIndex id from the UnusedRemoteIndexes if any exist
	// otherwise use the next integer because all indexes up to RemoteWorkers.Num() are in use.
	while (RemoteWorkers.Num() < DesiredNumRemoteWorkers)
	{
		int32 RemoteIndex;
		if (UnusedRemoteIndexes.Num())
		{
			RemoteIndex = UnusedRemoteIndexes[0];
			UnusedRemoteIndexes.RemoveAtSwap(0);
		}
		else
		{
			RemoteIndex = RemoteWorkers.Num();
		}
		RemoteWorkers.Add(RemoteIndex, MakeUnique<FCookWorkerServer>(*this, FWorkerId::FromRemoteIndex(RemoteIndex)));
	}
}

void FCookDirector::TickWorkerConnects()
{
	using namespace UE::CompactBinaryTCP;

	if (!WorkerConnectSocket)
	{
		return;
	}

	bool bReadReady;
	while (WorkerConnectSocket->HasPendingConnection(bReadReady) && bReadReady)
	{
		FSocket* WorkerSocket = WorkerConnectSocket->Accept(TEXT("Client Connection"));
		if (!WorkerSocket)
		{
			UE_LOG(LogCook, Warning, TEXT("Pending connection failed to create a ClientSocket."));
		}
		else
		{
			WorkerSocket->SetNonBlocking(true);
			PendingConnections.Add(FPendingConnection(WorkerSocket));
		}
	}

	for (TArray<FPendingConnection>::TIterator Iter(PendingConnections); Iter; ++Iter)
	{
		FPendingConnection& Conn = *Iter;
		TArray<FMarshalledMessage> Messages;
		EConnectionStatus Status;
		Status = TryReadPacket(Conn.Socket, Conn.Buffer, Messages);
		if (Status != EConnectionStatus::Okay)
		{
			UE_LOG(LogCook, Warning, TEXT("Pending connection failed before sending a WorkerPacket: %s"), DescribeStatus(Status));
			Iter.RemoveCurrent();
		}
		if (Messages.Num() == 0)
		{
			continue;
		}
		FPendingConnection LocalConn(MoveTemp(Conn));
		Iter.RemoveCurrent();

		if (Messages[0].MessageType != FWorkerConnectMessage::MessageType)
		{
			UE_LOG(LogCook, Warning, TEXT("Pending connection sent a different message before sending a connection message. MessageType: %s. Connection will be ignored."),
				*Messages[0].MessageType.ToString());
			continue;
		}
		FWorkerConnectMessage Message;
		if (!Message.TryRead(MoveTemp(Messages[0].Object)))
		{
			UE_LOG(LogCook, Warning, TEXT("Pending connection sent an invalid Connection Message. Connection will be ignored."));
			continue;
		}
		TUniquePtr<FCookWorkerServer>* RemoteWorkerPtr = RemoteWorkers.Find(Message.RemoteIndex);
		if (!RemoteWorkerPtr)
		{
			TStringBuilder<256> ValidIndexes;
			if (RemoteWorkers.Num())
			{
				RemoteWorkers.KeySort(TLess<>());
				for (TPair<int32, TUniquePtr<FCookWorkerServer>>& Pair : RemoteWorkers)
				{
					ValidIndexes.Appendf(TEXT("%d,"), Pair.Key);
				}
				ValidIndexes.RemoveSuffix(1); // Remove the terminating comma
			}
			UE_LOG(LogCook, Warning, TEXT("Pending connection sent a Connection Message with invalid RemoteIndex %d. ValidIndexes = {%s}. Connection will be ignored."),
				Message.RemoteIndex, *ValidIndexes);
			continue;
		}
		FCookWorkerServer& RemoteWorker = **RemoteWorkerPtr;
		FSocket* LocalSocket = LocalConn.DetachSocket();
		Messages.RemoveAt(0);
		if (!RemoteWorker.TryHandleConnectMessage(Message, LocalSocket, MoveTemp(Messages)))
		{
			UE_LOG(LogCook, Warning, TEXT("Pending connection sent a Connection Message with an already in-use RemoteIndex. Connection will be ignored."));
			Sockets::CloseSocket(LocalSocket);
			continue;
		}
	}
}

void FCookDirector::TickWorkerShutdowns()
{
	if (ShuttingDownWorkers.IsEmpty())
	{
		return;
	}

	// Move any newly shutting down workers from RemoteWorkers
	TArray<FCookWorkerServer*> NewShutdowns;
	for (TPair<FCookWorkerServer*, TUniquePtr<FCookWorkerServer>>& Pair : ShuttingDownWorkers)
	{
		if (!Pair.Value)
		{
			NewShutdowns.Add(Pair.Key);
		}
	}
	for (FCookWorkerServer* NewShutdown : NewShutdowns)
	{
		AbortWorker(NewShutdown->GetWorkerId());
	}
	for (TMap<FCookWorkerServer*, TUniquePtr<FCookWorkerServer>>::TIterator Iter(ShuttingDownWorkers); Iter; ++Iter)
	{
		FCookWorkerServer& Worker = *Iter.Key();
		check(Iter.Value()); // All non-yet-moved workers should have been moved by the AbortWorker calls above
		Worker.TickFromSchedulerThread();
		if (Worker.IsShutdownComplete())
		{
			// Worker is now deleted, do not access
			Iter.RemoveCurrent();
		}
	}
}

FString FCookDirector::GetWorkerCommandLine(FWorkerId WorkerId)
{
	FString CommandLine = FCommandLine::Get();

	const TCHAR* ProjectName = FApp::GetProjectName();
	checkf(ProjectName && ProjectName[0], TEXT("Expected UnrealEditor to be running with a non-empty project name"));
	TArray<FString> Tokens;
	UE::String::ParseTokensMultiple(CommandLine, { ' ', '\t', '\r', '\n' }, [&Tokens](FStringView Token)
		{
			if (Token.StartsWith(TEXT("-run=")) ||
				Token == TEXT("-CookOnTheFly") ||
				Token == TEXT("-CookWorker") ||
				Token == TEXT("-CookMultiProcess") ||
				Token == TEXT("-CookSingleProcess") ||
				Token.StartsWith(TEXT("-TargetPlatform")) ||
				Token.StartsWith(TEXT("-CookCultures")) ||
				Token.StartsWith(TEXT("-CookDirectorCount=")) ||
				Token.StartsWith(TEXT("-CookDirectorHost=")) ||
				Token.StartsWith(TEXT("-CookWorkerId=")) ||
				Token.StartsWith(TEXT("-ShowCookWorker"))
				)
			{
				return;
			}
			Tokens.Add(FString(Token));
		}, UE::String::EParseTokensOptions::SkipEmpty);
	if (Tokens[0] != ProjectName)
	{
		Tokens.Insert(ProjectName, 0);
	}
	Tokens.Insert(TEXT("-run=cook"), 1);
	Tokens.Insert(TEXT("-cookworker"), 2);
	check(!WorkerConnectAuthority.IsEmpty()); // This should have been constructed in TryCreateWorkerConnectSocket before any CookWorkerServers could exist to call GetWorkerCommandLine
	Tokens.Add(FString::Printf(TEXT("-CookDirectorHost=%s"), *WorkerConnectAuthority));
	Tokens.Add(FString::Printf(TEXT("-CookWorkerId=%d"), WorkerId.GetRemoteIndex()));

	return FString::Join(Tokens, TEXT(" "));
}

bool FDirectorConnectionInfo::TryParseCommandLine()
{
	if (!FParse::Value(FCommandLine::Get(), TEXT("-CookDirectorHost="), HostURI))
	{
		UE_LOG(LogCook, Error, TEXT("CookWorker startup failed: no CookDirector specified on commandline."));
		return false;
	}
	if (!FParse::Value(FCommandLine::Get(), TEXT("-CookWorkerId="), RemoteIndex))
	{
		UE_LOG(LogCook, Error, TEXT("CookWorker startup failed: no CookWorkerId specified on commandline."));
		return false;
	}
	return true;
}

void FCookDirector::LoadBalanceStriped(TArrayView<FCookWorkerServer*> SortedWorkers, TArrayView<FPackageData*> Requests, TArray<FWorkerId>& OutAssignments)
{
	TArray<FWorkerId> AllWorkers;
	int32 NumAllWorkers = SortedWorkers.Num() + 1;
	AllWorkers.Reserve(NumAllWorkers);
	AllWorkers.Add(FWorkerId::Local());
	for (FCookWorkerServer* Worker : SortedWorkers)
	{
		AllWorkers.Add(Worker->GetWorkerId());
	}
	OutAssignments.Reset(Requests.Num());
	int32 AllWorkersIndex = 0;
	for (FPackageData* Request : Requests)
	{
		OutAssignments.Add(AllWorkers[AllWorkersIndex]);
		AllWorkersIndex = (AllWorkersIndex + 1) % NumAllWorkers;
	}
}

void FCookDirector::AbortWorker(FWorkerId WorkerId)
{
	check(!WorkerId.IsLocal());
	int32 Index = WorkerId.GetRemoteIndex();
	TUniquePtr<FCookWorkerServer> RemoteWorker;
	RemoteWorkers.RemoveAndCopyValue(Index, RemoteWorker);
	if (!RemoteWorker)
	{
		return;
	}
	--DesiredNumRemoteWorkers;
	TSet<FPackageData*> PackagesToReassign;
	RemoteWorker->AbortWorker(PackagesToReassign);
	for (FPackageData* PackageData : PackagesToReassign)
	{
		check(PackageData->IsInProgress()); // Packages that were assigned to a worker should be in the AssignedToWorker state
		PackageData->SetWorkerAssignment(FWorkerId::Invalid());
		PackageData->SendToState(UE::Cook::EPackageState::Request, ESendFlags::QueueAddAndRemove);
	}
	TUniquePtr<FCookWorkerServer>& Existing = ShuttingDownWorkers.FindOrAdd(RemoteWorker.Get());
	check(!Existing); // We should not be able to abort a worker twice because we removed it from RemoteWorkers above
	Existing = MoveTemp(RemoteWorker);
}

}
