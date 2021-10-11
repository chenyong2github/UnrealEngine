// Copyright Epic Games, Inc. All Rights Reserved.

#include "PhysicsEffect.h"
#include "Engine/World.h"
#include "RewindData.h"
#include "EngineUtils.h"
#include "NetworkPredictionDebug.h"
#include "NetworkPredictionCVars.h"
#include "Async/NetworkPredictionAsyncWorldManager.h"
#include "Async/NetworkPredictionAsyncID.h"
#include "Async/NetworkPredictionAsyncModelDefRegistry.h"
#include "Async/NetworkPredictionAsyncProxyImpl.h"
#include "Components/PrimitiveComponent.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "DrawDebugHelpers.h"

NP_DEVCVAR_INT(PhysicsEffectsDefaultFrameDelay, 5, "np2.pe.FrameDelay", "Default Frame delay for Physics Effects");

struct FPhysicsEffectSimulation
{
	static void Tick_Internal(FNetworkPredictionAsyncTickContext& Context, UE_NP::FNetworkPredictionAsyncID ID, FPhysicsEffectCmd& InputCmd, FPhysicsEffectLocalState& LocalState, FPhysicsEffectNetState& NetState);
};

struct FPhysicsEffectsAsyncModelDef : public UE_NP::FNetworkPredictionAsyncModelDef
{
	NP_ASYNC_MODEL_BODY()

	using InputCmdType = FPhysicsEffectCmd;
	using NetStateType = FPhysicsEffectNetState;
	using LocalStateType = FPhysicsEffectLocalState;
	using SimulationTickType = FPhysicsEffectSimulation;

	static const TCHAR* GetName() { return TEXT("PhysicsEffects"); }
	static constexpr int32 GetSortPriority() { return 1; }
};

NP_ASYNC_MODEL_REGISTER(FPhysicsEffectsAsyncModelDef);

void FPhysicsEffectSimulation::Tick_Internal(FNetworkPredictionAsyncTickContext& Context, UE_NP::FNetworkPredictionAsyncID ID, FPhysicsEffectCmd& InputCmd, FPhysicsEffectLocalState& LocalState, FPhysicsEffectNetState& NetState)
{
	UWorld* World = Context.World;

	// Copy InputCmd PendingEffect if it starts after the "active" one
	if (InputCmd.PendingEffect.StartFrame > NetState.ActiveEffect.StartFrame)
	{
		NetState.ActiveEffect = InputCmd.PendingEffect;
	}

	// For now, require a static (non networked) proxy in the LocalState to base everything around
	// But we'll want to support "from X location" and not require that the "owner" of this NP system is even a physics object.
	// For example maybe we put a PhysicsEffect sim on the GameState for world/global impulses
	if (!LocalState.Proxy)
	{
		return;
	}

	auto* PT = LocalState.Proxy->GetPhysicsThreadAPI();
	if (!PT)
	{
		return;
	}

	if (NetState.ActiveEffect.StartFrame != INDEX_NONE)
	{
		if (Context.SimulationFrame >= NetState.ActiveEffect.StartFrame)
		{
			if (Context.SimulationFrame <= NetState.ActiveEffect.EndFrame || NetState.ActiveEffect.EndFrame == INDEX_NONE)
			{
				// The effect is active
				NetState.LastPlayedEffectTypeID = NetState.ActiveEffect.TypeID;
				NetState.LastPlayedSimFrame = Context.SimulationFrame;

				// Only sphere overlap / impulse for now
				const float MinDistSq = NetState.ActiveEffect.QueryData.X * NetState.ActiveEffect.QueryData.X;

				const FTransform ToWorldTransform(PT->R(), PT->X());
				const FTransform QueryWorldTransform = NetState.ActiveEffect.QueryRelativeTransform * ToWorldTransform;
				const FVector Origin = QueryWorldTransform.GetTranslation();

				//DrawDebugSphere(World, Origin, 50.f, 10, FColor::Green, false, 15.f); // Not thread safe but sorta works

				FCollisionShape Shape = FCollisionShape::MakeSphere(NetState.ActiveEffect.QueryData.X * 1.25f); // Extra distance + manual distance check for now

				TArray<FOverlapResult> Overlaps;
				World->OverlapMultiByChannel(Overlaps, Origin, FQuat::Identity, ECollisionChannel::ECC_PhysicsBody, Shape, LocalState.QueryParams);

				//UE_LOG(LogTemp, Warning, TEXT("[%s][%d] Applying Impulse %f, %f"), World->GetNetMode() == NM_Client ? TEXT("C") : TEXT("S"), Context.SimulationFrame, NetState.ActiveEffect.QueryData.X, NetState.ActiveEffect.ResponseMagnitude.X);
				for (FOverlapResult& Result : Overlaps)
				{
					//UE_LOG(LogTemp, Warning, TEXT("   Hit: %s"), *GetPathNameSafe(Result.GetActor()));
					UPrimitiveComponent* HitPrim = Result.GetComponent();
					
					if (HitPrim && FPhysicsInterface::IsValid(HitPrim->BodyInstance.ActorHandle))
					{
						if (auto HitPT = HitPrim->BodyInstance.ActorHandle->GetPhysicsThreadAPI())
						{
							const FVector Delta = (HitPT->X() - Origin);
							const float DistSq = Delta.SizeSquared();
							if (DistSq <= MinDistSq)
							{
								const FVector ImpulseNorm = DistSq > 0.f ? Delta.GetUnsafeNormal() : FVector(0.f, 0.f, 1.f);
								const FVector Fudge = FVector(0.f, 0.f, NetState.ActiveEffect.ResponseMagnitude.Z);
								HitPT->SetV(HitPT->V() + (ImpulseNorm * NetState.ActiveEffect.ResponseMagnitude.X) + Fudge);
							}							
						}
					}
				}

			}
			else
			{
				// Effect is finished, reset the state
				NetState.ActiveEffect = FPhysicsEffectDef();
			}
		}
	}
}

