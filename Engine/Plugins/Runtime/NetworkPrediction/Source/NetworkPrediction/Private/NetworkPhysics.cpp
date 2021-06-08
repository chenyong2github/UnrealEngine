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
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "NetworkPredictionDebug.h"
#include "GameFramework/PlayerState.h"
#include "Misc/FileHelper.h"

DEFINE_LOG_CATEGORY(LogNetworkPhysics);

namespace UE_NETWORK_PHYSICS
{
	float X = 1.0;	FAutoConsoleVariableRef CVarX(TEXT("np2.Tolerance.X"), X, TEXT("Location Tolerance"));
	float R = 0.1;	FAutoConsoleVariableRef CVarR(TEXT("np2.Tolerance.R"), R, TEXT("Rotation Tolerance"));
	float V = 1.0;	FAutoConsoleVariableRef CVarV(TEXT("np2.Tolerance.V"), V, TEXT("Velocity Tolerance"));
	float W = 1.0;	FAutoConsoleVariableRef CVarW(TEXT("np2.Tolerance.W"), W, TEXT("Rotational Velocity Tolerance"));

	float DebugDrawTolerance_X = 5.0;
	FAutoConsoleVariableRef CVarDebugX(TEXT("np2.Debug.Tolerance.X"), DebugDrawTolerance_X, TEXT("Location Debug Drawing Toleration"));

	float DebugDrawTolerance_R = 2.0;
	FAutoConsoleVariableRef CVarDebugR(TEXT("np2.Debug.Tolerance.R"), DebugDrawTolerance_R, TEXT("Rotation Debug Drawing Tolerance"));

	int32 Debug = 0;
	FAutoConsoleVariableRef CVarDebug(TEXT("np2.Debug"), Debug, TEXT("Debug mode for in world drawing"));

	int32 DebugTolerance = 0;
	FAutoConsoleVariableRef CVarDebugTolerance(TEXT("np2.Debug.Tolerance"), DebugTolerance, TEXT("If enabled, only draw large corrections in world."));

	bool bForceResim=false;
	FAutoConsoleVariableRef CVarResim(TEXT("np2.ForceResim"), bForceResim, TEXT("Forces near constant resimming"));

	bool ForceResimWithoutCorrection=false;
	FAutoConsoleVariableRef CVarForceResimWithoutCorrection(TEXT("np2.ForceResimWithoutCorrection"), ForceResimWithoutCorrection, TEXT("Forces near constant resimming WITHOUT applying server data"));

	bool bEnable=false;
	FAutoConsoleVariableRef CVarEnable(TEXT("np2.bEnable"), bEnable, TEXT("Enabled rollback physics. Must be set before starting game"));

	bool bLogCorrections=true;
	FAutoConsoleVariableRef CVarLogCorrections(TEXT("np2.LogCorrections"), bLogCorrections, TEXT("Logs corrections when they happen"));

	bool bLogImpulses=false;
	FAutoConsoleVariableRef CVarLogImpulses(TEXT("np2.LogImpulses"), bLogImpulses, TEXT("Logs all recorded F/T/LI/AI"));

	int32 FixedLocalFrameOffset = -1;
	FAutoConsoleVariableRef CVarFixedLocalFrameOffset(TEXT("np2.FixedLocalFrameOffset"), FixedLocalFrameOffset, TEXT("When > 0, use hardcoded frame offset on client from head"));

	int32 FixedLocalFrameOffsetTolerance = 3;
	FAutoConsoleVariableRef CVarFixedLocalFrameOffsetTolerance(TEXT("np2.FixedLocalFrameOffsetTolerance"), FixedLocalFrameOffsetTolerance, TEXT("When > 0, use hardcoded frame offset on client from head"));

	int32 EnableLOD = 0;
	FAutoConsoleVariableRef CVarEnableLOD(TEXT("np2.EnableLOD"), EnableLOD, TEXT("Enable local LOD mode"));

	float LODDistance = 2400.f;
	FAutoConsoleVariableRef CVarLODDistance(TEXT("np2.LODDistance"), LODDistance, TEXT("Simple distance based LOD"));

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
}

