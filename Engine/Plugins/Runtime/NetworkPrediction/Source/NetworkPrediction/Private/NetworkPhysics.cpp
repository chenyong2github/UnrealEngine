// Copyright Epic Games, Inc. All Rights Reserved.

#include "NetworkPhysics.h"
#include "Engine/World.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "PBDRigidsSolver.h"
#include "ChaosSolversModule.h"
#include "RewindData.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "Chaos/SimCallbackObject.h"
#include "UObject/UObjectIterator.h"
#include "GameFramework/PlayerController.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/HUD.h"
#include "Engine/Canvas.h"
#include "Debug/ReporterGraph.h"
#include "Misc/ScopeExit.h"
#include "GameFramework/Actor.h"
#include "NetworkPredictionCheck.h"
#include "NetworkPredictionCVars.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "NetworkPredictionDebug.h"
#include "GameFramework/PlayerState.h"
#include "Misc/FileHelper.h"

DEFINE_LOG_CATEGORY(LogNetworkPhysics);

namespace UE_NETWORK_PHYSICS
{
	bool Enable=false;
	FAutoConsoleVariableRef CVarEnable(TEXT("np2.bEnable"), Enable, TEXT("Enabled rollback physics. Must be set before starting game"));

	//NP_DEVCVAR_INT(Enable, 0, "np2.bEnable", "Enabled rollback physics. Must be set before starting game");
	NP_DEVCVAR_INT(LogCorrections, 1, "np2.LogCorrections", "Logs corrections when they happen");		
	NP_DEVCVAR_INT(LogImpulses, -1, "np2.LogImpulses", "Logs all recorded F/T/LI/AI");

	NP_DEVCVAR_FLOAT(X, 1.0f, "np2.Tolerance.X", "Location Tolerance");
	NP_DEVCVAR_FLOAT(R, 0.00f, "np2.Tolerance.R", "Rotation Tolerance");
	NP_DEVCVAR_FLOAT(V, 1.0f, "np2.Tolerance.V", "Velocity Tolerance");
	NP_DEVCVAR_FLOAT(W, 1.0f, "np2.Tolerance.W", "Rotational Velocity Tolerance");

	NP_DEVCVAR_INT(Debug, 0, "np2.Debug", "Debug mode for in world drawing");
	NP_DEVCVAR_INT(DebugTolerance, 1, "np2.Debug.Tolerance", "If enabled, only draw large corrections in world.");
	NP_DEVCVAR_FLOAT(DebugDrawTolerance_X, 5.0, "np2.Debug.Tolerance.X",  "Location Debug Drawing Toleration");
	NP_DEVCVAR_FLOAT(DebugDrawTolerance_R, 2.0, "np2.Debug.Tolerance.R",  "Simple distance based LOD");
		
	NP_DEVCVAR_INT(ForceResim, 0, "np2.ForceResim", "Forces near constant resimming");		
	NP_DEVCVAR_INT(ForceResimWithoutCorrection, 0, "np2.ForceResimWithoutCorrection", "Forces near constant resimming WITHOUT applying server data");
	
	NP_DEVCVAR_INT(FixedLocalFrameOffset, -1, "np2.FixedLocalFrameOffset", "When > 0, use hardcoded frame offset on client from head");	
	NP_DEVCVAR_INT(FixedLocalFrameOffsetTolerance, 3, "np2.FixedLocalFrameOffsetTolerance", "Tolerance when using np2.FixedLocalFrameOffset");

	// NOTE: These have temporarily been switched to regular CVar style variables, because the NP_DECVAR_* macros
	// can only make ECVF_Cheat variables, which cannot be hotfixed with DefaultEngine.ini. Instead they use
	// ConsoleVariables.ini, which isn't loaded in shipping.
	int32 EnableLOD=0;
	FAutoConsoleVariableRef CVarEnableLOD(TEXT("np2.bEnableLOD"), EnableLOD, TEXT("Enable local LOD mode"));
	int32 MinLODForNonLocal=0;
	FAutoConsoleVariableRef CVarMinLODForNonLocal(TEXT("np2.MinLODForNonLocal"), MinLODForNonLocal, TEXT("Force LOD to this value for actors who are not locally controlled (0 to skip)"));
	float LODDistance=2400.f;
	FAutoConsoleVariableRef CVarLODDistance(TEXT("np2.LODDistance"), LODDistance, TEXT("Simple distance based LOD"));

	NP_DEVCVAR_INT(ResimForSleep, 0, "np2.ResimForSleep", "Triggers resim if only sleep state differs. Otherwise we only match server sleep state if XRVW differ.");
		
	// Time dilation CVars
	NP_DEVCVAR_INT(TimeDilationEnabled, 1, "np2.TimeDilationEnabled", "Enable clientside TimeDilation");
	NP_DEVCVAR_FLOAT(MaxTargetNumBufferedCmds, 5.0, "np2.MaxTargetNumBufferedCmds", "");
	NP_DEVCVAR_FLOAT(MaxTimeDilationMag, 0.01f, "np2.MaxTimeDilationMag", "Maximum time dilation that client will use to slow down / catch up with server");
	NP_DEVCVAR_FLOAT(TimeDilationAlpha, 0.1f, "np2.TimeDilationAlpha", "");
	NP_DEVCVAR_FLOAT(TargetNumBufferedCmds, 1.9f, "np2.TargetNumBufferedCmds", "");
	NP_DEVCVAR_FLOAT(TargetNumBufferedCmdsAlpha, 0.005f, "np2.TargetNumBufferedCmdsAlpha", "");
	NP_DEVCVAR_INT(LerpTargetNumBufferedCmdsAggresively, 0, "np2.LerpTargetNumBufferedCmdsAggresively", "Aggresively lerp towards TargetNumBufferedCmds");

	// ----------------------------------------------
	// Debugger helpers:
	//	See current frame anywhere with: 
	//		{,,UE4Editor-NetworkPrediction.dll}UE_NETWORK_PHYSICS::GServerFrame
	//		{,,UE4Editor-NetworkPrediction.dll}UE_NETWORK_PHYSICS::GClientFrame
	//		note: all bets are off here with multiple clients, this is just a debugging convenience.
	//		Use UE_NETWORK_PHYSICS::DebugSimulationFrame() to get the correct value.
	//
	//
	//	Set frame you want to break at via: 
	//		{,,UE4Editor-NetworkPrediction.dll}UE_NETWORK_PHYSICS::GBreakAtFrame
	//		or via console with "np2.BreakAtFrame"
	//
	//	Do something like this in your code:
	//	if (UE_NETWORK_PHYSICS::ConditionalFrameBreakpoint()) { UE_LOG(...); }
	//		or
	//	UE_NETWORK_PHYSICS::ConditionalFrameEnsure();
	// ----------------------------------------------

	// Set this in debugger or via console to cause ConditionalFrameBreakpoint() to return true when current simulation frame is equal to this
	int32 GBreakAtFrame = 0;
	FAutoConsoleVariableRef CVarConditionalBreakAtFrame(TEXT("np2.BreakAtFrame"), GBreakAtFrame, TEXT(""));

	// These are strictly convenience variables to view in the debugger
	int32 GServerFrame = 0;
	int32 GClientFrame = 0;

	// returns true/false so you can log or do whatever at GBreakAtFrame
	bool ConditionalFrameBreakpoint()
	{
#if NETWORK_PHYSICS_THREAD_CONTEXT
		return (GBreakAtFrame != 0 && GBreakAtFrame == FNetworkPhysicsThreadContext::Get().SimulationFrame);
#else
		return false;
#endif
	}

	// forces ensure failure to invoke debugger
	void ConditionalFrameEnsure()
	{
#if NETWORK_PHYSICS_THREAD_CONTEXT
		ensureAlways(!ConditionalFrameBreakpoint());
#endif
	}

	int32 DebugSimulationFrame()
	{
#if NETWORK_PHYSICS_THREAD_CONTEXT
		return FNetworkPhysicsThreadContext::Get().SimulationFrame;
#else
		return 0;
#endif
	}
	
	bool DebugServer()
	{
#if NETWORK_PHYSICS_THREAD_CONTEXT
		return FNetworkPhysicsThreadContext::Get().bIsServer;
#else
		return false;
#endif
	}
		
	int8 QuantizeTimeDilation(float F)
	{
		if (F == 1.f)
		{
			return 0;
		}

		float Normalized = FMath::Clamp<float>((F - 1.f) / MaxTimeDilationMag, -1.f, 1.f);
		return (int8)(Normalized* 128.f);
	}

	
	float DeQuantizeTimeDilation(int8 i)
	{
		if (i == 0)
		{
			return 1.f;
		}

		float Normalized = (float)i / 128.f;
		float Uncompressed = 1.f + (Normalized * MaxTimeDilationMag);
		return Uncompressed;
	}
}