void IPhysicsEffectsInterface::InitializePhysicsEffects(FPhysicsEffectLocalState&& LocalState)
{
	FNetworkPredictionAsyncProxy& NetworkPredictionProxy = GetNetworkPredictionAsyncProxy();
	FPhysicsEffectsExternalState& State = GetPhysicsEffectsExternalState();

	NetworkPredictionProxy.RegisterSim<FPhysicsEffectsAsyncModelDef>(MoveTemp(LocalState), FPhysicsEffectNetState(), &State.PendingEffectCmd, &State.PhysicsEffectState);
}

void IPhysicsEffectsInterface::SyncPhysicsEffects()
{
	FNetworkPredictionAsyncProxy& NetworkPredictionProxy = GetNetworkPredictionAsyncProxy();
	FPhysicsEffectsExternalState& State = GetPhysicsEffectsExternalState();

	// This is a crude way to determine when a PhysicsEffect has been played and its results have first been detected on the GT
	// This covers both GT->PT latency and Remote client replication. This lets us tie cosmetic effects to Physics Effects.
	// The "NetSimCue" system in Network Prediction can replace this with something more robust.
	const int32 StartFrame = State.PhysicsEffectState.ActiveEffect.StartFrame;
	if (StartFrame != INDEX_NONE && StartFrame != State.CachedWindUpSimFrame)
	{
		UpdatePendingWindUp(StartFrame, State.PhysicsEffectState.ActiveEffect.TypeID);
	}

	if (State.PhysicsEffectState.LastPlayedSimFrame != State.CachedLastPlayedSimFrame)
	{
		State.CachedLastPlayedSimFrame = State.PhysicsEffectState.LastPlayedSimFrame;
		OnPhysicsEffectExecuted(State.PhysicsEffectState.LastPlayedEffectTypeID);
	}
}

void IPhysicsEffectsInterface::UpdatePendingWindUp(int32 Frame, uint8 TypeID)
{
	FPhysicsEffectsExternalState& State = GetPhysicsEffectsExternalState();

	if (Frame > State.CachedWindUpSimFrame)
	{
		State.CachedWindUpSimFrame = Frame;
		OnPhysicsEffectWindUp(TypeID);
	}
}

