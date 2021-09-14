// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "NetworkPredictionCVars.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace UE_NP {

	
NETSIM_DEVCVAR_SHIPCONST_INT(EnableFutureInputs, 0, "np2.EnableFutureInputs", "");

class IAsyncReconcileService
{
public:
	virtual ~IAsyncReconcileService() = default;
	virtual int32 Reconcile(const int32 LastCompletedStep, const int32 EarliestFrame, int32 LocalFrameOffset) = 0;
};

template<typename AsyncModelDef>
class TAsyncReconcileService : public IAsyncReconcileService
{
public:

	HOIST_ASYNCMODELDEF_TYPES()

	TAsyncReconcileService(TAsyncModelDataStore_Internal<AsyncModelDef>* InDataStore)
	{
		npCheck(InDataStore != nullptr);
		DataStore = InDataStore;
	}

	int32 Reconcile(const int32 LastCompletedStep, const int32 EarliestFrame, int32 LocalFrameOffset) final override
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(NPA_Reconcile);

		if (!DataStore->NetRecvQueue.Dequeue(DataStore->NetRecvData))
		{
			return INDEX_NONE;
		}
		npEnsure(DataStore->NetRecvQueue.IsEmpty()); // We should enqueue exactly one NetRecv snapshot per marshalled input, which should be exactly one call to Reconcile
		DataStore->NetRecvData.PendingCorrectionMask.Reset();

		int32 RewindFrame = INDEX_NONE;
		for (TConstSetBitIterator<> BitIt(DataStore->NetRecvData.NetRecvDirtyMask); BitIt; ++BitIt)
		{
			const int32 idx = BitIt.GetIndex();
			const typename TAsyncNetRecvData<AsyncModelDef>::FInstance& RecvData = DataStore->NetRecvData.NetRecvInstances[idx];
			RecvData.Frame += LocalFrameOffset; // Convert server-frame to local frame

			if (RecvData.Frame >= EarliestFrame && RecvData.Frame <= LastCompletedStep)
			{
				const TAsncFrameSnapshot<AsyncModelDef>& Snapshot = DataStore->Frames[RecvData.Frame];

				// This is probably not enough, we will need to check some alive flag to make sure this state was active at 
				// this time or something?
				bool bShouldReconcile = false;
				if (Snapshot.InputCmds.IsValidIndex(idx) && Snapshot.NetStates.IsValidIndex(idx))
				{
					const InputCmdType& LocalInputCmd = Snapshot.InputCmds[idx];
					const NetStateType& LocalNetState = Snapshot.NetStates[idx];

					if (LocalInputCmd.ShouldReconcile(RecvData.InputCmd))
					{
						//UE_LOG(LogNetworkPrediction, Log, TEXT("Reconcile on InputCmd. %s. Instance: %d. Frame: [%d/%d]"), AsyncModelDef::GetName(), idx, RecvData.Frame - LocalFrameOffset, RecvData.Frame);
						bShouldReconcile = true;
					}
					if (LocalNetState.ShouldReconcile(RecvData.NetState))
					{
						//UE_LOG(LogNetworkPrediction, Log, TEXT("Reconcile on NetState. %s. Instance: %d. Frame: [%d/%d]"), AsyncModelDef::GetName(), idx, RecvData.Frame - LocalFrameOffset, RecvData.Frame);
						bShouldReconcile = true;
					}
				}
				else
				{
					//UE_LOG(LogNetworkPrediction, Log, TEXT("Reconcile due to invalid state. %s. Instance: %d. Frame: [%d/%d]"), AsyncModelDef::GetName(), idx, RecvData.Frame - LocalFrameOffset, RecvData.Frame);
					bShouldReconcile = true;
				}

				if (bShouldReconcile)
				{
					NpResizeAndSetBit(DataStore->NetRecvData.PendingCorrectionMask, idx);
					RewindFrame = RewindFrame == INDEX_NONE ? RecvData.Frame : FMath::Min<int32>(RewindFrame, RecvData.Frame);
				}
			}

			// -------------------------------------------------------
			// Apply all Input corrections for future frames if non-locally controlled.
			//	doing this in a second pass maybe more cache coherent, but this is simpler for now.
			// -------------------------------------------------------
			
			if ((RecvData.Flags & (uint8)EInstanceFlags::LocallyControlled) == 0)
			{
				const InputCmdType& SourceInput = EnableFutureInputs() ? RecvData.LatestInputCmd : RecvData.InputCmd;

				const int32 NumFrames = (LastCompletedStep+1) - FMath::Max<int32>(0, RecvData.Frame+1);
				npEnsureMsgf(NumFrames < DataStore->Frames.Num(), TEXT("Too many input corrections. [%d %d] %d %d"), (LastCompletedStep+1), FMath::Max<int32>(0, RecvData.Frame+1), NumFrames, DataStore->Frames.Num() );

				for (int32 Frame= FMath::Max<int32>(0, RecvData.Frame+1); Frame <= LastCompletedStep+1; ++Frame)
				{
					TAsncFrameSnapshot<AsyncModelDef>& FutureSnapshot = DataStore->Frames[Frame];

					NpResizeForIndex(FutureSnapshot.InputCmds, idx);
					FutureSnapshot.InputCmds[idx] = SourceInput;
				}
			}			
		}

		DataStore->NetRecvData.NetRecvDirtyMask.Reset();
		return RewindFrame;
	}

private:
	TAsyncModelDataStore_Internal<AsyncModelDef>* DataStore = nullptr;
};


} // namespace UE_NP