// Copyright Epic Games, Inc. All Rights Reserved.

#include "NetworkPredictionProvider.h"

FName FNetworkPredictionProvider::ProviderName("NetworkPredictionProvider");

// -----------------------------------------------------------------------------

FNetworkPredictionProvider::FNetworkPredictionProvider(Trace::IAnalysisSession& InSession)
	: Session(InSession)
{

}

void FNetworkPredictionProvider::SetNetworkPredictionTraceVersion(uint32 Version)
{
	Session.WriteAccessCheck();
	NetworkPredictionTraceVersion = Version;
}

// -----------------------------------------------------------------------------

FSimulationData::FConst& FNetworkPredictionProvider::WriteSimulationCreated(uint32 SimulationId)
{
	FSimulationData::FConst& SimulationConstData = FindOrAdd(SimulationId)->ConstData;
	SimulationConstData.ID.PIESession = PIESessionCounter;
	return SimulationConstData;
}

void FNetworkPredictionProvider::WriteSimulationTick(uint32 SimulationId, FSimulationData::FTick&& InTick)
{
	FSimulationData& SimulationData = FindOrAdd(SimulationId).Get();

	SimulationData.Analysis.PendingUserStateSource = ENP_UserStateSource::SimTick;
	SimulationData.Analysis.PendingCommitUserStates.Reset();

	// ---------------------------------------------------------------------------------------------
	//	New tick may cause previous ticks to be trashed.
	//	FIXME: Dumb approach: loop through whole list. Can be cached off 
	//	note: don't do this after adding new tick to list since we'll end up trashing ourselves
	// ---------------------------------------------------------------------------------------------
	if (SimulationData.Ticks.Num() > 0)
	{
		for (auto It = SimulationData.Ticks.GetIterator(); It; ++It)
		{
			FSimulationData::FTick& Tick = const_cast<FSimulationData::FTick&>(*It);
			if (InTick.StartMS < Tick.EndMS && Tick.TrashedEngineFrame == 0)
			{
				Tick.TrashedEngineFrame = InTick.EngineFrame;
			}
		}
	}

	// Add it to the list
	FSimulationData::FTick& NewTick = SimulationData.Ticks.PushBack();
	NewTick = MoveTemp(InTick);

	// Repredict if we've already simulated past this time before
	if (NewTick.StartMS < SimulationData.Analysis.MaxTickSimTimeMS)
	{
		NewTick.bRepredict = true;
	}
	SimulationData.Analysis.MaxTickSimTimeMS = FMath::Max(SimulationData.Analysis.MaxTickSimTimeMS, NewTick.EndMS);

	// ---------------------------------------------------------------------------------------------
	//	Link this new tick with pending NetRecvs if possible
	// ---------------------------------------------------------------------------------------------

	TArray<FSimulationData::FNetSerializeRecv*> WorkingPendingNetSerializeRecv = MoveTemp(SimulationData.Analysis.PendingNetSerializeRecv);
	for (FSimulationData::FNetSerializeRecv* PendingRecv : WorkingPendingNetSerializeRecv)
	{
		check(PendingRecv);

		const FSimTime RecvSimTimeMS = PendingRecv->SimTimeMS;

		const bool bIsAuthority = (SimulationData.SparseData.Read(PendingRecv->EngineFrame)->NetRole == ENP_NetRole::Authority);

		// On non authority, this becomes a confirmed frame now if it wasn't already marked
		if (!bIsAuthority)
		{
			if (PendingRecv->Status == ENetSerializeRecvStatus::Unknown)
			{
				PendingRecv->Status = ENetSerializeRecvStatus::Confirm;
			}
		}

		// Perfect Match
		if (NewTick.StartMS == RecvSimTimeMS)
		{
			NewTick.StartNetRecv = PendingRecv;
			PendingRecv->NextTick = &NewTick;
		}
		// Ahead of last NetRecv
		else if (NewTick.StartMS > RecvSimTimeMS)
		{
			// Instead of attaching to the Orphaned list, find a tick to attach it to. This is a bit dicey and may not be
			// a good solution if we want to treat recvs that came in mid local predicted frame as confirmed (e.g, for sim proxies)
			auto FindNetRecvAHome = [&]()
			{
				for (auto It = SimulationData.Ticks.GetIteratorFromItem(SimulationData.Ticks.Num()-1); It; --It)
				{
					FSimulationData::FTick& PrevTick = const_cast<FSimulationData::FTick&>(*It);

					if (!PrevTick.StartNetRecv && PrevTick.StartMS == PendingRecv->SimTimeMS)
					{
						PrevTick.StartNetRecv = PendingRecv;
						PendingRecv->NextTick = &PrevTick;
						return true;
					}
				}
				return false;
			};

			if (!FindNetRecvAHome())
			{
				// No home for this recv so put him on the orphan list so he still gets drawn
				if (PendingRecv->Status == ENetSerializeRecvStatus::Unknown)
				{
					PendingRecv->Status = ENetSerializeRecvStatus::Stale;
				}
			}
		}
		else
		{
			SimulationData.Analysis.PendingNetSerializeRecv.Add(PendingRecv);
		}
	}
}