void IPhysicsEffectsInterface::ApplyPhysicsEffectDef(FPhysicsEffectDef&& Effect, const FPhysicsEffectParams& Params)
{
	FNetworkPredictionAsyncProxy& NetworkPredictionProxy = GetNetworkPredictionAsyncProxy();
	FPhysicsEffectsExternalState& State = GetPhysicsEffectsExternalState();

	const int32 DelayFrames = Params.DelaySeconds > 0.f ? (Params.DelaySeconds * 30.f) : 0;
	Effect.StartFrame = NetworkPredictionProxy.GetNextSimFrame() + PhysicsEffectsDefaultFrameDelay + DelayFrames;
	
	if (Params.DurationSeconds >= 0.f)
	{
		Effect.EndFrame = Effect.StartFrame + (Params.DurationSeconds * 30.f); // Fixme: what is fast way to access this without having to cache it off locally/per instance (it can't change at runtime but is config var)
	}
	else
	{
		Effect.EndFrame = -1;
	}

	if (IsController())
	{
		State.PendingEffectCmd.PendingEffect = MoveTemp(Effect);
	}
	else
	{
		NetworkPredictionProxy.ModifyNetState<FPhysicsEffectsAsyncModelDef>([CapturedEffect = MoveTemp(Effect)](FPhysicsEffectNetState& NetState)
		{
			NetState.ActiveEffect = CapturedEffect;
		});
	}

	UpdatePendingWindUp(Effect.StartFrame, Effect.TypeID);
}

FPhysicsEffectNetState IPhysicsEffectsInterface::Debug_ReadActivePhysicsEffect() const
{
	const FPhysicsEffectsExternalState& State = const_cast<IPhysicsEffectsInterface*>(this)->GetPhysicsEffectsExternalState();
	return State.PhysicsEffectState;
}

int32 IPhysicsEffectsInterface::Debug_GetLatestSimFrame() const
{
	FNetworkPredictionAsyncProxy& NetworkPredictionProxy = const_cast<IPhysicsEffectsInterface*>(this)->GetNetworkPredictionAsyncProxy();
	return NetworkPredictionProxy.GetLatestOutputSimFrame();
}

uint8 IPhysicsEffectsInterface::GetActivePhysicsEffectTypeID() const
{
	const FNetworkPredictionAsyncProxy& NetworkPredictionProxy = const_cast<IPhysicsEffectsInterface*>(this)->GetNetworkPredictionAsyncProxy();
	const FPhysicsEffectsExternalState& State = const_cast<IPhysicsEffectsInterface*>(this)->GetPhysicsEffectsExternalState();

	const int32 StartFrame = State.PhysicsEffectState.ActiveEffect.StartFrame;
	const int32 EndFrame = State.PhysicsEffectState.ActiveEffect.EndFrame;
	const int32 Frame = NetworkPredictionProxy.GetLatestOutputSimFrame();

	if (StartFrame != INDEX_NONE && Frame >= StartFrame && (Frame <= EndFrame || EndFrame == INDEX_NONE))
	{
		return State.PhysicsEffectState.LastPlayedEffectTypeID;
	}
	return 0;
}


void IPhysicsEffectsInterface::ClearPhysicsEffects()
{
	FNetworkPredictionAsyncProxy& NetworkPredictionProxy = GetNetworkPredictionAsyncProxy();
	FPhysicsEffectsExternalState& State = GetPhysicsEffectsExternalState();

	if (IsController())
	{
		// Need to always "advance" StartFrame on the InputCmd to get it to be picked up in Tick_Internal.
		State.PendingEffectCmd.PendingEffect.StartFrame = FMath::Max(State.PendingEffectCmd.PendingEffect.StartFrame+1, NetworkPredictionProxy.GetNextSimFrame());
		State.PendingEffectCmd.PendingEffect.EndFrame = 0;
	}
	else
	{
		NetworkPredictionProxy.ModifyNetState<FPhysicsEffectsAsyncModelDef>([](FPhysicsEffectNetState& NetState)
		{
			NetState.ActiveEffect.EndFrame = 0;
		});
	}
}

void IPhysicsEffectsInterface::ApplyImpulse(float Radius, float Magnitude, FTransform RelativeOffset, float FudgeVelocityZ, const FPhysicsEffectParams& Params)
{
	FPhysicsEffectDef NewEffect;

	NewEffect.QueryType = EPhysicsEffectQueryType::SphereOverlap;
	NewEffect.QueryData = FVector(Radius, 0.f, 0.f);
	NewEffect.QueryRelativeTransform = RelativeOffset;
	
	NewEffect.ResponseType = EPhysicsEffectResponseType::Impulse;
	NewEffect.ResponseMagnitude = FVector(Magnitude, 0.f, FudgeVelocityZ);

	NewEffect.TypeID = Params.TypeID;

	ApplyPhysicsEffectDef(MoveTemp(NewEffect), Params);
}

