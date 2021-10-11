// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "NetworkPredictionCVars.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace UE_NP {


NETSIM_DEVCVAR_SHIPCONST_INT(LogStateCorrections, 0, "np2.LogStateCorrections", "");
NETSIM_DEVCVAR_SHIPCONST_INT(EnableFutureInputs, 0, "np2.EnableFutureInputs", "");
NETSIM_DEVCVAR_SHIPCONST_INT(SkipObjStateReconcinle, 0, "np2.SkipObjStateReconcile", "");

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

		int32 RewindFrame = INDEX_NONE;
		TAsyncNetRecvData<AsyncModelDef> NetRecvData;

		while (DataStore->NetRecvQueue.Dequeue(NetRecvData))
		{	
			for (auto It = NetRecvData.NetRecvInstances.CreateIterator(); It; ++It)
			{
				const int32 idx = It.GetIndex();
				const typename TAsyncNetRecvData<AsyncModelDef>::FInstance& RecvData = *It;
				RecvData.Frame += LocalFrameOffset; // Convert server-frame to local frame

				if (RecvData.Frame >= EarliestFrame && RecvData.Frame <= LastCompletedStep)
				{
					const TAsncFrameSnapshot<AsyncModelDef>& Snapshot = DataStore->Frames[RecvData.Frame];

					// This is probably not enough, we will need to check some alive flag to make sure this state was active at 
					// this time or something?
					bool bShouldReconcile = false;
					if (Snapshot.InputCmds.IsValidIndex(idx) && Snapshot.NetStates.IsValidIndex(idx))
					{
						if (SkipObjStateReconcinle() == 0)
						{
							const InputCmdType& LocalInputCmd = Snapshot.InputCmds[idx];
							const NetStateType& LocalNetState = Snapshot.NetStates[idx];

							if (LocalInputCmd.ShouldReconcile(RecvData.InputCmd))
							{
								UE_CLOG(LogStateCorrections() > 0, LogNetworkPrediction, Log, TEXT("Reconcile on InputCmd. %s. Instance: %d. Frame: [%d/%d]"), AsyncModelDef::GetName(), idx, RecvData.Frame - LocalFrameOffset, RecvData.Frame);
								UE_CLOG(LogStateCorrections() > 0, LogNetworkPrediction, Log, TEXT("    Local: %s"), *NpModelUtil<AsyncModelDef>::ToString(&LocalInputCmd));
								UE_CLOG(LogStateCorrections() > 0, LogNetworkPrediction, Log, TEXT("   Server: %s"), *NpModelUtil<AsyncModelDef>::ToString(&RecvData.InputCmd));
								bShouldReconcile = true;
							}
							if (LocalNetState.ShouldReconcile(RecvData.NetState))
							{
								UE_CLOG(LogStateCorrections() > 0, LogNetworkPrediction, Log, TEXT("Reconcile on NetState. %s. Instance: %d. Frame: [%d/%d]"), AsyncModelDef::GetName(), idx, RecvData.Frame - LocalFrameOffset, RecvData.Frame);
								UE_CLOG(LogStateCorrections() > 0, LogNetworkPrediction, Log, TEXT("    Local: %s"), *NpModelUtil<AsyncModelDef>::ToString(&LocalNetState));
								UE_CLOG(LogStateCorrections() > 0, LogNetworkPrediction, Log, TEXT("   Server: %s"), *NpModelUtil<AsyncModelDef>::ToString(&RecvData.NetState));
								bShouldReconcile = true;
							}
						}
					}
					else
					{
						UE_CLOG(LogStateCorrections() > 0, LogNetworkPrediction, Log, TEXT("Reconcile due to invalid state. %s. Instance: %d. Frame: [%d/%d]"), AsyncModelDef::GetName(), idx, RecvData.Frame - LocalFrameOffset, RecvData.Frame);
						bShouldReconcile = true;
					}

					if (bShouldReconcile)
					{
						TAsyncCorrectionData<AsyncModelDef>& CorrectionData = DataStore->Corrections[RecvData.Frame % DataStore->Corrections.Num()];
						if (CorrectionData.Frame != RecvData.Frame)
						{
							CorrectionData.Frame = RecvData.Frame;
							CorrectionData.Instances.Reset();
						}

						typename TAsyncCorrectionData<AsyncModelDef>::FInstance& CorrectionInstance = CorrectionData.Instances.AddDefaulted_GetRef();
						CorrectionInstance.Index = idx;
						CorrectionInstance.InputCmd = RecvData.InputCmd;
						CorrectionInstance.NetState = RecvData.NetState;
					
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
		}
		
		return RewindFrame;
	}

private:
	TAsyncModelDataStore_Internal<AsyncModelDef>* DataStore = nullptr;
};


} // namespace UE_NP