FSimulationData::FEngineFrame& FNetworkPredictionProvider::WriteSimulationEOF(uint32 SimulationId)
{
	FSimulationData& SimulationData = FindOrAdd(SimulationId).Get();
	FSimulationData::FEngineFrame& NewEOF = SimulationData.EOFState.PushBack();
	NewEOF.SystemFaults = MoveTemp(SimulationData.Analysis.PendingSystemFaults);
	return NewEOF;
}

void FNetworkPredictionProvider::WriteNetRecv(uint32 SimulationId, FSimulationData::FNetSerializeRecv&& InNetRecv)
{
	FSimulationData& SimulationData = FindOrAdd(SimulationId).Get();

	SimulationData.Analysis.PendingUserStateSource = ENP_UserStateSource::NetRecv;
	SimulationData.Analysis.PendingCommitUserStates.Reset();

	FSimulationData::FNetSerializeRecv &NewNetSerializeRecv = SimulationData.NetRecv.PushBack();
	NewNetSerializeRecv = MoveTemp(InNetRecv);

	if (SimulationData.SparseData.Read(NewNetSerializeRecv.EngineFrame)->NetRole == ENP_NetRole::Authority)
	{
		// Authority gets net receives before running ticks, so we accumulate all of them
		SimulationData.Analysis.PendingNetSerializeRecv.Add(&NewNetSerializeRecv);
	}
	else
	{
		if (SimulationData.Analysis.PendingNetSerializeRecv.Num() > 0)
		{
			ensure(SimulationData.Analysis.PendingNetSerializeRecv.Num() == 1);

			if (SimulationData.Analysis.PendingNetSerializeRecv.Last()->Status == ENetSerializeRecvStatus::Unknown)
			{
				SimulationData.Analysis.PendingNetSerializeRecv.Last()->Status = ENetSerializeRecvStatus::Stale;
			}
			SimulationData.Analysis.PendingNetSerializeRecv.Reset();
		}
		SimulationData.Analysis.PendingNetSerializeRecv.Add(&NewNetSerializeRecv);

		if (NewNetSerializeRecv.SimTimeMS > SimulationData.Analysis.MaxTickSimTimeMS)
		{
			NewNetSerializeRecv.Status = ENetSerializeRecvStatus::Jump;
		}
	}

	// Mark ConfirmedEngineFrame on pending frame
	// We want Tick.ConfirmedEngineFrame to be the earliest engine frame we received that was timestamped > what the tick was
	// NetRecvs will always increase in sim time, but our ticks do not
	// So we need to search the remainder of the list, knowing that tick time will be not be linear
	if (SimulationData.Ticks.Num() > SimulationData.Analysis.NetRecvItemIdx)
	{
		bool bIncrementSavedIdx = true;
		for (auto It = SimulationData.Ticks.GetIteratorFromItem(SimulationData.Analysis.NetRecvItemIdx); It; ++It)
		{
			FSimulationData::FTick& Tick = const_cast<FSimulationData::FTick&>(*It);
			if (Tick.EndMS > NewNetSerializeRecv.SimTimeMS)
			{
				// Once we encounter a frame that is still unconfirmed, we can no longer increment our saved position,
				// We need to pick up back here when we get a newer NetRecv.
				bIncrementSavedIdx = false;
			}
			else if (Tick.ConfirmedEngineFrame == 0)
			{
				// This is the first NetRecv that this frame was confirmed on
				Tick.ConfirmedEngineFrame = NewNetSerializeRecv.EngineFrame;
			}

			if (bIncrementSavedIdx)
			{
				SimulationData.Analysis.NetRecvItemIdx++;
			}
		}
	}
}