struct FNetworkPhysicsRewindCallback : public Chaos::IRewindCallback
{
	bool CompareVec(const FVector& A, const FVector& B, const float D, const TCHAR* Str)
	{
		const bool b = D == 0 ? A != B : FVector::DistSquared(A, B) > D * D;
		UE_CLOG(UE_NETWORK_PHYSICS::LogCorrections > 0 && b && Str, LogNetworkPhysics, Log, TEXT("%s correction. Server: %s. Local: %s. Delta: %s (%.4f)"), Str, *A.ToString(), *B.ToString(), *(A-B).ToString(), (A-B).Size());
		return  b;
	}
	bool CompareQuat(const FQuat& A, const FQuat& B, const float D, const TCHAR* Str)
	{
		const float Error = FQuat::ErrorAutoNormalize(A, B);
		const bool b = D == 0 ? A != B : Error > D;

		//UE_LOG(LogTemp, Warning, TEXT("Error: %f"), Error);

		UE_CLOG(UE_NETWORK_PHYSICS::LogCorrections > 0 && b && Str, LogNetworkPhysics, Log, TEXT("%s correction. Server: %s. Local: %s. Delta: %f"), Str, *A.ToString(), *B.ToString(), FQuat::ErrorAutoNormalize(A, B));
		return b;
	}
	bool CompareObjState(Chaos::EObjectStateType A, Chaos::EObjectStateType B, const TCHAR* Str)
	{
		const bool b = A != B;
		UE_CLOG(UE_NETWORK_PHYSICS::LogCorrections > 0 && b && Str, LogNetworkPhysics, Log, TEXT("%s correction. Server: %d. Local: %d."), Str, A, B);
		return b;
	}

	int32 TriggerRewindIfNeeded_Internal(int32 LastCompletedStep) override
	{
		if (bIsServer)
		{
			return INDEX_NONE;
		}

		if (ResimEndFrame != INDEX_NONE)
		{
			// We are already in a resim and shouldn't trigger a new one
			return INDEX_NONE;
		}

		PendingCorrections.Reset();

		if (UE_NETWORK_PHYSICS::ForceResimWithoutCorrection > 0)
		{
			//const int32 ForceRewindFrame = RewindData->GetEarliestFrame_Internal()+1; // This was causing us to back > 64 frames?
			const int32 ForceRewindFrame = LastCompletedStep - 9;
			ResimEndFrame = LastCompletedStep;
			//UE_LOG(LogTemp, Warning, TEXT("Forcing rewind to %d. LastCompletedStep: %d"), ForceRewindFrame, LastCompletedStep);
			return ForceRewindFrame;
		}

		FStats Stats;
		Stats.LatestSimFrame = RewindData->CurrentFrame();
		Stats.MinFrameChecked = TNumericLimits<int32>::Max();
		Stats.MaxFrameChecked = 0;

		ON_SCOPE_EXIT
		{
			if (UE_NETWORK_PHYSICS::Debug > 0)
			{
				StatsForGT.Enqueue(MoveTemp(Stats));
			}
		};
		
		checkSlow(RewindData);
		const int32 MinFrame = RewindData->CurrentFrame() - RewindData->GetFramesSaved();				

		FSnapshot Snapshot;
		int32 RewindToFrame = INDEX_NONE;
		while (DataFromNetwork.Dequeue(Snapshot))
		{
			// Detect change in local offset. This usually results in a bunch of corrections
			// that are the fault of networking, not physics/user code. Note this for debugging purposes.
			if (Snapshot.LocalFrameOffset != LastLocalOffset)
			{
				LastLocalOffset = Snapshot.LocalFrameOffset;
				Stats.LocalOffsetChanged = true;
			}

			for (FNetworkPhysicsState& Obj : Snapshot.Objects)
			{
				FSingleParticlePhysicsProxy* Proxy = Obj.Proxy;
				check(Proxy);

				if (Obj.Frame < MinFrame)
				{
					UE_LOG(LogNetworkPhysics, Log, TEXT("Obj too stale to reconcile. Frame: %d. LastCompletedStep: %d"), Obj.Frame, LastCompletedStep);
					continue;
				}

				if (Proxy->GetHandle_LowLevel() == nullptr)
				{
					UE_LOG(LogTemp, Warning, TEXT("No valid LowLEvel handle yet..."));
					continue;
				}

				const auto P = RewindData->GetPastStateAtFrame(*Proxy->GetHandle_LowLevel(), Obj.Frame);
				const int32 SimulationFrame = Obj.Frame - Snapshot.LocalFrameOffset;

				Stats.MinFrameChecked = FMath::Min(Stats.MaxFrameChecked, Obj.Frame);
				Stats.MaxFrameChecked = FMath::Max(Stats.MaxFrameChecked, Obj.Frame);
				Stats.NumChecked++;

				//UE_LOG(LogTemp, Warning, TEXT("Reconcile Obj 0x%X at frame %d"), (int64)Proxy, Obj.Frame);


#if NETWORK_PHYSICS_REPLICATE_EXTRAS
				if (UE_NETWORK_PHYSICS::LogImpulses > 0)
				{
					for (int32 i=(int32)Chaos::FFrameAndPhase::EParticleHistoryPhase::PrePushData; i < (int32)Chaos::FFrameAndPhase::EParticleHistoryPhase::NumPhases; ++i)
					{
						if (Obj.DebugState[i].LinearImpulse.SizeSquared() > 0.f)
						{
							UE_LOG(LogNetworkPhysics, Warning, TEXT("[C] server told us they applied Linear Impulse %s on Frame [%d] in Phase [%d]"), *Obj.DebugState[i].LinearImpulse.ToString(), SimulationFrame-1, i);
						}
					}
				}
#endif
				
				//UE_LOG(LogTemp, Warning, TEXT("0x%X P.ObjectState(): %d. R: %s [%s]"), (int64)Proxy,  P.ObjectState(), *P.R().ToString(), *Obj.Physics.Rotation.ToString());
				//UE_LOG(LogTemp, Warning, TEXT("			R: %s [%s]"), *FRotator(P.R()).ToString(), *FRotator(Obj.Physics.Rotation).ToString());

				
				if ((UE_NETWORK_PHYSICS::ResimForSleep && CompareObjState(Obj.Physics.ObjectState, P.ObjectState(), TEXT("ObjectState"))) ||
					CompareVec(Obj.Physics.Location, P.X(), UE_NETWORK_PHYSICS::X, TEXT("Location")) ||
					CompareVec(Obj.Physics.LinearVelocity, P.V(), UE_NETWORK_PHYSICS::V, TEXT("Velocity")) ||
					CompareVec(Obj.Physics.AngularVelocity, P.W(), UE_NETWORK_PHYSICS::W, TEXT("Angular Velocity")) ||
					CompareQuat(Obj.Physics.Rotation, P.R(), UE_NETWORK_PHYSICS::R, TEXT("Rotation")) || UE_NETWORK_PHYSICS::ForceResim > 0)
				{

					UE_CLOG(UE_NETWORK_PHYSICS::LogCorrections > 0, LogNetworkPhysics, Log, TEXT("Rewind Needed. SimFrame: %d. Obj.Frame: %d. LastCompletedStep: %d."), SimulationFrame, Obj.Frame, LastCompletedStep);

					RewindToFrame = RewindToFrame == INDEX_NONE ? Obj.Frame : FMath::Min(RewindToFrame, Obj.Frame);
					ensure(RewindToFrame >= 0);

					PendingCorrections.Emplace(Obj);
					PendingCorrectionIdx = 0;

					// Send this correction back to GT for debugging
					if (UE_NETWORK_PHYSICS::Debug > 0)
					{
						FBasePhysicsState Auth{P.ObjectState(), P.X(), P.R(), P.V(), P.W()};
						Stats.Corrections.Emplace(FStats::FCorrection{Proxy, Obj.Physics, Auth, Obj.Frame, Obj.Frame - Snapshot.LocalFrameOffset});
					}

#if NETWORK_PHYSICS_REPLICATE_EXTRAS
					if (UE_NETWORK_PHYSICS::LogCorrections > 0 && Obj.Frame-1 >= RewindData->GetEarliestFrame_Internal())
					{
						const int32 PreSimulationFrame = SimulationFrame-1;
						for (int32 i=(int32)Chaos::FFrameAndPhase::EParticleHistoryPhase::PrePushData; i < (int32)Chaos::FFrameAndPhase::EParticleHistoryPhase::NumPhases; ++i)
						{
							const auto LocalPreFrameData = RewindData->GetPastStateAtFrame(*Proxy->GetHandle_LowLevel(), Obj.Frame-1, (Chaos::FFrameAndPhase::EParticleHistoryPhase)i);
							if (Obj.DebugState[i].Force != LocalPreFrameData.F())
							{
								UE_LOG(LogNetworkPhysics, Warning, TEXT("Previous Frame [%d] Phase [%d] FORCE mismatch. Local: %s. Server: %s"), PreSimulationFrame, i, *LocalPreFrameData.F().ToString(), *Obj.DebugState[i].Force.ToString());
							}
							if (Obj.DebugState[i].Torque != LocalPreFrameData.Torque())
							{
								UE_LOG(LogNetworkPhysics, Warning, TEXT("Previous Frame [%d] Phase [%d] TORQUE mismatch. Local: %s. Server: %s"), PreSimulationFrame, i, *LocalPreFrameData.Torque().ToString(), *Obj.DebugState[i].Torque.ToString());
							}
							if (Obj.DebugState[i].LinearImpulse != LocalPreFrameData.LinearImpulse())
							{
								UE_LOG(LogNetworkPhysics, Warning, TEXT("Previous Frame [%d] Phase [%d] LINEAR IMPULSE mismatch. Local: %s. Server: %s"), PreSimulationFrame, i, *LocalPreFrameData.LinearImpulse().ToString(), *Obj.DebugState[i].LinearImpulse.ToString());
							}
							if (Obj.DebugState[i].AngularImpulse != LocalPreFrameData.AngularImpulse())
							{
								UE_LOG(LogNetworkPhysics, Warning, TEXT("Previous Frame [%d] Phase [%d] ANGULAR IMPULSE mismatch. Local: %s. Server: %s"), PreSimulationFrame, i, *LocalPreFrameData.AngularImpulse().ToString(), *Obj.DebugState[i].AngularImpulse.ToString());
							}
						}
					}
#endif
				}
			}
		}

		// Gameplay code extension
		for (Chaos::ISimCallbackObject* Callback : SimObjectCallbacks)
		{
			int32 CallbackFrame = Callback->TriggerRewindIfNeeded_Internal(LastCompletedStep);
			if (CallbackFrame != INDEX_NONE)
			{
				Stats.NumSubsystemCorrections++;
				RewindToFrame = RewindToFrame == INDEX_NONE ? CallbackFrame : FMath::Min(RewindToFrame, CallbackFrame);
			}
		}

		
		for (auto& SubSys : NetworkPhysicsManager->SubSystems)
		{
			const int32 CallbackFrame = SubSys.Value->TriggerRewindIfNeeded_Internal(LastCompletedStep);
			if (CallbackFrame != INDEX_NONE)
			{
				Stats.NumSubsystemCorrections++;
				RewindToFrame = RewindToFrame == INDEX_NONE ? CallbackFrame : FMath::Min(RewindToFrame, CallbackFrame);
			}
		}

		// Calling code will ensure if we return a bad frame
		if (!(RewindToFrame > INDEX_NONE && RewindToFrame < LastCompletedStep-1))
		{
			UE_CLOG(UE_NETWORK_PHYSICS::LogCorrections > 0 && RewindToFrame != INDEX_NONE, LogNetworkPhysics, Warning, TEXT("Trying to rewind to invalid frame %d (LastCompletedStep: %d)"), RewindToFrame, LastCompletedStep);
			return INDEX_NONE;
		}
		
		return RewindToFrame;
	}
	
