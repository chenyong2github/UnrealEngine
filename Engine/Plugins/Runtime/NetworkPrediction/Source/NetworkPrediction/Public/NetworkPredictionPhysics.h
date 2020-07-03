// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "Engine/NetSerialization.h"
#include "PhysicsInterfaceDeclaresCore.h"
#include "Chaos/Core.h"
#include "Chaos/ParticleHandle.h"
#include "RewindData.h"
#include "NetworkPredictionCVars.h"
#include "NetworkPredictionLog.h"
#include "NetworkPredictionModelDef.h"
#include "Containers/StringFwd.h"
#include "NetworkPredictionTrace.h"

namespace NetworkPredictionPhysicsCvars
{
	NETSIM_DEVCVAR_SHIPCONST_INT(FullPrecision, 1, "np.Physics.FullPrecision", "Replicatre physics state with full precision. Not to be toggled during gameplay.");
	NETSIM_DEVCVAR_SHIPCONST_INT(DebugPositionCorrections, 0, "np.Physics.DebugPositionCorrections", "Prints position history when correcting physics X");

	NETSIM_DEVCVAR_SHIPCONST_FLOAT(ToleranceX, 1.0, "np.Physics.Tolerance.X", "Absolute tolerance for position");
	NETSIM_DEVCVAR_SHIPCONST_FLOAT(ToleranceR, 0.1, "np.Physics.Tolerance.R", "Normalized error tolerance between rotation (0..1)");
	NETSIM_DEVCVAR_SHIPCONST_FLOAT(ToleranceV, 1.0, "np.Physics.Tolerance.V", "Absolute error tolerance for velocity ");
	NETSIM_DEVCVAR_SHIPCONST_FLOAT(ToleranceW, 1.0, "np.Physics.Tolerance.W", "Absolute error tolerance for rotational velocity ");
};

// ------------------------------------------------------------------------------------------------------
// Actual physics state. More of these could be created to include more state or change the serialization
// ------------------------------------------------------------------------------------------------------
struct NETWORKPREDICTION_API FNetworkPredictionPhysicsState
{
	Chaos::FVec3 Location;
	Chaos::FRotation3 Rotation;
	Chaos::FVec3 LinearVelocity;
	Chaos::FVec3 AngularVelocity;

	static void NetSend(const FNetSerializeParams& P, FPhysicsActorHandle Handle)
	{
		if (NetworkPredictionPhysicsCvars::FullPrecision())
		{
			P.Ar << const_cast<Chaos::FVec3&>(Handle->X());
			P.Ar << const_cast<Chaos::FRotation3&>(Handle->R());

			Chaos::TKinematicGeometryParticle<float, 3>* Kinematic = Handle->CastToKinematicParticle();
			if (npEnsure(Kinematic))
			{
				P.Ar << const_cast<Chaos::FVec3&>(Kinematic->V());
				P.Ar << const_cast<Chaos::FVec3&>(Kinematic->W());
			}
		}
		else
		{
			bool bSuccess = true;
			
			SerializePackedVector<100, 30>(const_cast<Chaos::FVec3&>(Handle->X()), P.Ar);
			const_cast<Chaos::FRotation3&>(Handle->R()).NetSerialize(P.Ar, nullptr, bSuccess);
			
			Chaos::TKinematicGeometryParticle<float, 3>* Kinematic = Handle->CastToKinematicParticle();
			if (npEnsure(Kinematic))
			{
				SerializePackedVector<100, 30>(const_cast<Chaos::FVec3&>(Kinematic->V()), P.Ar);
				SerializePackedVector<100, 30>(const_cast<Chaos::FVec3&>(Kinematic->W()), P.Ar);
			}
		}
	}