void FNetworkPredictionProvider::WriteNetCommit(uint32 SimulationId)
{
	FSimulationData& SimulationData = FindOrAdd(SimulationId).Get();

	// Mark pending user states committed
	for (FSimulationData::FUserState* UserState : SimulationData.Analysis.PendingCommitUserStates)
	{
		if (ensure(UserState->Source == ENP_UserStateSource::NetRecv))
		{
			UserState->Source = ENP_UserStateSource::NetRecvCommit;
		}
	}
	SimulationData.Analysis.PendingCommitUserStates.Reset();

	// Pending NetReceives also become committed here
	for (FSimulationData::FNetSerializeRecv* PendingRecv : SimulationData.Analysis.PendingNetSerializeRecv)
	{
		check(PendingRecv);

		const FSimTime RecvSimTimeMS = PendingRecv->SimTimeMS;
		const bool bIsAuthority = (SimulationData.SparseData.Read(PendingRecv->EngineFrame)->NetRole == ENP_NetRole::Authority);

		if (PendingRecv->Status == ENetSerializeRecvStatus::Unknown)
		{
			if (bIsAuthority)
			{
				// Authority this is "confirmed" (misnomer): we wait for net recvs to advance the sim (unless synth)
				PendingRecv->Status = ENetSerializeRecvStatus::Confirm;
			}
			else
			{
				// Non authority, this caused a rollback/correction since we committed to our buffers
				PendingRecv->Status = ENetSerializeRecvStatus::Rollback;
			}
		}
	}
}

void FNetworkPredictionProvider::WriteSystemFault(uint32 SimulationId, uint64 EngineFrameNumber, const TCHAR* Fmt)
{
	FSimulationData& SimulationData = FindOrAdd(SimulationId).Get();
	
	// This is akwward to trace. Should refactor some of this so it can be easily known if this happened within a
	// NetRecv or SimTick scope.
	
	for (FSimulationData::FNetSerializeRecv* PendingRecv : SimulationData.Analysis.PendingNetSerializeRecv)
	{
		PendingRecv->Status = ENetSerializeRecvStatus::Fault;
		PendingRecv->SystemFaults.Add({Fmt});
	}

	SimulationData.Analysis.PendingSystemFaults.Add({Fmt});
}

void FNetworkPredictionProvider::WriteOOBStateMod(uint32 SimulationId)
{
	// Signals that the next user states traced will be OOB mods
	FSimulationData& SimulationData = FindOrAdd(SimulationId).Get();
	SimulationData.Analysis.PendingCommitUserStates.Reset();
	SimulationData.Analysis.PendingUserStateSource = ENP_UserStateSource::OOB;
}

void FNetworkPredictionProvider::WriteOOBStateModStrSync(uint32 SimulationId, const TCHAR* Fmt)
{
	FSimulationData& SimulationData = FindOrAdd(SimulationId).Get();
	SimulationData.Analysis.PendingOOBSyncStr = Fmt;
}
void FNetworkPredictionProvider::WriteOOBStateModStrAux(uint32 SimulationId, const TCHAR* Fmt)
{
	FSimulationData& SimulationData = FindOrAdd(SimulationId).Get();
	SimulationData.Analysis.PendingOOBAuxStr = Fmt;
}

void FNetworkPredictionProvider::WriteProduceInput(uint32 SimulationId)
{
	FSimulationData& SimulationData = FindOrAdd(SimulationId).Get();
	SimulationData.Analysis.PendingCommitUserStates.Reset();
	SimulationData.Analysis.PendingUserStateSource = ENP_UserStateSource::ProduceInput;
}

void FNetworkPredictionProvider::WriteSynthInput(uint32 SimulationId)
{
	FSimulationData& SimulationData = FindOrAdd(SimulationId).Get();
	SimulationData.Analysis.PendingCommitUserStates.Reset();
	SimulationData.Analysis.PendingUserStateSource = ENP_UserStateSource::SynthInput;
}

void FNetworkPredictionProvider::WriteSimulationNetRole(uint32 SimulationId, uint64 EngineFrame, ENP_NetRole Role)
{
	FindOrAdd(SimulationId)->SparseData.Write(EngineFrame)->NetRole = Role;
}

void FNetworkPredictionProvider::WriteSimulationNetGUID(uint32 SimulationId, uint32 NetGUID)
{
	FindOrAdd(SimulationId)->ConstData.ID.NetGUID = NetGUID;
}