	void ProcessInputs_Internal(int32 PhysicsStep, const TArray<Chaos::FSimCallbackInputAndObject>& SimCallbackInputs) override
	{
		// Apply Physics corrections if necessary
		if (PendingCorrectionIdx != INDEX_NONE)
		{		
			for (; PendingCorrectionIdx < PendingCorrections.Num(); ++PendingCorrectionIdx)
			{
				FNetworkPhysicsState& CorrectionState = PendingCorrections[PendingCorrectionIdx];
				if (CorrectionState.Frame > PhysicsStep)
				{
					break;
				}

				// TODO: This nullcheck is to avoid null access crash in Editor Tests.
				// Should re-evaluate whether/why this is occurring in the first place.
				if (CorrectionState.Proxy)
				{
				if (auto* PT = CorrectionState.Proxy->GetPhysicsThreadAPI())
				{
					UE_CLOG(UE_NETWORK_PHYSICS::LogCorrections > 0, LogNetworkPhysics, Log, TEXT("Applying Correction from frame %d (actual step: %d). Location: %s"), CorrectionState.Frame, PhysicsStep, *FVector(CorrectionState.Physics.Location).ToString());
				
					PT->SetX(CorrectionState.Physics.Location, false);
					PT->SetV(CorrectionState.Physics.LinearVelocity, false);
					PT->SetR(CorrectionState.Physics.Rotation, false);
					PT->SetW(CorrectionState.Physics.AngularVelocity, false);
					
						//Solver->GetParticles().MarkTransientDirtyParticle(PT->GetProxy()->GetHandle_LowLevel());

					//if (PT->ObjectState() != CorrectionState.Physics.ObjectState)
					{
						ensure(CorrectionState.Physics.ObjectState != Chaos::EObjectStateType::Uninitialized);
							UE_CLOG(UE_NETWORK_PHYSICS::LogCorrections > 0 && PT->ObjectState() != CorrectionState.Physics.ObjectState, LogNetworkPhysics, Log, TEXT("Applying Correction State %d"), CorrectionState.Physics.ObjectState);
						PT->SetObjectState(CorrectionState.Physics.ObjectState);
					}
				}
			}
		}
		}

		// Marhsall data back to GT based on what was requested for networking
		if (!RewindData->IsResim())
		{
			FRequest Request;
			while (DataRequested.Dequeue(Request));

			FSnapshot Snapshot;
			Snapshot.SimulationFrame = PhysicsStep - this->LastLocalOffset;
			Snapshot.LocalFrameOffset = this->LastLocalOffset;
			for (FSingleParticlePhysicsProxy* Proxy : Request.Proxies)
			{
				if (auto* PT = Proxy->GetPhysicsThreadAPI())
				{
					FNetworkPhysicsState& Obj = Snapshot.Objects.AddDefaulted_GetRef();
					Obj.Proxy = Proxy;
					Obj.Frame = PhysicsStep;

					Obj.Physics.ObjectState = PT->ObjectState();
					Obj.Physics.Location = PT->X();
					Obj.Physics.LinearVelocity = PT->V();
					Obj.Physics.Rotation = PT->R();
					Obj.Physics.AngularVelocity = PT->W();
				
#if NETWORK_PHYSICS_REPLICATE_EXTRAS
					// Record previous frames forces/torques for debugging
					int32 EarliestFrame = RewindData->GetEarliestFrame_Internal();
					const int32 PreSimFrame = Snapshot.SimulationFrame - 1;
					const int32 PreStorageFrame = PhysicsStep - 1;
					if (EarliestFrame < PreStorageFrame)
					{
						for (int32 i=(int32)Chaos::FFrameAndPhase::EParticleHistoryPhase::PrePushData; i < (int32)Chaos::FFrameAndPhase::EParticleHistoryPhase::NumPhases; ++i)
						{
							Chaos::FGeometryParticleState State = RewindData->GetPastStateAtFrame(*Proxy->GetHandle_LowLevel(), PreStorageFrame, (Chaos::FFrameAndPhase::EParticleHistoryPhase)i);
							Obj.DebugState[i].Force = State.F();
							Obj.DebugState[i].Torque = State.Torque();
							Obj.DebugState[i].LinearImpulse = State.LinearImpulse();
							Obj.DebugState[i].AngularImpulse = State.AngularImpulse();

							if (UE_NETWORK_PHYSICS::LogImpulses > 0)
							{
								if (Obj.DebugState[i].Force.SizeSquared() > 0.f)
								{
									UE_LOG(LogNetworkPhysics, Warning, TEXT("[%s] Applied Force: %s on frame %d. In Phase %d"),  this->bIsServer ? TEXT("S") : TEXT("C"), *Obj.DebugState[i].Force.ToString(), PreSimFrame, i);
								}

								if (Obj.DebugState[i].Torque.SizeSquared() > 0.f)
								{
									UE_LOG(LogNetworkPhysics, Warning, TEXT("[%s] Applied Force: %s on frame %d. In Phase %d"),  this->bIsServer ? TEXT("S") : TEXT("C"), *Obj.DebugState[i].Torque.ToString(), PreSimFrame, i);
								}

								if (Obj.DebugState[i].LinearImpulse.SizeSquared() > 0.f)
								{
									UE_LOG(LogNetworkPhysics, Warning, TEXT("[%s] Applied LinearImpulse: %s on frame %d. In Phase %d"),  this->bIsServer ? TEXT("S") : TEXT("C"), *Obj.DebugState[i].LinearImpulse.ToString(), PreSimFrame, i);
								}

								if (Obj.DebugState[i].AngularImpulse.SizeSquared() > 0.f)
								{
									UE_LOG(LogNetworkPhysics, Warning, TEXT("[%s] Applied AngularImpulse: %s on frame %d. In Phase %d"),  this->bIsServer ? TEXT("S") : TEXT("C"), *Obj.DebugState[i].AngularImpulse.ToString(), PreSimFrame, i);
								}
							}
						}
					}
#endif
				}
			}

			if (Snapshot.Objects.Num() > 0)
			{
				DataFromPhysics.Enqueue(MoveTemp(Snapshot));
			}
		}

#if NETWORK_PHYSICS_THREAD_CONTEXT
		FNetworkPhysicsThreadContext::Get().Update(PhysicsStep - this->LastLocalOffset, this->bIsServer);
#endif

		for (auto& SubSys : NetworkPhysicsManager->SubSystems)
		{
			SubSys.Value->ProcessInputs_Internal(PhysicsStep);
		}
		
		for (auto CallbacKInput : SimCallbackInputs)
		{
			for (Chaos::ISimCallbackObject* Callback : SimObjectCallbacks)
			{
				if (CallbacKInput.CallbackObject == Callback)
				{
					Callback->ApplyCorrections_Internal(PhysicsStep, CallbacKInput.Input);
					break;
				}
			}
		}
	}