	static void NetRecv(const FNetSerializeParams& P, FNetworkPredictionPhysicsState* RecvState)
	{
		npCheckSlow(RecvState);

		if (NetworkPredictionPhysicsCvars::FullPrecision())
		{
			P.Ar << RecvState->Location;
			P.Ar << RecvState->Rotation;
			P.Ar << RecvState->LinearVelocity;
			P.Ar << RecvState->AngularVelocity;
		}
		else
		{
			bool bSuccess = true;
			SerializePackedVector<100, 30>(RecvState->Location, P.Ar);
			RecvState->Rotation.NetSerialize(P.Ar, nullptr, bSuccess);
			SerializePackedVector<100, 30>(RecvState->LinearVelocity, P.Ar);
			SerializePackedVector<100, 30>(RecvState->AngularVelocity, P.Ar);
		}

		npEnsureSlow(!RecvState->ContainsNaN());
	}

	static bool ShouldReconcile(int32 PhysicsFrame, Chaos::FRewindData* RewindData, FPhysicsActorHandle Handle, FNetworkPredictionPhysicsState* RecvState)
	{
		auto CompareVector = [](const FVector& Local, const FVector& Authority, const float Tolerance, const TCHAR* DebugStr)
		{
			const FVector Delta = Local - Authority;
			if (Delta.SizeSquared() > (Tolerance * Tolerance))
			{
				//UE_NP_TRACE_SIM(0);
				UE_NP_TRACE_SYSTEM_FAULT("Physics Compare mismatch %s", DebugStr);
				UE_NP_TRACE_SYSTEM_FAULT("   Pred: %s", *Local.ToString());				
				UE_NP_TRACE_SYSTEM_FAULT("   Auth: %s", *Authority.ToString());
				UE_NP_TRACE_SYSTEM_FAULT("   Delta: %s (%.f)", *Delta.ToString(), Delta.Size());
				return true;
			}

			return false;
		};

		auto CompareQuat = [](const FQuat& Local, const FQuat& Authority, const float Tolerance, const TCHAR* DebugStr)
		{
			const float Error = FQuat::ErrorAutoNormalize(Local, Authority);
			if (Error > Tolerance)
			{
				//UE_NP_TRACE_SYSTEM_FAULT("Physics Compare mismatch %s", DebugStr);
				return true;
			}

			return false;
		};

		const Chaos::FGeometryParticleState LocalState = RewindData->GetPastStateAtFrame(*Handle, PhysicsFrame);

		if (CompareVector(LocalState.X(), RecvState->Location, NetworkPredictionPhysicsCvars::ToleranceX(), TEXT("Position")))
		{
			return true;
		}

		if (CompareVector(LocalState.V(), RecvState->LinearVelocity, NetworkPredictionPhysicsCvars::ToleranceV(), TEXT("LinearVelocity")))
		{
			return true;
		}

		if (CompareVector(LocalState.W(), RecvState->AngularVelocity, NetworkPredictionPhysicsCvars::ToleranceW(), TEXT("AngularVelocity")))
		{
			return true;
		}

		if (CompareQuat(LocalState.R(), RecvState->Rotation, NetworkPredictionPhysicsCvars::ToleranceR(), TEXT("Rotation")))
		{
			return true;
		}

		return false;
	}

	static void Interpolate(const FNetworkPredictionPhysicsState* From, const FNetworkPredictionPhysicsState* To, const float PCT, FNetworkPredictionPhysicsState* Out)
	{
		npCheckSlow(From);
		npCheckSlow(To);

		npEnsureMsgfSlow(!From->ContainsNaN(), TEXT("From interpolation state contains NaN"));
		npEnsureMsgfSlow(!To->ContainsNaN(), TEXT("To interpolation state contains NaN"));

		Out->Location = FMath::Lerp(From->Location, To->Location, PCT);
		Out->LinearVelocity = FMath::Lerp(From->LinearVelocity, To->LinearVelocity, PCT);
		Out->AngularVelocity = FMath::Lerp(From->AngularVelocity, To->AngularVelocity, PCT);

		Out->Rotation = FQuat::FastLerp(From->Rotation, To->Rotation, PCT);
		Out->Rotation.Normalize();
		
		npEnsureMsgfSlow(!Out->ContainsNaN(), TEXT("Out interpolation state contains NaN. %f"), PCT);
	}