void FNetworkPredictionProvider::WriteUserState(uint32 SimulationId, int32 Frame, uint64 EngineFrame, ENP_UserState Type, const TCHAR* UserStr)
{
	FSimulationData& SimulationData = FindOrAdd(SimulationId).Get();
	ensure(SimulationData.Analysis.PendingUserStateSource != ENP_UserStateSource::Unknown);

	FSimulationData::FUserState& NewUserState = SimulationData.UserData.Store[(int32)Type].Push(Frame, EngineFrame);
	NewUserState.UserStr = UserStr;
	NewUserState.Source = SimulationData.Analysis.PendingUserStateSource;

	if (SimulationData.Analysis.PendingUserStateSource == ENP_UserStateSource::NetRecv)
	{
		SimulationData.Analysis.PendingCommitUserStates.Add(&NewUserState);
	}
	else if (SimulationData.Analysis.PendingUserStateSource == ENP_UserStateSource::OOB)
	{
		if (Type == ENP_UserState::Sync)
		{
			NewUserState.OOBStr = SimulationData.Analysis.PendingOOBSyncStr;
			SimulationData.Analysis.PendingOOBSyncStr = nullptr;
		}
		else if (Type == ENP_UserState::Aux)
		{
			NewUserState.OOBStr = SimulationData.Analysis.PendingOOBAuxStr;
			SimulationData.Analysis.PendingOOBAuxStr = nullptr;
		}
	}
}

void FNetworkPredictionProvider::WritePIEStart()
{
	PIESessionCounter++;
}

TSharedRef<FSimulationData>& FNetworkPredictionProvider::FindOrAdd(uint32 SimulationId)
{
	for (TSharedRef<FSimulationData>& Data : ProviderData)
	{
		if (Data->SimulationId == SimulationId)
		{
			return Data;
		}
	}

	ProviderData.Add(MakeShareable(new FSimulationData(SimulationId, Session.GetLinearAllocator())));
	return ProviderData.Last();
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------

const INetworkPredictionProvider* ReadNetworkPredictionProvider(const Trace::IAnalysisSession& Session)
{
	Session.ReadAccessCheck();
	return Session.ReadProvider<INetworkPredictionProvider>(FNetworkPredictionProvider::ProviderName);
}

const TCHAR* LexToString(ENP_NetRole Role)
{
	switch (Role)
	{
	case ENP_NetRole::None:
		return TEXT("None");
	case ENP_NetRole::SimulatedProxy:
		return TEXT("Simulated");
	case ENP_NetRole::AutonomousProxy:
		return TEXT("Autonomous");
	case ENP_NetRole::Authority:
		return TEXT("Authority");	
	default:
		return TEXT("Unknown");
	}
}

const TCHAR* LexToString(ENP_UserState State)
{
	switch(State)
	{
	case ENP_UserState::Input:
		return TEXT("Input");
	case ENP_UserState::Sync:
		return TEXT("Sync");
	case ENP_UserState::Aux:
		return TEXT("Aux");
	default:
		return TEXT("Unknown");
	}
}
const TCHAR* LexToString(ENP_UserStateSource Source)
{
	switch(Source)
	{
	case ENP_UserStateSource::ProduceInput:
		return TEXT("ProduceInput");
	case ENP_UserStateSource::SynthInput:
		return TEXT("SynthInput");
	case ENP_UserStateSource::SimTick:
		return TEXT("SimTick");
	case ENP_UserStateSource::NetRecv:
		return TEXT("NetRecv (Uncommited)");
	case ENP_UserStateSource::NetRecvCommit:
		return TEXT("NetRecv Committed");
	case ENP_UserStateSource::OOB:
		return TEXT("OOB");
	default:
		return TEXT("Unknown");
	}
}

const TCHAR* LexToString(ENetSerializeRecvStatus Status)
{
	switch(Status)
	{
	case ENetSerializeRecvStatus::Unknown:
		return TEXT("Unknown");
	case ENetSerializeRecvStatus::Confirm:
		return TEXT("Confirm");
	case ENetSerializeRecvStatus::Rollback:
		return TEXT("Rollback");
	case ENetSerializeRecvStatus::Jump:
		return TEXT("Jump");
	case ENetSerializeRecvStatus::Fault:
		return TEXT("Fault");
	case ENetSerializeRecvStatus::Stale:
		return TEXT("Stale");

	default:
		ensure(false);
		return TEXT("???");
	}
}