	void PreResimStep_Internal(int32 PhysicsStep, bool bFirst) override
	{
		if (bFirst)
		{
			for (Chaos::ISimCallbackObject* Callback : SimObjectCallbacks)
			{
				Callback->FirstPreResimStep_Internal(PhysicsStep);
				break;
			}
		}

		for (auto& SubSys : NetworkPhysicsManager->SubSystems)
		{
			SubSys.Value->PreResimStep_Internal(PhysicsStep, bFirst);
		}
	}

	void PostResimStep_Internal(int32 PhysicsStep) override
	{
		if (ResimEndFrame == PhysicsStep)
		{
			ResimEndFrame = INDEX_NONE;
		}
	}

	void ProcessInputs_External(int32 PhysicsStep, const TArray<Chaos::FSimCallbackInputAndObject>& SimCallbackInputs) override 
	{
		npCheckSlow(NetworkPhysicsManager);
		NetworkPhysicsManager->ProcessInputs_External(PhysicsStep, SimCallbackInputs);
	}

	void RegisterRewindableSimCallback_Internal(Chaos::ISimCallbackObject* Callback) override
	{
		SimObjectCallbacks.Add(Callback);
	}

	struct FSnapshot
	{
		int32 SimulationFrame = 0; // Only set in DataFromPhysics
		int32 LocalFrameOffset = 0; // only needed for debugging (translating client to server frames). Could be removed or maybe done via Insights tracing
		TArray<FNetworkPhysicsState> Objects;
	};

	TQueue<FSnapshot> DataFromNetwork;	// Data used to check for corrections
	TQueue<FSnapshot> DataFromPhysics;	// Data sent back to GT for networking

	// GT -> PT. "These are the proxies I want you to populate DataFromPhysics with". Is there a better way to do it?
	struct FRequest
	{
		TArray<FSingleParticlePhysicsProxy*> Proxies;
	};

	TQueue<FRequest> DataRequested;

	// Objects that we need to correct in PreResimStep_Internal
	TArray<FNetworkPhysicsState> PendingCorrections;
	int32 PendingCorrectionIdx = INDEX_NONE;
	int32 ResimEndFrame = INDEX_NONE;

	Chaos::FRewindData* RewindData=nullptr; // This be made to be accessed off of IRewindCallback
	Chaos::FPhysicsSolver* Solver = nullptr; //todo: shouldn't have to cache this, should be part of base class
	UWorld* World = nullptr;

	int32 LastLocalOffset = 0;

	// ----------------------

	TArray<Chaos::ISimCallbackObject*> SimObjectCallbacks;
	UNetworkPhysicsManager* NetworkPhysicsManager = nullptr;

	bool bIsServer = false;

	// Debugging
	struct FStats
	{
		struct FCorrection
		{
			FSingleParticlePhysicsProxy* Proxy = nullptr;
			FBasePhysicsState LocalState;
			FBasePhysicsState AuthorityState;
			int32 LocalFrame = 0;
			int32 AuthorityFrame = 0;
		};

		TArray<FCorrection> Corrections;
		
		int32 LatestSimFrame = 0;
		int32 MinFrameChecked = 0;
		int32 MaxFrameChecked = 0;
		int32 NumChecked = 0;
		int32 NumSubsystemCorrections = 0;
		bool LocalOffsetChanged = false;
	};

	TQueue<FStats> StatsForGT; // Marshalled PT->GT
	TStaticArray<FStats, 128> RecordedStatsGT;
	int32 LastRecordedStat = 0;
};

// --------------------------------------------------------------------------------------------

UNetworkPhysicsManager::UNetworkPhysicsManager()
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	bRecordDebugSnapshots = true;
#endif
}

void UNetworkPhysicsManager::Initialize(FSubsystemCollectionBase& Collection)
{
	UWorld* World = GetWorld();
	check(World);

	if (World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Game)
	{
		if (UE_NETWORK_PHYSICS::Enable)
		{
			PostTickDispatchHandle = World->OnPostTickDispatch().AddUObject(this, &UNetworkPhysicsManager::PostNetRecv); // Should be safe. We want to marshal GT->PT as soon as possible
			TickFlushHandle = World->OnTickFlush().AddUObject(this, &UNetworkPhysicsManager::PreNetSend); // Who wins, Subsystems or net drivers? We need to go first. Probably a dumb game to play (but we want to marshal PT->GT at last possible second)

			FWorldDelegates::OnPostWorldInitialization.AddUObject(this, &UNetworkPhysicsManager::OnWorldPostInit);
		}
	}
}

void UNetworkPhysicsManager::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		if (PostTickDispatchHandle.IsValid())
		{
			World->OnPostTickDispatch().Remove(PostTickDispatchHandle);
		}
		if (TickFlushHandle.IsValid())
		{
			World->OnTickFlush().Remove(TickFlushHandle);
		}
	}
}

void UNetworkPhysicsManager::OnWorldPostInit(UWorld* World, const UWorld::InitializationValues)
{
#if WITH_CHAOS
	if (World != GetWorld())
	{
		return;
	}

	FPhysScene* PhysScene = World->GetPhysicsScene();
	if (ensureAlways(PhysScene))
	{
		Chaos::FPhysicsSolver* Solver = PhysScene->GetSolver();

		if (ensure(Solver->GetRewindCallback()==nullptr))
		{
			int32 NumFrames = 64;
			const IConsoleVariable* NumFramesCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("p.RewindCaptureNumFrames"));
			if (ensure(NumFramesCVar))
			{
				NumFrames = NumFramesCVar->GetInt();
			}

			Solver->EnableRewindCapture(NumFrames, true, MakeUnique<FNetworkPhysicsRewindCallback>());
			RewindCallback = static_cast<FNetworkPhysicsRewindCallback*>(Solver->GetRewindCallback());
			RewindCallback->RewindData = Solver->GetRewindData();
			RewindCallback->Solver = Solver;
			RewindCallback->World = World;
			RewindCallback->NetworkPhysicsManager = this;
		}
	}
#endif
}