	static void PerformRollback(FPhysicsActorHandle Handle, FNetworkPredictionPhysicsState* RecvState)
	{
		npCheckSlow(RecvState);

		Handle->SetX(RecvState->Location);
		Handle->SetR(RecvState->Rotation);

		if (Chaos::TKinematicGeometryParticle<float, 3>* Kinematic = Handle->CastToKinematicParticle())
		{
			Kinematic->SetV(RecvState->LinearVelocity);
			Kinematic->SetW(RecvState->AngularVelocity);
		}
	}

	// Interpolation related functions currently need to go through a UPrimitiveComponent
	static void FinalizeInterpolatedPhysics(UPrimitiveComponent* Driver, FPhysicsActorHandle Handle, FNetworkPredictionPhysicsState* InterpolatedState);
	static void BeginInterpolation(UPrimitiveComponent* Driver, FPhysicsActorHandle ActorHandle);
	static void EndInterpolation(UPrimitiveComponent* Driver, FPhysicsActorHandle ActorHandle);

	// Networked state to string
	static void ToString(FNetworkPredictionPhysicsState* RecvState, FAnsiStringBuilderBase& Builder)
	{
		ToStringInternal(RecvState->Location, RecvState->Rotation, RecvState->LinearVelocity, RecvState->AngularVelocity, Builder);
	}

	// Locally stored state to string
	static void ToString(int32 PhysicsFrame, Chaos::FRewindData* RewindData, FPhysicsActorHandle Handle, FAnsiStringBuilderBase& Builder)
	{
		const Chaos::FGeometryParticleState LocalState = RewindData->GetPastStateAtFrame(*Handle, PhysicsFrame);
		ToStringInternal(LocalState.X(), LocalState.R(), LocalState.V(), LocalState.W(), Builder);
	}

	// Current state to string
	static void ToString(FPhysicsActorHandle Handle, FAnsiStringBuilderBase& Builder)
	{
		Chaos::TKinematicGeometryParticle<float, 3>* Kinematic = Handle->CastToKinematicParticle();
		if (npEnsure(Kinematic))
		{
			ToStringInternal(Kinematic->X(), Kinematic->R(), Kinematic->V(), Kinematic->W(), Builder);
		}
	}

	bool ContainsNaN() const
	{
		return Location.ContainsNaN() || Rotation.ContainsNaN() || LinearVelocity.ContainsNaN() || AngularVelocity.ContainsNaN();
	}

private:

	static void ToStringInternal(const Chaos::FVec3& Location, const Chaos::FRotation3& Rotation, const Chaos::FVec3& LinearVelocity, const Chaos::FVec3& AngularVelocity, FAnsiStringBuilderBase& Builder)
	{
		Builder.Appendf("X: X=%.2f Y=%.2f Z=%.2f\n", Location.X, Location.Y, Location.Z);
		Builder.Appendf("R: X=%.2f Y=%.2f Z=%.2f W=%.2f\n", Rotation.X, Rotation.Y, Rotation.Z, Rotation.W);
		Builder.Appendf("V: X=%.2f Y=%.2f Z=%.2f\n", LinearVelocity.X, LinearVelocity.Y, LinearVelocity.Z);
		Builder.Appendf("W: X=%.2f Y=%.2f Z=%.2f\n", AngularVelocity.X, AngularVelocity.Y, AngularVelocity.Z);
	}
};

// ------------------------------------------------------------------------------------------------------
// Generic model def for physics actors with no backing simulation
// ------------------------------------------------------------------------------------------------------
struct FGenericPhysicsModelDef : FNetworkPredictionModelDef
{
	NP_MODEL_BODY();

	using PhysicsState = FNetworkPredictionPhysicsState;
	using Driver = UPrimitiveComponent; // this is required for interpolation mode to work, see FNetworkPredictionPhysicsState::FinalizeInterpolatedPhysics. Would like to not require it one day.

	static const TCHAR* GetName() { return TEXT("Generic Physics Actor"); }
	static constexpr int32 GetSortPriority() { return (int32)ENetworkPredictionSortPriority::Physics; }
};