struct FNetworkPhysicsRewindCallback : public Chaos::IRewindCallback
{
	bool CompareVec(const FVector& A, const FVector& B, const float D, const TCHAR* Str)
	{
		const bool b = D == 0 ? A != B : FVector::DistSquared(A, B) > D * D;
		UE_CLOG(UE_NETWORK_PHYSICS::bLogCorrections && b && Str, LogNetworkPhysics, Log, TEXT("%s correction. Server: %s. Local: %s. Delta: %s (%.4f)"), Str, *A.ToString(), *B.ToString(), *(A-B).ToString(), (A-B).Size());
		return  b;
	}
	bool CompareQuat(const FQuat& A, const FQuat& B, const float D, const TCHAR* Str)
	{
		const bool b = D == 0 ? A != B : FQuat::ErrorAutoNormalize(A, B) > D;
		UE_CLOG(UE_NETWORK_PHYSICS::bLogCorrections && b && Str, LogNetworkPhysics, Log, TEXT("%s correction. Server: %s. Local: %s. Delta: %f"), Str, *A.ToString(), *B.ToString(), FQuat::ErrorAutoNormalize(A, B));
		return b;
	}
	bool CompareObjState(Chaos::EObjectStateType A, Chaos::EObjectStateType B, const TCHAR* Str)
	{
		const bool b = A != B;
		UE_CLOG(UE_NETWORK_PHYSICS::bLogCorrections && b && Str, LogNetworkPhysics, Log, TEXT("%s correction. Server: %d. Local: %d."), Str, A, B);
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

		if (UE_NETWORK_PHYSICS::ForceResimWithoutCorrection)
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
				
				if (CompareObjState(Obj.Physics.ObjectState, P.ObjectState(), TEXT("DEBUG ObjectState")))
				//if (CompareVec(Obj.Physics.Location, P.X(), UE_NETWORK_PHYSICS::X, TEXT("Location")))
				{
					/*
					UE_LOG(LogTemp, Warning, TEXT("ObjectState Correction. Obj.Frame: %d. CurrentFramE: %d"), Obj.Frame, RewindData->CurrentFrame());
					//UE_LOG(LogTemp, Warning, TEXT("Location Correction. Obj.Frame: %d. CurrentFrame: %d"), Obj.Frame, RewindData->CurrentFrame());
					
					for (int32 F = Obj.Frame-1; F <= RewindData->CurrentFrame(); ++F)
					{
						const auto PP = RewindData->GetPastStateAtFrame(*Proxy->GetHandle_LowLevel(), F);
						////UE_LOG(LogTemp, Warning, TEXT("    [%d/%d] --> %s"), F, F-Snapshot.LocalFrameOffset, *FRotator(PP.R()).ToString());
						//UE_LOG(LogTemp, Warning, TEXT("    [%d/%d] --> %s"), F, F-Snapshot.LocalFrameOffset, *FVector(PP.X()).ToString());
						UE_LOG(LogTemp, Warning, TEXT("    [%d/%d] --> %d"), F, F-Snapshot.LocalFrameOffset, (int32)PP.ObjectState());
					}
					UE_LOG(LogTemp, Warning, TEXT(""));
					*/
				}

				Stats.MinFrameChecked = FMath::Min(Stats.MaxFrameChecked, Obj.Frame);
				Stats.MaxFrameChecked = FMath::Max(Stats.MaxFrameChecked, Obj.Frame);
				Stats.NumChecked++;


#if NETWORK_PHYSICS_REPLICATE_EXTRAS
				if (UE_NETWORK_PHYSICS::bLogImpulses)
				{
					for (int32 i=(int32)Chaos::FParticleHistoryEntry::EParticleHistoryPhase::PrePushData; i < (int32)Chaos::FParticleHistoryEntry::EParticleHistoryPhase::NumPhases; ++i)
					{
						if (Obj.DebugState[i].LinearImpulse.SizeSquared() > 0.f)
						{
							UE_LOG(LogNetworkPhysics, Warning, TEXT("[C] server told us they applied Linear Impulse %s on Frame [%d] in Phase [%d]"), *Obj.DebugState[i].LinearImpulse.ToString(), SimulationFrame-1, i);
						}
					}
				}
#endif
				
				if (CompareObjState(Obj.Physics.ObjectState, P.ObjectState(), TEXT("ObjectState")) ||
					CompareVec(Obj.Physics.Location, P.X(), UE_NETWORK_PHYSICS::X, TEXT("Location")) ||
					CompareVec(Obj.Physics.LinearVelocity, P.V(), UE_NETWORK_PHYSICS::V, TEXT("Velocity")) ||
					CompareVec(Obj.Physics.AngularVelocity, P.W(), UE_NETWORK_PHYSICS::V, TEXT("Angular Velocity")) ||
					CompareQuat(Obj.Physics.Rotation, P.R(), UE_NETWORK_PHYSICS::R, TEXT("Rotation")) || UE_NETWORK_PHYSICS::bForceResim)
				{

					UE_CLOG(UE_NETWORK_PHYSICS::bLogCorrections, LogNetworkPhysics, Log, TEXT("Rewind Needed. SimFrame: %d. Obj.Frame: %d. LastCompletedStep: %d."), SimulationFrame, Obj.Frame, LastCompletedStep);

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
					if (UE_NETWORK_PHYSICS::bLogCorrections && Obj.Frame-1 >= RewindData->GetEarliestFrame_Internal())
					{
						const int32 PreSimulationFrame = SimulationFrame-1;
						for (int32 i=(int32)Chaos::FParticleHistoryEntry::EParticleHistoryPhase::PrePushData; i < (int32)Chaos::FParticleHistoryEntry::EParticleHistoryPhase::NumPhases; ++i)
						{
							const auto LocalPreFrameData = RewindData->GetPastStateAtFrame(*Proxy->GetHandle_LowLevel(), Obj.Frame-1, (Chaos::FParticleHistoryEntry::EParticleHistoryPhase)i);
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
			UE_CLOG(UE_NETWORK_PHYSICS::bLogCorrections && RewindToFrame != INDEX_NONE, LogNetworkPhysics, Warning, TEXT("Trying to rewind to invalid frame %d (LastCompletedStep: %d)"), RewindToFrame, LastCompletedStep);
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

				if (auto* PT = CorrectionState.Proxy->GetPhysicsThreadAPI())
				{
					UE_CLOG(UE_NETWORK_PHYSICS::bLogCorrections, LogNetworkPhysics, Log, TEXT("Applying Correction from frame %d (actual step: %d). Location: %s"), CorrectionState.Frame, PhysicsStep, *FVector(CorrectionState.Physics.Location).ToString());
				
					PT->SetX(CorrectionState.Physics.Location, false);
					PT->SetV(CorrectionState.Physics.LinearVelocity, false);
					PT->SetR(CorrectionState.Physics.Rotation, false);
					PT->SetW(CorrectionState.Physics.AngularVelocity, false);
					
					//if (PT->ObjectState() != CorrectionState.Physics.ObjectState)
					{
						ensure(CorrectionState.Physics.ObjectState != Chaos::EObjectStateType::Uninitialized);
						UE_CLOG(UE_NETWORK_PHYSICS::bLogCorrections, LogNetworkPhysics, Log, TEXT("Applying Correction State %d"), CorrectionState.Physics.ObjectState);
						PT->SetObjectState(CorrectionState.Physics.ObjectState);
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
						for (int32 i=(int32)Chaos::FParticleHistoryEntry::EParticleHistoryPhase::PrePushData; i < (int32)Chaos::FParticleHistoryEntry::EParticleHistoryPhase::NumPhases; ++i)
						{
							Chaos::FGeometryParticleState State = RewindData->GetPastStateAtFrame(*Proxy->GetHandle_LowLevel(), PreStorageFrame, (Chaos::FParticleHistoryEntry::EParticleHistoryPhase)i);
							Obj.DebugState[i].Force = State.F();
							Obj.DebugState[i].Torque = State.Torque();
							Obj.DebugState[i].LinearImpulse = State.LinearImpulse();
							Obj.DebugState[i].AngularImpulse = State.AngularImpulse();

							if (UE_NETWORK_PHYSICS::bLogImpulses)
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
		if (UE_NETWORK_PHYSICS::bEnable)
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

			Solver->EnableRewindCapture(64, true, MakeUnique<FNetworkPhysicsRewindCallback>());
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
				const int32 LatestAckdClientFrame = ClientFrameInfo.LastRecvInputFrame;
				const int32 LatestAckdServerFrame = ClientFrameInfo.LastRecvServerFrame;
				if (LatestAckdClientFrame != INDEX_NONE)
				{
					const int32 FrameOffset = LatestAckdClientFrame - LatestAckdServerFrame;
					if (FrameOffset != LocalOffset)
					{
						UE_CLOG(UE_NETWORK_PHYSICS::bLogCorrections, LogNetworkPhysics, Log, TEXT("LocalOFfset Changed: %d -> %d"), LocalOffset, FrameOffset);
						LocalOffset = FrameOffset;
					}

					FPhysScene* PhysScene = World->GetPhysicsScene();
					checkSlow(PhysScene);			
					Chaos::FPhysicsSolver* Solver = PhysScene->GetSolver();
					checkSlow(Solver);

					//const int32 UnAckdFrames = Solver->GetCurrentFrame() - LatestAckdClientFrame;
					//UE_LOG(LogTemp, Warning, TEXT("%d UnAckdFrames"), UnAckdFrames);
				}
			}
		}
		else
		{
			// CVar that keeps us X +/- Y frames ahad of latest received server state
			int32 FrameOffset = 0;
			
			FPhysScene* PhysScene = World->GetPhysicsScene();
			checkSlow(PhysScene);			
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
				UE_CLOG(UE_NETWORK_PHYSICS::bLogCorrections, LogNetworkPhysics, Warning, TEXT("LocalOFfset Changed: %d -> %d"), LocalOffset, FrameOffset);
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
						const FVector Location = PhysicsState->Proxy->GetGameThreadAPI().X();
						if (FVector::DistSquared(Location, LocalViewLoc) > LODDistSq)
						{
							CalculatedLOD = 1;
						}
					
						if (PhysicsState->LocalLOD != CalculatedLOD)
						{
							PhysicsState->LocalLOD = CalculatedLOD;
						}

						if (PhysicsState->LocalLOD == 0)
						{
							CreateMarshalledCopy();

							PhysicsState->OwningActor->GetReplicatedMovement_Mutable().bRepPhysics = false;
							PhysicsState->OwningActor->SetReplicateMovement(false);
						}
						else
						{
							if (ensure(PhysicsState->OwningActor))
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
						for (int32 i=(int32)Chaos::FParticleHistoryEntry::EParticleHistoryPhase::PrePushData; i < (int32)Chaos::FParticleHistoryEntry::EParticleHistoryPhase::NumPhases; ++i)
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

					// Check fault - we are waiting for Cmds to reach FaultLimit before continuing
					if (FrameInfo.bFault)
					{
						if (NumBufferedInputCmds < FrameInfo.FaultLimit)
						{
							// Skip this because it is in fault. We will use the prev input for this frame.
							UE_CLOG(FrameInfo.LastProcessedInputFrame != INDEX_NONE, LogNetworkPhysics, Warning, TEXT("[Remote.Input] in fault. Reusing Inputcmd. (Client) Input: %d. (Server) Local Frame:"), FrameInfo.LastProcessedInputFrame, FrameInfo.LastLocalFrame);
							return;
						}
						FrameInfo.bFault = false;
					}
					else if (NumBufferedInputCmds <= 0)
					{
						// No Cmds to process, enter fault state. Increment FaultLimit each time this happens.
						// TODO: We should have something to bring this back down (which means skipping frames) we don't want temporary poor conditions to cause permanent high input buffering
						FrameInfo.bFault = true;
						FrameInfo.FaultLimit = FMath::Min(FrameInfo.FaultLimit+1, MaxBufferedCmds-1);

						UE_CLOG(FrameInfo.LastProcessedInputFrame != INDEX_NONE, LogNetworkPhysics, Warning, TEXT("[Remote.Input] ENTERING fault. New Limit: %d. (Client) Input: %d. (Server) Local Frame:"), FrameInfo.FaultLimit, FrameInfo.LastProcessedInputFrame, FrameInfo.LastLocalFrame);
						return;
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
			// REMOVE Once Mock/Hardcoded exmaple is gone
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

void UNetworkPhysicsManager::RegisterPhysicsProxy(FNetworkPhysicsState* State)
{
	checkSlow(State);
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

				if (APlayerState* PS = LocalPC->GetPlayerState<APlayerState>())
				{
					YPos += Canvas->DrawText(GEngine->GetMediumFont(), FString::Printf(TEXT("Ping: %d"), PS->GetPing()), XPos, YPos);
				}

				
				YPos += Canvas->DrawText(GEngine->GetMediumFont(), FString::Printf(TEXT("ClientServerMaxFrameDelta: %d"), Manager->ClientServerMaxFrameDelta), XPos, YPos);
				
				if (APlayerController* ServerPC = Cast<APlayerController>(NetworkPredictionDebug::FindReplicatedObjectOnPIEServer(LocalPC)))
				{
					int32 NumBufferedCmd = ServerPC->GetInputBuffer().HeadFrame() - ServerPC->GetServerFrameInfo().LastProcessedInputFrame;
					YPos += Canvas->DrawText(GEngine->GetMediumFont(), FString::Printf(TEXT("Server-Side Buffered InputCmds: %d"), NumBufferedCmd), XPos, YPos);
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