void UNetworkPhysicsManager::PostNetRecv()
{
#if WITH_CHAOS
	if (RewindCallback == nullptr)
	{
		return;
	}

	UWorld* World = GetWorld();
	checkSlow(World);

	FPhysScene* PhysScene = World->GetPhysicsScene();
	checkSlow(PhysScene);

	const bool bIsServer = World->GetNetMode() != NM_Client;
	RewindCallback->bIsServer = bIsServer;

	// Iterate through all objects we are managing and coalease them into a snapshot that is marshalled to PT for rollback consideration
	//if (bIsServer)
	{
		// Server has to tell PT what objects he wants to hear about.
		// This feels kind of not right but I'm not sure how else to do it from the PT alone? Can I get a list of active proxies on the PT?
		// I want a list of proxies on the PT that have rollback networking enable (currently defined by if they implement theINetworkPhysics interface)
		// (also maybe this should be in tick something? It doesn't need to be in post net recv)
		FNetworkPhysicsRewindCallback::FRequest Request;

		for (FNetworkPhysicsState* PhysicsState : ManagedPhysicsStates)
		{
			check(PhysicsState);
			check(PhysicsState->Proxy);

			Request.Proxies.Add(PhysicsState->Proxy);
		}

		if (Request.Proxies.Num() > 0)
		{
			RewindCallback->DataRequested.Enqueue(MoveTemp(Request));
		}
	}

	if (!bIsServer)
	{
		// Client: Marshal received data from Network to PT so it can be reconciled
		FNetworkPhysicsRewindCallback::FSnapshot Snapshot;
		int32 MaxFrameSeen = INDEX_NONE;
		
		// Calculate themost accurate LocalOffset so that we can coorelate ServerFrames with our local frames
		if (UE_NETWORK_PHYSICS::FixedLocalFrameOffset == INDEX_NONE)
		{
			// The server tells us what the latest InputCmd frame he processed from us when he did this tick (TODO)
			// There are N InputCmds in flight. This is precisely how many steps behind we should take this server frame number at.	
			if (APlayerController* PC = World->GetFirstPlayerController())
			{
				const APlayerController::FClientFrameInfo& ClientFrameInfo = PC->GetClientFrameInfo();
				const int32 LatestAckdClientFrame = ClientFrameInfo.LastProcessedInputFrame;
				const int32 LatestAckdServerFrame = ClientFrameInfo.LastRecvServerFrame;

				float RealTimeDilation = UE_NETWORK_PHYSICS::DeQuantizeTimeDilation(ClientFrameInfo.QuantizedTimeDilation);

				if (UE_NETWORK_PHYSICS::TimeDilationEnabled > 0)
				{
				PhysScene->SetNetworkDeltaTimeScale(RealTimeDilation);
				}

				if (LatestAckdClientFrame != INDEX_NONE)
				{
					const int32 FrameOffset = LatestAckdClientFrame - LatestAckdServerFrame;
					if (FrameOffset != LocalOffset)
					{
						UE_CLOG(UE_NETWORK_PHYSICS::LogCorrections > 0, LogNetworkPhysics, Log, TEXT("LocalOFfset Changed: %d -> %d"), LocalOffset, FrameOffset);
						LocalOffset = FrameOffset;
					}
				}
			}
		}
		else
		{
			// CVar that keeps us X +/- Y frames ahad of latest received server state
			int32 FrameOffset = 0;
			
			Chaos::FPhysicsSolver* Solver = PhysScene->GetSolver();
			checkSlow(Solver);

			int32 MaxFrameFromServer = INDEX_NONE;
			for (FNetworkPhysicsState* PhysicsState : ManagedPhysicsStates)
			{
				MaxFrameFromServer = FMath::Max(MaxFrameFromServer, PhysicsState->Frame);
			}

			// LocalFrame = ServerFrame + Offset
			FrameOffset = Solver->GetCurrentFrame() - UE_NETWORK_PHYSICS::FixedLocalFrameOffset - MaxFrameFromServer;

			if (FMath::Abs( LocalOffset - FrameOffset) > UE_NETWORK_PHYSICS::FixedLocalFrameOffsetTolerance)
			{
				UE_CLOG(UE_NETWORK_PHYSICS::LogCorrections > 0, LogNetworkPhysics, Warning, TEXT("LocalOFfset Changed: %d -> %d"), LocalOffset, FrameOffset);
				LocalOffset = FrameOffset;
			}
		}

		FVector LocalViewLoc;
		FRotator LocalViewRot;
		const float LODDistSq = UE_NETWORK_PHYSICS::LODDistance * UE_NETWORK_PHYSICS::LODDistance;
		if (UE_NETWORK_PHYSICS::EnableLOD > 0)
		{
			if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
			{
				PC->GetPlayerViewPoint(LocalViewLoc, LocalViewRot);
			}
		}

		// Go through the physics states that we are managing and look for new network updates and marshal them to PT with correct local frame
		for (FNetworkPhysicsState* PhysicsState : ManagedPhysicsStates)
		{
			check(PhysicsState);
			MaxFrameSeen = FMath::Max(PhysicsState->Frame, MaxFrameSeen);
			if (PhysicsState->Frame > LatestConfirmedFrame)
			{
				auto CreateMarshalledCopy = [&]()
				{
					const int32 LocalFrame = PhysicsState->Frame + LocalOffset;
					if (LocalFrame > 0 && PhysicsState->Physics.ObjectState != Chaos::EObjectStateType::Uninitialized)
					{
						FNetworkPhysicsState& MarshalledCopy = Snapshot.Objects.Emplace_GetRef(FNetworkPhysicsState(*PhysicsState));
						MarshalledCopy.Frame = LocalFrame;
					}
				};

				if (UE_NETWORK_PHYSICS::EnableLOD > 0)
				{
					if (PhysicsState->Proxy)
					{
						int32 CalculatedLOD = 0;

						if (UE_NETWORK_PHYSICS::MinLODForNonLocal != 0)
						{
							if (PhysicsState->OwningActor)
							{
								if (PhysicsState->OwningActor->GetLocalRole() == ROLE_SimulatedProxy)
								{
									CalculatedLOD = FMath::Max(CalculatedLOD, UE_NETWORK_PHYSICS::MinLODForNonLocal);
								}
							}
						}

						const FVector Location = PhysicsState->Proxy->GetGameThreadAPI().X();
						if (LODDistSq > 0.f && FVector::DistSquared(Location, LocalViewLoc) > LODDistSq)
						{
							CalculatedLOD = FMath::Max(CalculatedLOD, 1);
						}
					
						if (PhysicsState->LocalLOD != CalculatedLOD)
						{
							PhysicsState->LocalLOD = CalculatedLOD;
						}

						if (PhysicsState->LocalLOD == 0)
						{
							CreateMarshalledCopy();

							if (PhysicsState->OwningActor)
							{
							PhysicsState->OwningActor->GetReplicatedMovement_Mutable().bRepPhysics = false;
							PhysicsState->OwningActor->SetReplicateMovement(false);
						}
						}
						else
						{
							if (PhysicsState->OwningActor)
							{
								FRepMovement& RepMovement = PhysicsState->OwningActor->GetReplicatedMovement_Mutable();
								RepMovement.Location = PhysicsState->Physics.Location;
								RepMovement.Rotation = FRotator(PhysicsState->Physics.Rotation);
								RepMovement.LinearVelocity = PhysicsState->Physics.LinearVelocity;
								RepMovement.AngularVelocity = PhysicsState->Physics.AngularVelocity;
								RepMovement.bSimulatedPhysicSleep = (PhysicsState->Physics.ObjectState == Chaos::EObjectStateType::Sleeping);
								
								RepMovement.bRepPhysics = true;
								PhysicsState->OwningActor->SetReplicateMovement(true);
								PhysicsState->OwningActor->OnRep_ReplicatedMovement();
							}
						}
					}
				}
				else
				{
					CreateMarshalledCopy();
				}
			}
		}

		if (Snapshot.Objects.Num() > 0)
		{
			Snapshot.LocalFrameOffset = LocalOffset;
			RewindCallback->DataFromNetwork.Enqueue(MoveTemp(Snapshot));
		}

		for (auto& SubSys : SubSystems)
		{
			SubSys.Value->PostNetRecv(World, LocalOffset, LatestConfirmedFrame);
		}

		// The highest frame we encountered in the last network update is now sealed. We won't reconcile it again or any previous frames
		LatestConfirmedFrame = MaxFrameSeen;
	}
	
	TickDrawDebug();
#endif
}

void UNetworkPhysicsManager::PreNetSend(float DeltaSeconds)
{
#if WITH_CHAOS
	if (RewindCallback == nullptr)
	{
		return;
	}

	UWorld* World = GetWorld();
	checkSlow(World);

	for (auto& SubSys : SubSystems)
	{
		SubSys.Value->PreNetSend(World, DeltaSeconds);
	}


	const bool bIsServer = World->GetNetMode() != NM_Client;

	FNetworkPhysicsRewindCallback::FSnapshot Snapshot;
	bool bFoundData = false;
	while (RewindCallback->DataFromPhysics.Dequeue(Snapshot))
	{			
		bFoundData = true;
	}

	if (bFoundData)
	{
		if (bRecordDebugSnapshots)
		{
			DebugSnapshotHead = Snapshot.SimulationFrame;
			FDebugSnapshot& DebugSnapshot = DebugSnapshots[Snapshot.SimulationFrame % DebugSnapshots.Num()];
			DebugSnapshot.LocalFrameOffset = Snapshot.LocalFrameOffset;
			DebugSnapshot.Objects.Reset(Snapshot.Objects.Num());
			for (FNetworkPhysicsState& Obj : Snapshot.Objects)
			{
				FDebugSnapshot::FDebugObject& DebugObj = DebugSnapshot.Objects.AddDefaulted_GetRef();
				DebugObj.State = Obj;

				if (FBodyInstance* BodyInstance = FPhysicsUserData::Get<FBodyInstance>(Obj.Proxy->GetGameThreadAPI().UserData()))
				{
					if (UPrimitiveComponent* PrimComponent = BodyInstance->OwnerComponent.Get())
					{
						DebugObj.WeakOwningActor = PrimComponent->GetOwner();
					}
				}
			}
		}

		if (bIsServer)
		{
			for (FNetworkPhysicsState& Obj : Snapshot.Objects)
			{
				check(Obj.Proxy);

				// Copy data that was marshalled from PT to the managed FNetworkPhysicsState* that will be replicated.
				// The tricky thing is that the objects we are hearing back from the PT about maybe deleted on the GT already.
			
				// If we could use the physics proxy to safely get to the FNetworkPhysicsState, that would be ideal.
				// The interface approach sort of allows this... if the interface is implemented on the actor. So not actually workable.
				// If we just merged FNetworkPhysicsState into FRepMovement, we could just cast to actor and be done...

				// INetworkPhysicsObject* NetworkObject = Cast<INetworkPhysicsObject>(PhysicsState.Proxy->GetOwner());
				// FNetworkPhysicsState* DestState = NetworkObject->GetNetworkPhysicsState();

				// So we have to do the map lookup for now...

				// Actually this wont work either because we aren't round tripping the LocalManagedHandle to the PT

				/*			
				if (int32* IdxPtr = ManagedHandleToIndexMap.Find(PhysicsState.LocalManagedHandle))			
				{
					const int32 Idx = *IdxPtr;
					if (ensure(ManagedPhysicsStates.IsValidIndex(Idx)))
					{
						*ManagedPhysicsStates[Idx] = PhysicsState;
					}
				}
				*/

				// This is how it has to work for now, linear search to match the proxy pointer.
				// We might be able to leverage consistent ordering (each linear search through both Snapshot.Objects and ManagedPhysicsStates incrementally in step)
				//	(but be careful in the case where GT is ahead of PT and later requests have newer objects in lower positions in the MangedPhysicsState list...)
				for (FNetworkPhysicsState* ManagedState : ManagedPhysicsStates)
				{
					if (ManagedState->Proxy == Obj.Proxy)
					{
						ManagedState->Physics = Obj.Physics;					
						ManagedState->Frame = Obj.Frame;
#if NETWORK_PHYSICS_REPLICATE_EXTRAS
						for (int32 i=(int32)Chaos::FFrameAndPhase::EParticleHistoryPhase::PrePushData; i < (int32)Chaos::FFrameAndPhase::EParticleHistoryPhase::NumPhases; ++i)
						{
							ManagedState->DebugState[i] = Obj.DebugState[i];
						}
#endif
					}
				}
			}
		}
	}
#endif
}

void UNetworkPhysicsManager::ProcessInputs_External(int32 PhysicsStep, const TArray<Chaos::FSimCallbackInputAndObject>& SimCallbackInputs)
{
	UWorld* World = GetWorld();
	const ENetMode NetMode = World->GetNetMode();

	int32 FrameDelta = this->LatestConfirmedFrame - PhysicsStep;

	if (PhysicsStep % 500 == 0)
	{
		ClientServerMaxFrameDelta = 0;
	}

	ClientServerMaxFrameDelta = FMath::Max(ClientServerMaxFrameDelta, FrameDelta);

	// ----------------------------------------------------------------------
	//	Server: Do remote client input buffering logic
	//	This should probably be moved into PlayerController itself, with more options.
	//	We want this to happen before calling into INetworkPhysicsSubsystem::ProcessInputs_External.
	// ----------------------------------------------------------------------
	
	if (NetMode != NM_Client)
	{
		ProcessClientInputBuffers_External(PhysicsStep, SimCallbackInputs);
	}

	// ----------------------------------------------------------------------
	//	Call into Subsystems. This is where they will marshall input to the PT
	// ----------------------------------------------------------------------

	bool bOutSentClientInput = false;
	for (auto& Pair : SubSystems)
	{
		Pair.Value->ProcessInputs_External(PhysicsStep, LocalOffset, bOutSentClientInput);
	}

	// ----------------------------------------------------------------------
	// If we are a client and none of the subsystems sent ClientInput on their own,
	// then we should flush empty input to the server so that we can have an accurate
	// LocalFrameOffset. This is the critical piece of information for determining how
	// far ahead of the server we should be.
	// ----------------------------------------------------------------------
	if (NetMode == NM_Client && !bOutSentClientInput)
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			TArray<uint8> SendData;

			// ----------------------------------------------------------------------------------------------
			// REMOVE Once Mock/Hardcoded example is gone
			// ----------------------------------------------------------------------------------------------
			{
				FNetBitWriter Writer(nullptr, 256 << 3);

				for (Chaos::FSimCallbackInputAndObject Pair :  SimCallbackInputs)
				{
					if (Pair.Input->NetSendInputCmd(Writer))
					{
						break;
					}
				}

			
			
				if (Writer.IsError() == false)
				{
					int32 NumBytes = (int32)Writer.GetNumBytes();
					SendData = MoveTemp(*const_cast<TArray<uint8>*>(Writer.GetBuffer()));
					SendData.SetNum(NumBytes);
				}
			}

			// ----------------------------------------------------------------------------------------------
			// ----------------------------------------------------------------------------------------------
			// ----------------------------------------------------------------------------------------------
			PC->PushClientInput(PhysicsStep, SendData);
		}
	}
}

void UNetworkPhysicsManager::ProcessClientInputBuffers_External(int32 PhysicsStep, const TArray<Chaos::FSimCallbackInputAndObject>& SimCallbackInputs)
{
	UWorld* World = GetWorld();
	constexpr int32 MaxBufferedCmds = 16;

	for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator)
	{
		if (APlayerController* PC = Iterator->Get())
		{
			APlayerController::FServerFrameInfo& FrameInfo = PC->GetServerFrameInfo();
			APlayerController::FInputCmdBuffer& InputBuffer = PC->GetInputBuffer();

			auto UpdateLastProcessedInputFrame = [&]()
			{
				const int32 NumBufferedInputCmds = InputBuffer.HeadFrame() - FrameInfo.LastProcessedInputFrame;

				// Check Overflow
				if (NumBufferedInputCmds > MaxBufferedCmds)
				{
					UE_LOG(LogNetworkPhysics, Warning, TEXT("[Remote.Input] overflow %d %d -> %d"), InputBuffer.HeadFrame(), FrameInfo.LastProcessedInputFrame, NumBufferedInputCmds);
					FrameInfo.LastProcessedInputFrame = InputBuffer.HeadFrame() - MaxBufferedCmds + 1;
				}

				// Check fault - we are waiting for Cmds to reach TargetNumBufferedCmds before continuing
				if (FrameInfo.bFault)
				{
					if (NumBufferedInputCmds < (int32)FrameInfo.TargetNumBufferedCmds)
					{
						// Skip this because it is in fault. We will use the prev input for this frame.
						UE_CLOG(FrameInfo.LastProcessedInputFrame != INDEX_NONE, LogNetworkPhysics, Warning, TEXT("[Remote.Input] in fault. Reusing Inputcmd. (Client) Input: %d. (Server) Local Frame:"), FrameInfo.LastProcessedInputFrame, FrameInfo.LastLocalFrame);
						return;
					}
					FrameInfo.bFault = false;
				}
				else if (NumBufferedInputCmds <= 0)
				{
					// No Cmds to process, enter fault state. Increment TargetNumBufferedCmds each time this happens.
					// TODO: We should have something to bring this back down (which means skipping frames) we don't want temporary poor conditions to cause permanent high input buffering
					FrameInfo.bFault = true;
					FrameInfo.TargetNumBufferedCmds = FMath::Min(FrameInfo.TargetNumBufferedCmds+1.f, UE_NETWORK_PHYSICS::MaxTargetNumBufferedCmds);

					UE_CLOG(FrameInfo.LastProcessedInputFrame != INDEX_NONE, LogNetworkPhysics, Warning, TEXT("[Remote.Input] ENTERING fault. New Target: %.2f. (Client) Input: %d. (Server) Local Frame:"), FrameInfo.TargetNumBufferedCmds, FrameInfo.LastProcessedInputFrame, FrameInfo.LastLocalFrame);
					return;
				}

				float TargetTimeDilation = 1.f;
				if (NumBufferedInputCmds < (int32)FrameInfo.TargetNumBufferedCmds)
				{
					TargetTimeDilation += UE_NETWORK_PHYSICS::MaxTimeDilationMag; // Tell client to speed up, we are starved on cmds
				}
				else if (NumBufferedInputCmds > (int32)FrameInfo.TargetNumBufferedCmds)
				{
					TargetTimeDilation -= UE_NETWORK_PHYSICS::MaxTimeDilationMag; // Tell client to slow down, we have too many buffered cmds

					if (UE_NETWORK_PHYSICS::LerpTargetNumBufferedCmdsAggresively == 0)
					{
						// When non gaggressive, only lerp when we are above our limit
						FrameInfo.TargetNumBufferedCmds = FMath::Lerp(FrameInfo.TargetNumBufferedCmds, UE_NETWORK_PHYSICS::TargetNumBufferedCmds, UE_NETWORK_PHYSICS::TargetNumBufferedCmdsAlpha);
					}
				}
				
				FrameInfo.TargetTimeDilation = FMath::Lerp(FrameInfo.TargetTimeDilation, TargetTimeDilation, UE_NETWORK_PHYSICS::TimeDilationAlpha);
				FrameInfo.QuantizedTimeDilation = UE_NETWORK_PHYSICS::QuantizeTimeDilation(TargetTimeDilation);

				if (UE_NETWORK_PHYSICS::LerpTargetNumBufferedCmdsAggresively != 0)
				{
					// When gaggressive, always lerp towards target
					FrameInfo.TargetNumBufferedCmds = FMath::Lerp(FrameInfo.TargetNumBufferedCmds, UE_NETWORK_PHYSICS::TargetNumBufferedCmds, UE_NETWORK_PHYSICS::TargetNumBufferedCmdsAlpha);
				}

				FrameInfo.LastProcessedInputFrame++;
				FrameInfo.LastLocalFrame = PhysicsStep;
			};

			UpdateLastProcessedInputFrame();


			// ----------------------------------------------------------------------------------------------
			// REMOVE Once Mock/Hardcoded example is gone
			// ----------------------------------------------------------------------------------------------
			if (FrameInfo.LastProcessedInputFrame != INDEX_NONE)
			{
				const TArray<uint8>& Data = InputBuffer.Get(FrameInfo.LastProcessedInputFrame);
				if (Data.Num() > 0)
				{
					uint8* RawData = const_cast<uint8*>(Data.GetData());
					FNetBitReader Ar(nullptr, RawData, ((int64)Data.Num()) << 3);
						
					for (Chaos::FSimCallbackInputAndObject Pair :  SimCallbackInputs)
					{
						if (Pair.Input->NetRecvInputCmd(PC, Ar))
						{
							break;
						}
					}
				}
			}
			// ----------------------------------------------------------------------------------------------
			// ----------------------------------------------------------------------------------------------
			// ----------------------------------------------------------------------------------------------
		}
	}
}

void UNetworkPhysicsManager::RegisterPhysicsProxy(FNetworkPhysicsState* State)
{
	checkSlow(State);

	if (!ensureMsgf(State->Proxy, TEXT("No proxy set on FNetworkPhysicsState")))
	{
		return;
	}

	State->LocalManagedHandle = ++UniqueHandleCounter;
	ManagedHandleToIndexMap.Add(UniqueHandleCounter) = ManagedPhysicsStates.EmplaceAtLowestFreeIndex(LastFreeIndex, State);
}

void UNetworkPhysicsManager::UnregisterPhysicsProxy(FNetworkPhysicsState* State)
{
	checkSlow(State);
	const int32 LocalHandle = State->LocalManagedHandle;

	if (LocalHandle != INDEX_NONE)
	{
		int32 LocalIndex;
		if (ensure(ManagedHandleToIndexMap.RemoveAndCopyValue(LocalHandle, LocalIndex)))
		{
			ManagedPhysicsStates.RemoveAt(LocalIndex);
			LastFreeIndex = FMath::Min(LastFreeIndex, LocalIndex);
		}
		
		State->LocalManagedHandle = INDEX_NONE;
	}

	DrawDebugMap.Remove(State->Proxy);
}

void UNetworkPhysicsManager::RegisterPhysicsProxyDebugDraw(FNetworkPhysicsState* State, TUniqueFunction<void(const FDrawDebugParams&)>&& Func)
{
	DrawDebugMap.Add(State->Proxy, MoveTemp(Func));
}

void UNetworkPhysicsManager::TickDrawDebug()
{
	if (UE_NETWORK_PHYSICS::Debug <= 0)
	{
		if (RewindCallback)
		{
			RewindCallback->StatsForGT.Empty();
		}
		return;
	}

	static TWeakObjectPtr<UWorld> ServerWorld;
	UWorld* ThisWorld = GetWorld();
	ENetMode ThisNetMode = ThisWorld->GetNetMode();
	if (ServerWorld.IsValid() == false && ThisNetMode != NM_Client)
	{
		ServerWorld = GetWorld();
	}

	if (ThisNetMode == NM_DedicatedServer)
	{
		return;
	}

	FDrawDebugParams P;
	P.DrawWorld = ThisWorld;

	if (ServerWorld.IsValid())
	{
		// Draw the server's current state in our world.
		// (This only works in PIE)
		if (UNetworkPhysicsManager* ServerManager = ServerWorld->GetSubsystem<UNetworkPhysicsManager>())
		{
			P.Color = FColor::Purple;
			P.Lifetime = -1.f;

			for (FNetworkPhysicsState* State : ServerManager->ManagedPhysicsStates)
			{
				P.Loc = State->Proxy->GetGameThreadAPI().X();
				P.Rot = State->Proxy->GetGameThreadAPI().R();
				if (auto Func = ServerManager->DrawDebugMap.Find(State->Proxy))
				{
					(*Func)(P);
				}
			}
		}
	}

	if (ThisNetMode == NM_Client)
	{
		// Draw corrections
		P.Lifetime = 5.f;

		while(RewindCallback->StatsForGT.IsEmpty() == false)
		{
			FNetworkPhysicsRewindCallback::FStats& Stats = RewindCallback->RecordedStatsGT[RewindCallback->LastRecordedStat++ % RewindCallback->RecordedStatsGT.Num()];
			ensure(RewindCallback->StatsForGT.Dequeue(Stats));

			for (auto& Correction : Stats.Corrections)
			{
				if (UE_NETWORK_PHYSICS::DebugTolerance > 0)
				{
					if (FVector::Distance(Correction.LocalState.Location, Correction.AuthorityState.Location) < UE_NETWORK_PHYSICS::DebugDrawTolerance_X)
					{
						continue;
					}

					if (FQuat::ErrorAutoNormalize(Correction.LocalState.Rotation, Correction.AuthorityState.Rotation) < UE_NETWORK_PHYSICS::DebugDrawTolerance_R)
					{
						continue;
					}
				}

				if (auto Func = DrawDebugMap.Find(Correction.Proxy))
				{
					P.Color = FColor::Red;
					P.Loc = Correction.LocalState.Location;
					P.Rot = Correction.LocalState.Rotation;
					(*Func)(P);

					P.Color = FColor::Green;
					P.Loc = Correction.AuthorityState.Location;
					P.Rot = Correction.AuthorityState.Rotation;
					(*Func)(P);
				}
			}
		}

		if (UE_NETWORK_PHYSICS::Debug > 1)
		{
			// Draw last recv state in blue
			// (This ends up being pretty noisey so is only enabled in debug mode > 1)
			P.Color = FColor::Blue;
			P.Lifetime = -1.f;

			for (FNetworkPhysicsState* State : ManagedPhysicsStates)
			{
				P.Loc = State->Physics.Location;
				P.Rot = State->Physics.Rotation;
				if (auto Func = DrawDebugMap.Find(State->Proxy))
				{
					(*Func)(P);
				}


				DrawDebugString(ThisWorld, P.Loc + FVector(0.f, 0.f, 100.f), LexToString(State->Frame), nullptr, FColor::White, 0.f);
			}
		}
	}

	static FDelegateHandle OnShowDebugInfoHandle;
	if (OnShowDebugInfoHandle.IsValid() == false)
	{
		struct FLocal
		{
			using FStats = FNetworkPhysicsRewindCallback::FStats;

			// implementing via static func so that Live Coding picks up changes
			static void DrawHUD(AHUD* HUD, UCanvas* Canvas, UNetworkPhysicsManager* Manager, UNetworkPhysicsManager* ServerManager)
			{
				float YPos = 15.f; //16;
				float XPos = 23.f;

				// FLinearColor BackgroundColor(0.f, 0.f, 0.f, 0.2f);
				// Canvas->DrawTile(nullptr, XPos - 4.f, YPos - 4.f, 100.f, 300.f, 0.f, 0.f, 0.f, 0.f, BackgroundColor);

				Canvas->SetDrawColor(FColor::Red);
				YPos += Canvas->DrawText(GEngine->GetMediumFont(), FString::Printf(TEXT("NETWORK PHYSICS MANAGER - %d Managed Objects"), Manager->ManagedPhysicsStates.Num()), XPos, YPos);
				//Canvas->SetDrawColor(FColor::White);

				FPhysScene* PhysScene = Manager->GetWorld()->GetPhysicsScene();
				Chaos::FPhysicsSolver* Solver = PhysScene->GetSolver();
				const int32 LatestSimulatedFrame = Solver->GetCurrentFrame() - Manager->LocalOffset;
				YPos += Canvas->DrawText(GEngine->GetMediumFont(), FString::Printf(TEXT("Num Predicted Frames: %d"), (LatestSimulatedFrame - Manager->LatestConfirmedFrame)), XPos, YPos);

				APlayerController* LocalPC = GEngine->GetFirstLocalPlayerController(Manager->GetWorld());
				const APlayerController::FClientFrameInfo& ClientFrameInfo = LocalPC->GetClientFrameInfo();

				if (APlayerState* PS = LocalPC->GetPlayerState<APlayerState>())
				{
					int32 ActualPing = PS->GetPing() * 4;
					YPos += Canvas->DrawText(GEngine->GetMediumFont(), FString::Printf(TEXT("Ping: %d"), ActualPing), XPos, YPos);
				}

				
				YPos += Canvas->DrawText(GEngine->GetMediumFont(), FString::Printf(TEXT("ClientServerMaxFrameDelta: %d"), Manager->ClientServerMaxFrameDelta), XPos, YPos);
				
				if (APlayerController* ServerPC = Cast<APlayerController>(NetworkPredictionDebug::FindReplicatedObjectOnPIEServer(LocalPC)))
				{
					int32 NumBufferedCmd = ServerPC->GetInputBuffer().HeadFrame() - ServerPC->GetServerFrameInfo().LastProcessedInputFrame;
					YPos += Canvas->DrawText(GEngine->GetMediumFont(), FString::Printf(TEXT("[PIE] TargetNumBufferedCmds: %f"), ServerPC->GetServerFrameInfo().TargetNumBufferedCmds), XPos, YPos);
					YPos += Canvas->DrawText(GEngine->GetMediumFont(), FString::Printf(TEXT("[PIE] Buffered InputCmds: %d"), NumBufferedCmd), XPos, YPos);
				}
				//else
				{
					YPos += Canvas->DrawText(GEngine->GetMediumFont(), FString::Printf(TEXT("TargetNumBufferedCmds: %.4f "), ClientFrameInfo.TargetNumBufferedCmds), XPos, YPos);
				}

				{
					int32 NumBufferedCmd = ClientFrameInfo.LastRecvInputFrame - ClientFrameInfo.LastProcessedInputFrame;
					YPos += Canvas->DrawText(GEngine->GetMediumFont(), FString::Printf(TEXT("Buffered InputCmds: %d"), NumBufferedCmd), XPos, YPos);
				}

				{
					const float TimeDilation = UE_NETWORK_PHYSICS::DeQuantizeTimeDilation(ClientFrameInfo.QuantizedTimeDilation);
					YPos += Canvas->DrawText(GEngine->GetMediumFont(), FString::Printf(TEXT("Local TimeDilation: %f"), TimeDilation), XPos, YPos);
				}
				
				

				YPos += 20.f;

				const float W = 2.f;
				const float H = 80.f;


				auto& RecordedStatsGT = Manager->RewindCallback->RecordedStatsGT;
				const int32 LastRecordedStat = Manager->RewindCallback->LastRecordedStat;
				const int32 StartStat = LastRecordedStat - RecordedStatsGT.Num();
				
				const float HS = H / (float)Manager->ManagedPhysicsStates.Num();

				for (int32 i=0; i < RecordedStatsGT.Num(); ++i)
				{
					const int32 StatIdx = StartStat + i;
					if (StatIdx < 0)
					{
						continue;
					}

					FStats& Stat = RecordedStatsGT[StatIdx % RecordedStatsGT.Num()];

					const float X = XPos + (i * W);
					auto DrawLine = [&](const float Value, const FLinearColor& Color)
					{
						if (Value > 0.f)
						{
							float LH = FMath::Min((HS * Value), H);
							Canvas->K2_DrawLine( FVector2D(X, YPos + H), FVector2D(X, YPos + H - LH), W, Color );
						}
					};

					if (Stat.LocalOffsetChanged)
					{
						DrawLine(Manager->ManagedPhysicsStates.Num(), FLinearColor::Green);
					}
					else
					{
						DrawLine(Manager->ManagedPhysicsStates.Num(), FLinearColor::Gray);
						DrawLine(Stat.NumChecked, FLinearColor::Blue);
						DrawLine(Stat.Corrections.Num() + Stat.NumSubsystemCorrections, FLinearColor::Red);
						DrawLine(Stat.NumSubsystemCorrections, FLinearColor::Yellow);
					}
				}

				YPos += H + 5.f;
				Canvas->SetDrawColor(FColor::Blue);
				YPos += Canvas->DrawText(GEngine->GetMediumFont(), TEXT("ShouldReconcile Comparisons "), XPos, YPos);
				Canvas->SetDrawColor(FColor::Red);
				YPos += Canvas->DrawText(GEngine->GetMediumFont(), TEXT("Physics Correction"), XPos, YPos);
				Canvas->SetDrawColor(FColor::Yellow);
				YPos += Canvas->DrawText(GEngine->GetMediumFont(), TEXT("GameObj Correction"), XPos, YPos);
				Canvas->SetDrawColor(FColor::Green);
				YPos += Canvas->DrawText(GEngine->GetMediumFont(), TEXT("Input Buffer Faults"), XPos, YPos);
			}
		};

		//OnShowDebugInfoHandle = AHUD::OnHUDPostRender.AddLambda([this](AHUD* HUD, UCanvas* Canvas, const FDebugDisplayInfo& DisplayInfo, float& YL, float& YPos)
		OnShowDebugInfoHandle = AHUD::OnHUDPostRender.AddLambda([this](AHUD* HUD, UCanvas* Canvas)
		{
			if (UE_NETWORK_PHYSICS::Debug <= 0)
			{
				return;
			}

			UWorld* HUDWorld = HUD->GetWorld();
			if (UNetworkPhysicsManager* Manager = HUDWorld->GetSubsystem<UNetworkPhysicsManager>())
			{
				UNetworkPhysicsManager* ServerManager = ServerWorld.IsValid() ? ServerWorld->GetSubsystem<UNetworkPhysicsManager>() : nullptr;
				FLocal::DrawHUD(HUD, Canvas, Manager, ServerManager);
			}
		});
	}
}

void UNetworkPhysicsManager::DumpDebugHistory()
{
	FStringOutputDevice Out;

	TMap<TWeakObjectPtr<AActor>, FNetworkGUID> GUIDMap;

	for (int32 i= FMath::Max(0, DebugSnapshotHead - DebugSnapshots.Num() + 1); i <= DebugSnapshotHead; ++i)
	{
		FDebugSnapshot& Snapshot = DebugSnapshots[i % DebugSnapshots.Num()];

		for (FDebugSnapshot::FDebugObject& Obj : Snapshot.Objects)
		{
			if (Obj.WeakOwningActor.IsValid() == false)
			{
				continue;
			}

			FNetworkGUID GUID;
			if (FNetworkGUID* FoundGUID = GUIDMap.Find(Obj.WeakOwningActor))
			{
				GUID = *FoundGUID;
			}
			else
			{
				GUID = NetworkPredictionDebug::FindObjectNetGUID(Obj.WeakOwningActor.Get());
				GUIDMap.Add(Obj.WeakOwningActor, GUID);
			}

			Out.Logf(TEXT("[%d][n:%-4d][%d] X: %-45sR: %-64sV: %-40sW: %-40s\n"), i, GUID.Value, Snapshot.LocalFrameOffset, *Obj.State.Physics.Location.ToString(), *Obj.State.Physics.Rotation.ToString(), *Obj.State.Physics.LinearVelocity.ToString(), *Obj.State.Physics.AngularVelocity.ToString());
		}
	}

	Out.Logf(TEXT("\n"));
	for (auto& MapIt : GUIDMap)
	{
		Out.Logf(TEXT("[n:%-4d] -> %s\n"), MapIt.Value.Value, *GetNameSafe(MapIt.Key.Get()));
	}

	FString Path = FPaths::ProfilingDir() + FString::Printf(TEXT("/NetworkPrediction/NpDump_%s_%d.txt"), GetWorld()->GetNetMode() == NM_Client ? TEXT("Client") : TEXT("Server"), DebugSnapshotHead);
	FFileHelper::SaveStringToFile(Out, *Path);

}

FAutoConsoleCommandWithWorldAndArgs NpDumpCmd(TEXT("np2.Dump"), TEXT(""),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray< FString >& Args, UWorld* InWorld) 
{
	if (UNetworkPhysicsManager* Manager = InWorld->GetSubsystem<UNetworkPhysicsManager>())
	{
		Manager->DumpDebugHistory();
	}

	if (InWorld->GetNetMode() == NM_Client)
	{
		if (APlayerController* PC = GEngine->GetFirstLocalPlayerController(InWorld))
		{
			PC->ServerExec(TEXT("np2.dump"));
		}
	}
}));

FAutoConsoleCommandWithWorldAndArgs NpFloatTestCmd(TEXT("np2.FloatTest"), TEXT(""),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray< FString >& Args, UWorld* InWorld) 
{

	for (float F= 0.99f; F <= 1.01f; F += 0.00001f)
	{
		int8 Compressed = UE_NETWORK_PHYSICS::QuantizeTimeDilation(F);
		float UnCompressed = UE_NETWORK_PHYSICS::DeQuantizeTimeDilation(Compressed);
		UE_LOG(LogTemp, Log, TEXT("%.4f -> %d -> %.4f (%.4f error"), F, Compressed, UnCompressed, UnCompressed - F);
	}

}));