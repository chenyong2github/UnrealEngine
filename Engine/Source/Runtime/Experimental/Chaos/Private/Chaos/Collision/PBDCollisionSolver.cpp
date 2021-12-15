// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Collision/PBDCollisionSolver.h"

#include "Chaos/Collision/CollisionApplyType.h"
#include "Chaos/Collision/PBDCollisionConstraint.h"
#include "Chaos/Collision/SolverCollisionContainer.h"
#include "Chaos/CollisionResolution.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/CollisionResolutionUtil.h"
#include "Chaos/Evolution/SolverBodyContainer.h"
#include "Chaos/Particle/ParticleUtilities.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/PBDCollisionConstraintsContact.h"
#include "Chaos/Utilities.h"

//PRAGMA_DISABLE_OPTIMIZATION

// Set to 0 to use a linearized error calculation, and set to 1 to use a non-linear error calculation in collision detection. 
// In principle nonlinear is more accurate when large rotation corrections occur, but this is not too important for collisions because 
// when the bodies settle the corrections are small. The linearized version is significantly faster than the non-linear version because 
// the non-linear version requires a quaternion multiply and renormalization whereas the linear version is just a cross product.
#define CHAOS_NONLINEAR_COLLISIONS_ENABLED 0

DEFINE_LOG_CATEGORY(LogChaosCollision);

namespace Chaos
{
	namespace CVars
	{
		extern int32 Chaos_Collision_UseShockPropagation;

		bool bChaos_PBDCollisionSolver_Position_SolveEnabled = true;
		int32 Chaos_PBDCollisionSolver_Position_ShockPropagationIterations = 3;
		float Chaos_PBDCollisionSolver_Position_MinInvMassScale = 0.3f;
		int32 Chaos_PBDCollisionSolver_Position_ZeroFrictionIterations = 4;
		bool bChaos_PBDCollisionSolver_Position_NegativePushOutEnabled = true;
		float Chaos_PBDCollisionSolver_Position_StaticFrictionStiffness = 0.5f;
		float Chaos_PBDCollisionSolver_Position_StaticFrictionLerpRate = 0.1f;
		float Chaos_PBDCollisionSolver_Position_PositionSolverTolerance = 0.001f;		// cms
		float Chaos_PBDCollisionSolver_Position_RotationSolverTolerance = 0.001f;		// rads

		FAutoConsoleVariableRef CVarChaos_PBDCollisionSolver_Position_SolveEnabled(TEXT("p.Chaos.PBDCollisionSolver.Position.SolveEnabled"), bChaos_PBDCollisionSolver_Position_SolveEnabled, TEXT(""));
		FAutoConsoleVariableRef CVarChaos_PBDCollisionSolver_Position_UseShockPropagation(TEXT("p.Chaos.PBDCollisionSolver.Position.ShockPropagationIterations"), Chaos_PBDCollisionSolver_Position_ShockPropagationIterations, TEXT(""));
		FAutoConsoleVariableRef CVarChaos_PBDCollisionSolver_Position_MinInvMassScale(TEXT("p.Chaos.PBDCollisionSolver.Position.MinInvMassScale"), Chaos_PBDCollisionSolver_Position_MinInvMassScale, TEXT(""));
		FAutoConsoleVariableRef CVarChaos_PBDCollisionSolver_Position_ZeroFrictionIterations(TEXT("p.Chaos.PBDCollisionSolver.Position.ZeroFrictionIterations"), Chaos_PBDCollisionSolver_Position_ZeroFrictionIterations, TEXT(""));
		FAutoConsoleVariableRef CVarChaos_PBDCollisionSolver_Position_StaticFrictionStiffness(TEXT("p.Chaos.PBDCollisionSolver.Position.StaticFriction.Stiffness"), Chaos_PBDCollisionSolver_Position_StaticFrictionStiffness, TEXT(""));
		FAutoConsoleVariableRef CVarChaos_PBDCollisionSolver_Position_StaticFrictionLerpRate(TEXT("p.Chaos.PBDCollisionSolver.Position.StaticFriction.LerpRate"), Chaos_PBDCollisionSolver_Position_StaticFrictionLerpRate, TEXT(""));
		FAutoConsoleVariableRef CVarChaos_PBDCollisionSolver_Position_PositionSolverTolerance(TEXT("p.Chaos.PBDCollisionSolver.Position.PositionTolerance"), Chaos_PBDCollisionSolver_Position_PositionSolverTolerance, TEXT(""));
		FAutoConsoleVariableRef CVarChaos_PBDCollisionSolver_Position_RotationSolverTolerance(TEXT("p.Chaos.PBDCollisionSolver.Position.RotationTolerance"), Chaos_PBDCollisionSolver_Position_RotationSolverTolerance, TEXT(""));

		bool bChaos_PBDCollisionSolver_Velocity_SolveEnabled = true;
		int32 Chaos_PBDCollisionSolver_Velocity_ShockPropagationIterations = 1;
		// If this is the same as Chaos_PBDCollisionSolver_Position_MinInvMassScale and all velocity iterations have shockpropagation, we avoid recalculating constraiunt-space mass
		float Chaos_PBDCollisionSolver_Velocity_MinInvMassScale = Chaos_PBDCollisionSolver_Position_MinInvMassScale;
		bool bChaos_PBDCollisionSolver_Velocity_DynamicFrictionEnabled = true;
		bool bChaos_PBDCollisionSolver_Velocity_NegativeImpulseEnabled = true;
		bool bChaos_PBDCollisionSolver_Velocity_ImpulseClampEnabled = true;

		FAutoConsoleVariableRef CVarChaos_PBDCollisionSolver_Velocity_SolveEnabled(TEXT("p.Chaos.PBDCollisionSolver.Velocity.SolveEnabled"), bChaos_PBDCollisionSolver_Velocity_SolveEnabled, TEXT(""));
		FAutoConsoleVariableRef CVarChaos_PBDCollisionSolver_Velocity_UseShockPropagation(TEXT("p.Chaos.PBDCollisionSolver.Velocity.ShockPropagationIterations"), Chaos_PBDCollisionSolver_Velocity_ShockPropagationIterations, TEXT(""));
		FAutoConsoleVariableRef CVarChaos_PBDCollisionSolver_Velocity_MinInvMassScale(TEXT("p.Chaos.PBDCollisionSolver.Velocity.MinInvMassScale"), Chaos_PBDCollisionSolver_Velocity_MinInvMassScale, TEXT(""));
		FAutoConsoleVariableRef CVarChaos_PBDCollisionSolver_Velocity_DynamicFrictionEnabled(TEXT("p.Chaos.PBDCollisionSolver.Velocity.DynamicFrictionEnabled"), bChaos_PBDCollisionSolver_Velocity_DynamicFrictionEnabled, TEXT(""));
		FAutoConsoleVariableRef CVarChaos_PBDCollisionSolver_Velocity_NegativeImpulseEnabled(TEXT("p.Chaos.PBDCollisionSolver.Velocity.NegativeImpulseEnabled"), bChaos_PBDCollisionSolver_Velocity_NegativeImpulseEnabled, TEXT(""));
		FAutoConsoleVariableRef CVarChaos_PBDCollisionSolver_Velocity_ImpulseClampEnabled(TEXT("p.Chaos.PBDCollisionSolver.Velocity.ImpulseClampEnabled"), bChaos_PBDCollisionSolver_Velocity_ImpulseClampEnabled, TEXT(""));
	}
	using namespace CVars;

	inline FReal GetPositionInvMassScale(int32 It, int32 NumIts)
	{
		if (Chaos_PBDCollisionSolver_Position_ShockPropagationIterations > 0)
		{
			const int32 FirstShockIt = FMath::Max(NumIts - Chaos_PBDCollisionSolver_Position_ShockPropagationIterations, 0);
			const FReal Interpolant = FMath::Clamp(FReal(It - FirstShockIt + 1) / FReal(NumIts - FirstShockIt), FReal(0), FReal(1));
			return FMath::Lerp((FReal)1.0f, (FReal)Chaos_PBDCollisionSolver_Position_MinInvMassScale, Interpolant);
		}
		return 1.0f;
	}

	inline FReal GetVelocityInvMassScale(int32 It, int32 NumIts)
	{
		if (Chaos_PBDCollisionSolver_Velocity_ShockPropagationIterations > 0)
		{
			const int32 FirstShockIt = FMath::Max(NumIts - Chaos_PBDCollisionSolver_Velocity_ShockPropagationIterations, 0);
			const FReal Interpolant = FMath::Clamp(FReal(It - FirstShockIt + 1) / FReal(NumIts - FirstShockIt), FReal(0), FReal(1));
			return FMath::Lerp((FReal)1.0f, (FReal)Chaos_PBDCollisionSolver_Velocity_MinInvMassScale, Interpolant);
		}
		return 1.0f;
	}

	inline void CalculatePositionCorrectionWithoutFriction(
		const FReal Stiffness,
		const FReal ContactDeltaNormal,
		const FVec3& ContactNormal,
		const FReal ContactMassNormal,
		FVec3& InOutNetPushOut,
		FVec3& OutPushOut)
	{
		FVec3 PushOut = -(Stiffness * ContactDeltaNormal * ContactMassNormal) * ContactNormal;

		// The total pushout so far this sub-step
		// We allow negative incremental impulses, but not net negative impulses
		const FVec3 NetPushOut = InOutNetPushOut + PushOut;
		const FReal NetPushOutNormal = FVec3::DotProduct(NetPushOut, ContactNormal);
		if (NetPushOutNormal < 0)
		{
			PushOut = -InOutNetPushOut;
		}

		InOutNetPushOut += PushOut;
		OutPushOut = PushOut;
	}


	inline void CalculatePositionCorrectionWithFriction(
		const FReal Stiffness,
		const FVec3& ContactDelta,
		const FReal ContactDeltaNormal,
		const FVec3& ContactNormal,
		const FMatrix33& ContactMass,
		const FReal StaticFriction,
		const FReal DynamicFriction,
		FVec3& InOutNetPushOut,
		FVec3& OutPushOut,
		FReal& InOutStaticFrictionMax,
		bool& bOutInsideStaticFrictionCone)
	{
		// If static friction is enabled, calculate the correction to move the contact point back to its
		// original relative location on all axes.
		// @todo(chaos): this should be moved to the ManifoldPoint error calculation?
		FVec3 ModifiedContactError = -ContactDelta;
		const FReal FrictionStiffness = Chaos_PBDCollisionSolver_Position_StaticFrictionStiffness;
		if (FrictionStiffness < 1.0f)
		{
			const FVec3 ContactDeltaTangent = ContactDelta - ContactDeltaNormal * ContactNormal;
			ModifiedContactError = -ContactDeltaNormal * ContactNormal - FrictionStiffness * ContactDeltaTangent;
		}

		FVec3 PushOut = Stiffness * ContactMass * ModifiedContactError;

		// If we ended up with a negative normal pushout, disable friction
		FVec3 NetPushOut = InOutNetPushOut + PushOut;
		const FReal NetPushOutNormal = FVec3::DotProduct(NetPushOut, ContactNormal);
		bool bInsideStaticFrictionCone = true;
		if (NetPushOutNormal < FReal(SMALL_NUMBER))
		{
			bInsideStaticFrictionCone = false;
		}

		// Static friction limit: immediately increase maximum lateral correction, but smoothly decay maximum static friction limit. 
		// This is so that small variations in position (jitter) and therefore NetPushOutNormal don't cause static friction to slip
		// @todo(chaos): StaticFriction smoothing is iteration count depenendent - try to make it not so
		const FReal StaticFrictionLerpRate = Chaos_PBDCollisionSolver_Position_StaticFrictionLerpRate;
		const FReal StaticFrictionDest = FMath::Max(NetPushOutNormal, FReal(0));
		FReal StaticFrictionMax = FMath::Lerp(FMath::Max(InOutStaticFrictionMax, StaticFrictionDest), StaticFrictionDest, StaticFrictionLerpRate);

		// If we exceed the friction cone, stop adding frictional corrections (although
		// any already added lateral corrections will not be undone)
		// @todo(chaos): clamp to dynamic friction
		if (bInsideStaticFrictionCone)
		{
			const FReal MaxPushOutTangentSq = FMath::Square(StaticFriction * StaticFrictionMax);
			const FVec3 NetPushOutTangent = (NetPushOut - NetPushOutNormal * ContactNormal);
			const FReal NetPushOutTangentSq = NetPushOutTangent.SizeSquared();
			if (NetPushOutTangentSq > MaxPushOutTangentSq)
			{
				NetPushOut = NetPushOutNormal * ContactNormal + (StaticFriction * StaticFrictionMax) * NetPushOutTangent / FMath::Sqrt(NetPushOutTangentSq);
				PushOut = NetPushOut - InOutNetPushOut;
				bInsideStaticFrictionCone = false;
			}
		}

		// If we leave the friction cone, we will flll through into the non-friction impulse calculation
		// so do not export the results
		if (bInsideStaticFrictionCone)
		{
			InOutNetPushOut = NetPushOut;
			OutPushOut = PushOut;
		}
		else
		{
			OutPushOut = FVec3(0);
		}

		InOutStaticFrictionMax = StaticFrictionMax;
		bOutInsideStaticFrictionCone = bInsideStaticFrictionCone;
	}

	inline void ApplyPositionCorrection(
		const FReal Stiffness,
		const FReal StaticFriction,
		const FReal DynamicFriction,
		const FVec3& ContactDelta,
		const FReal ContactDeltaNormal,
		FPBDCollisionSolverManifoldPoint& ManifoldPoint,
		FConstraintSolverBody& Body0,
		FConstraintSolverBody& Body1)
	{
		FVec3 PushOut = FVec3(0);

		// If we want to add static friction...
		// (note: we run a few iterations without friction by temporarily setting StaticFriction to 0)
		if ((StaticFriction > 0) && ManifoldPoint.bInsideStaticFrictionCone)
		{
			CalculatePositionCorrectionWithFriction(
				Stiffness,
				ContactDelta,
				ContactDeltaNormal,
				ManifoldPoint.WorldContactNormal,
				ManifoldPoint.WorldContactMass,
				StaticFriction,
				DynamicFriction,
				ManifoldPoint.NetPushOut,					// Out
				PushOut,									// Out
				ManifoldPoint.StaticFrictionMax,			// Out
				ManifoldPoint.bInsideStaticFrictionCone);	// Out
		}
		else
		{
			CalculatePositionCorrectionWithoutFriction(
				Stiffness,
				ContactDeltaNormal,
				ManifoldPoint.WorldContactNormal,
				ManifoldPoint.WorldContactMassNormal,
				ManifoldPoint.NetPushOut,					// Out
				PushOut);									// Out
		}

		// Update the particle state based on the pushout
		if (Body0.IsDynamic())
		{
			const FVec3 AngularPushOut = FVec3::CrossProduct(ManifoldPoint.WorldContactPosition - Body0.P(), PushOut);
			const FVec3 DX0 = Body0.InvM() * PushOut;
			const FVec3 DR0 = Body0.InvI() * AngularPushOut;
			//if (!DX0.IsNearlyZero(Chaos_PBDCollisionSolver_Position_PositionSolverTolerance))
			{
				Body0.ApplyPositionDelta(DX0);
			}
			//if (!DR0.IsNearlyZero(Chaos_PBDCollisionSolver_Position_RotationSolverTolerance))
			{
				Body0.ApplyRotationDelta(DR0);
			}
		}
		if (Body1.IsDynamic())
		{
			const FVec3 AngularPushOut = FVec3::CrossProduct(ManifoldPoint.WorldContactPosition - Body1.P(), PushOut);
			const FVec3 DX1 = -(Body1.InvM() * PushOut);
			const FVec3 DR1 = -(Body1.InvI() * AngularPushOut);
			//if (!DX1.IsNearlyZero(Chaos_PBDCollisionSolver_Position_PositionSolverTolerance))
			{
				Body1.ApplyPositionDelta(DX1);
			}
			//if (!DR1.IsNearlyZero(Chaos_PBDCollisionSolver_Position_RotationSolverTolerance))
			{
				Body1.ApplyRotationDelta(DR1);
			}
		}
	}

	inline void CalculateVelocityCorrectionImpulse(
		const FReal Stiffness,
		const FReal Dt,
		const FReal DynamicFriction,
		const FVec3& ContactNormal,
		const FMatrix33& ContactMass,
		const FReal ContactMassNormal,
		const FVec3& ContactVelocityDelta,
		const FReal ContactVelocityDeltaNormal,
		const FVec3& NetPushOut,
		FVec3& InOutNetImpulse,
		FVec3& OutImpulse)
	{
		FVec3 Impulse = FVec3(0);

		if ((ContactVelocityDeltaNormal > FReal(0)) && !bChaos_PBDCollisionSolver_Velocity_NegativeImpulseEnabled)
		{
			return;
		}

		// Tangential velocity (dynamic friction)
		const bool bApplyFriction = (DynamicFriction > 0) && (Dt > 0);
		if (bApplyFriction)
		{
			Impulse = -Stiffness * (ContactMass * ContactVelocityDelta);
		}
		else
		{
			Impulse = -(Stiffness * ContactMassNormal) * ContactVelocityDelta;
		}

		// Clamp the total impulse to be positive along the normal. We can apply negative velocity correction, 
		// but only to correct the velocity that was added by pushout, or in this velocity solve step.
		if (bChaos_PBDCollisionSolver_Velocity_ImpulseClampEnabled && (Dt > 0))
		{
			// @todo(chaos): cache max negative impulse
			const FVec3 NetImpulse = InOutNetImpulse + Impulse;
			const FReal PushOutImpulseNormal = FMath::Max(0.0f, FVec3::DotProduct(NetPushOut, ContactNormal) / Dt);
			const FReal NetImpulseNormal = FVec3::DotProduct(NetImpulse, ContactNormal);
			if (NetImpulseNormal < -PushOutImpulseNormal)
			{
				// We are trying to apply a negative impulse larger than one to counteract the effective pushout impulse
				// so clamp the net impulse to be equal to minus the pushout impulse along the normal.
				// NOTE: NetImpulseNormal is negative here
				//const FVec3 NewNetImpulse = NetImpulse - NetImpulseNormal * ContactNormal - PushOutImpulseNormal * ContactNormal;
				//const FVec3 NewImpulse = NewNetImpulse - InOutNetImpulse;
				//Impulse = InOutNetImpulse + Impulse - NetImpulseNormal * ContactNormal - PushOutImpulseNormal * ContactNormal - InOutNetImpulse;
				Impulse = Impulse - (NetImpulseNormal + PushOutImpulseNormal) * ContactNormal;
			}
		}

		OutImpulse = Impulse;
		InOutNetImpulse += Impulse;
	}

	inline void ApplyVelocityCorrection(
		const FReal Stiffness,
		const FReal Dt,
		const FReal DynamicFriction,
		const FVec3& ContactVelocityDelta,
		const FReal ContactVelocityDeltaNormal,
		FPBDCollisionSolverManifoldPoint& ManifoldPoint,
		FConstraintSolverBody& Body0,
		FConstraintSolverBody& Body1)
	{
		FVec3 Impulse = FVec3(0);

		CalculateVelocityCorrectionImpulse(
			Stiffness,
			Dt,
			DynamicFriction,
			ManifoldPoint.WorldContactNormal,
			ManifoldPoint.WorldContactMass,
			ManifoldPoint.WorldContactMassNormal,
			ContactVelocityDelta,
			ContactVelocityDeltaNormal,
			ManifoldPoint.NetPushOut,	// Out
			ManifoldPoint.NetImpulse,	// Out
			Impulse);					// Out

		// Calculate the velocity deltas from the impulse
		if (Body0.IsDynamic())
		{
			const FVec3 AngularImpulse = FVec3::CrossProduct(ManifoldPoint.WorldContactPosition - Body0.P(), Impulse);
			Body0.ApplyVelocityDelta(Body0.InvM() * Impulse, Body0.InvI() * AngularImpulse);
		}
		if (Body1.IsDynamic())
		{
			const FVec3 AngularImpulse = FVec3::CrossProduct(ManifoldPoint.WorldContactPosition - Body1.P(), Impulse);
			Body1.ApplyVelocityDelta(Body1.InvM() * -Impulse, Body1.InvI() * -AngularImpulse);
		}
	}

	void FPBDCollisionSolverManifoldPoint::InitContact(
		const FConstraintSolverBody& Body0,
		const FConstraintSolverBody& Body1,
		const FVec3& InWorldAnchorPoint0,
		const FVec3& InWorldAnchorPoint1,
		const FVec3& InWorldContactNormal)
	{
		NetPushOut = FVec3(0);
		NetImpulse = FVec3(0);
		UpdateContact(Body0, Body1, InWorldAnchorPoint0, InWorldAnchorPoint1, InWorldContactNormal);
		UpdateMass(Body0, Body1);
	}

	void FPBDCollisionSolverManifoldPoint::InitMaterial(
		const FConstraintSolverBody& Body0,
		const FConstraintSolverBody& Body1,
		const FReal InRestitution,
		const FReal InRestitutionVelocityThreshold,
		const bool bInEnableStaticFriction,
		const FReal InStaticFrictionMax)
	{
		StaticFrictionMax = InStaticFrictionMax;
		bInsideStaticFrictionCone = bInEnableStaticFriction;
		WorldContactVelocityTargetNormal = FReal(0);

		if (InRestitution > FReal(0))
		{
			const FVec3 ContactVelocity = CalculateContactVelocity(Body0, Body1);
			const FReal ContactVelocityNormal = FVec3::DotProduct(ContactVelocity, WorldContactNormal);
			if (ContactVelocityNormal < -InRestitutionVelocityThreshold)
			{
				WorldContactVelocityTargetNormal = -InRestitution * ContactVelocityNormal;
			}
		}

	}

	inline void FPBDCollisionSolverManifoldPoint::UpdateContact(
		const FConstraintSolverBody& Body0, 
		const FConstraintSolverBody& Body1,
		const FVec3& InWorldAnchorPoint0,
		const FVec3& InWorldAnchorPoint1, 
		const FVec3& InWorldContactNormal)
	{
		// The world-space point where we apply impulses/corrections (same world-space point for momentum conservation)
		WorldContactPosition = FReal(0.5) * (InWorldAnchorPoint0 + InWorldAnchorPoint1);

		// The world-space contact normal
		WorldContactNormal = InWorldContactNormal;

		// The contact point error we are trying to correct in this solver
		WorldContactDelta = InWorldAnchorPoint0 - InWorldAnchorPoint1;
	}

	inline void FPBDCollisionSolverManifoldPoint::UpdateMass(const FConstraintSolverBody& Body0, const FConstraintSolverBody& Body1)
	{
		const FMatrix33 ContactMassInv =
			(Body0.IsDynamic() ? Collisions::ComputeFactorMatrix3(WorldContactPosition - Body0.P(), Body0.InvI(), Body0.InvM()) : FMatrix33(0)) +
			(Body1.IsDynamic() ? Collisions::ComputeFactorMatrix3(WorldContactPosition - Body1.P(), Body1.InvI(), Body1.InvM()) : FMatrix33(0));
		const FMatrix33 ContactMass = ContactMassInv.Inverse();
		const FReal ContactMassInvNormal = FVec3::DotProduct(WorldContactNormal, Utilities::Multiply(ContactMassInv, WorldContactNormal));
		const FReal ContactMassNormal = (ContactMassInvNormal > FReal(SMALL_NUMBER)) ? FReal(1) / ContactMassInvNormal : FReal(0);
		WorldContactMass = ContactMass;
		WorldContactMassNormal = ContactMassNormal;
	}

	FVec3 FPBDCollisionSolverManifoldPoint::CalculateContactVelocity(const FConstraintSolverBody& Body0, const FConstraintSolverBody& Body1) const
	{
		const FVec3 ContactVelocity0 = Body0.V() + FVec3::CrossProduct(Body0.W(), WorldContactPosition - Body0.P());
		const FVec3 ContactVelocity1 = Body1.V() + FVec3::CrossProduct(Body1.W(), WorldContactPosition - Body1.P());
		return ContactVelocity0 - ContactVelocity1;
	}

	inline void FPBDCollisionSolverManifoldPoint::CalculateContactPositionError(const FConstraintSolverBody& Body0, const FConstraintSolverBody& Body1, const FReal MaxPushOut, FVec3& OutContactDelta, FReal& OutContactDeltaNormal) const
	{
		const FVec3 WorldRelativeContactPosition0 = WorldContactPosition - Body0.P();
		const FVec3 WorldRelativeContactPosition1 = WorldContactPosition - Body1.P();
#if CHAOS_NONLINEAR_COLLISIONS_ENABLED
		// Non-linear version: calculate the contact delta after we have converted the current positional impulses into position and rotation corrections.
		// We could precalculate and store the LocalContactPositions if we really want to use this nonlinear version
		const FVec3 LocalContactPosition0 = Body0.Q().Inverse() * WorldRelativeContactPosition0;
		const FVec3 LocalContactPosition1 = Body1.Q().Inverse() * WorldRelativeContactPosition1;
		OutContactDelta = (Body0.CorrectedP() + Body0.CorrectedQ() * LocalContactPosition0) - (Body1.CorrectedP() + Body1.CorrectedQ() * LocalContactPosition1);
#else
		// Linear version: calculate the contact delta assuming linear motion after applying a positional impulse at the contact point. There will be an error that depends on the size of the rotation.
		const FVec3 ContactDelta0 = Body0.DP() + FVec3::CrossProduct(Body0.DQ(), WorldRelativeContactPosition0);
		const FVec3 ContactDelta1 = Body1.DP() + FVec3::CrossProduct(Body1.DQ(), WorldRelativeContactPosition1);
		OutContactDelta = WorldContactDelta + ContactDelta0 - ContactDelta1;
#endif
		OutContactDeltaNormal = FVec3::DotProduct(OutContactDelta, WorldContactNormal);

		// NOTE: OutContactDeltaNormal is negative for penetration
		// NOTE: MaxPushOut == 0 disables the pushout limits
		if ((MaxPushOut > 0) && (OutContactDeltaNormal < -MaxPushOut))
		{
			const FReal ClampedContactDeltaNormal = -MaxPushOut;
			OutContactDelta += (ClampedContactDeltaNormal - OutContactDeltaNormal) * WorldContactNormal;
			OutContactDeltaNormal = ClampedContactDeltaNormal;
		}
	}

	inline void FPBDCollisionSolverManifoldPoint::CalculateContactVelocityError(const FConstraintSolverBody& Body0, const FConstraintSolverBody& Body1, const FReal DynamicFriction, const FReal Dt, FVec3& OutContactVelocityDelta, FReal& OutContactVelocityDeltaNormal) const
	{
		const FVec3 ContactVelocity0 = Body0.V() + FVec3::CrossProduct(Body0.W(), WorldContactPosition - Body0.P());
		const FVec3 ContactVelocity1 = Body1.V() + FVec3::CrossProduct(Body1.W(), WorldContactPosition - Body1.P());
		const FVec3 ContactVelocity = ContactVelocity0 - ContactVelocity1;
		const FReal ContactVelocityNormal = FVec3::DotProduct(ContactVelocity, WorldContactNormal);

		// Target normal velocity, including restitution
		const FReal ContactVelocityTargetNormal = WorldContactVelocityTargetNormal;

		// Add up the errors in the velocity (current velocity - desired velocity)
		OutContactVelocityDeltaNormal = (ContactVelocityNormal - WorldContactVelocityTargetNormal);
		OutContactVelocityDelta = (ContactVelocityNormal - ContactVelocityTargetNormal) * WorldContactNormal;

		const bool bApplyFriction = (DynamicFriction > 0) && (Dt > 0);
		if (bApplyFriction)
		{
			const FVec3 ContactVelocityTangential = ContactVelocity - ContactVelocityNormal * WorldContactNormal;
			const FReal ContactVelocityTangentialLen = ContactVelocityTangential.Size();
			if (ContactVelocityTangentialLen > SMALL_NUMBER)
			{
				// PushOut = ContactMass * DP, where DP is the contact positional correction
				// Friction force is proportional to the normal force, so friction velocity correction
				// is proprtional to normal velocity correction, or DVn = DPn/dt = PushOut.N / (ContactMass * dt);
				const FReal PushOutNormal = FVec3::DotProduct(NetPushOut, WorldContactNormal);
				const FReal DynamicFrictionVelocityError = PushOutNormal / (WorldContactMassNormal * Dt);
				if (DynamicFrictionVelocityError > SMALL_NUMBER)
				{
					const FReal ContactVelocityErrorTangential = FMath::Min(DynamicFriction * DynamicFrictionVelocityError, ContactVelocityTangentialLen);
					OutContactVelocityDelta += ContactVelocityTangential * (ContactVelocityErrorTangential / ContactVelocityTangentialLen);
				}
			}
		}
	}


	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////


	FPBDCollisionSolver::FPBDCollisionSolver()
		: State()
	{
	}

	void FPBDCollisionSolver::EnablePositionShockPropagation()
	{
		SetShockPropagationInvMassScale(Chaos_PBDCollisionSolver_Position_MinInvMassScale);
	}

	void FPBDCollisionSolver::EnableVelocityShockPropagation()
	{
		SetShockPropagationInvMassScale(Chaos_PBDCollisionSolver_Velocity_MinInvMassScale);
	}

	void FPBDCollisionSolver::DisableShockPropagation()
	{
		SetShockPropagationInvMassScale(FReal(1));
	}

	void FPBDCollisionSolver::SetShockPropagationInvMassScale(const FReal InvMassScale)
	{
		FConstraintSolverBody& Body0 = SolverBody0();
		FConstraintSolverBody& Body1 = SolverBody1();

		// Shock propagation decreases the inverse mass of bodies that are lower in the pile
		// of objects. This significantly improves stability of heaps and stacks. Height in the pile is indictaed by the "level". 
		// No need to set an inverse mass scale if the other body is kinematic (with inv mass of 0).
		// Bodies at the same level do not take part in shock propagation.
		if (Body0.IsDynamic() && Body1.IsDynamic() && (Body0.Level() != Body1.Level()))
		{
			// Set the inv mass scale of the "lower" body to make it heavier
			bool bInvMassUpdated = false;
			if (Body0.Level() < Body1.Level())
			{
				if (Body0.InvMScale() != InvMassScale)
				{
					Body0.SetInvMScale(InvMassScale);
					bInvMassUpdated = true;
				}
			}
			else
			{
				if (Body1.InvMScale() != InvMassScale)
				{
					Body1.SetInvMScale(InvMassScale);
					bInvMassUpdated = true;
				}
			}

			// If the masses changed, we need to rebuild the contact mass for each manifold point
			if (bInvMassUpdated)
			{
				for (int32 PointIndex = 0; PointIndex < State.NumManifoldPoints; ++PointIndex)
				{
					State.ManifoldPoints[PointIndex].UpdateMass(Body0, Body1);
				}
			}
		}
	}

	void FPBDCollisionSolver::InitContact(
		const int32 ManifoldPoiontIndex,
		const FVec3& InWorldAnchorPoint0,
		const FVec3& InWorldAnchorPoint1,
		const FVec3& InWorldContactNormal)
	{
		State.ManifoldPoints[ManifoldPoiontIndex].InitContact(State.SolverBodies[0], State.SolverBodies[1], InWorldAnchorPoint0, InWorldAnchorPoint1, InWorldContactNormal);
	}

	void FPBDCollisionSolver::InitMaterial(
		const int32 ManifoldPoiontIndex,
		const FReal InRestitution,
		const FReal InRestitutionVelocityThreshold,
		const bool bInEnableStaticFriction,
		const FReal InStaticFrictionMax)
	{
		State.ManifoldPoints[ManifoldPoiontIndex].InitMaterial(State.SolverBodies[0], State.SolverBodies[1], InRestitution, InRestitutionVelocityThreshold, bInEnableStaticFriction, InStaticFrictionMax);

		// Track if any points have restitution enabled. See SolveVelocity
		State.bHaveRestitution = State.bHaveRestitution || (State.ManifoldPoints[ManifoldPoiontIndex].WorldContactVelocityTargetNormal > 0);
	}

	void FPBDCollisionSolver::UpdateContact(
		const int32 ManifoldPoiontIndex,
		const FVec3& InWorldAnchorPoint0,
		const FVec3& InWorldAnchorPoint1,
		const FVec3& InWorldContactNormal)
	{
		State.ManifoldPoints[ManifoldPoiontIndex].UpdateContact(State.SolverBodies[0], State.SolverBodies[1], InWorldAnchorPoint0, InWorldAnchorPoint1, InWorldContactNormal);
	}

	bool FPBDCollisionSolver::SolvePosition(const FReal Dt, const FReal MaxPushOut, const bool bApplyStaticFriction)
	{
		if (!bChaos_PBDCollisionSolver_Position_SolveEnabled)
		{
			return false;
		}

		// SolverBody decorator used to add mass scaling
		FConstraintSolverBody& Body0 = SolverBody0();
		FConstraintSolverBody& Body1 = SolverBody1();

		// Check for solved
		if (State.bIsSolved)
		{
			if ((Body0.LastChangeEpoch() == State.BodyEpochs[0]) && (Body1.LastChangeEpoch() == State.BodyEpochs[1]))
			{
				// We did not apply a correction last iteration (within tolerance) and nothing else has moved the bodies
				return false;
			}
		}

		// The first few iterations have friction disabled. This allows us to solve the normal penetration at each contact point
		// without adding spurious lateral impulses which get cancelled out by other contacts. The spurious lateral impulses
		// cause problems when clipping to the friction cone on a per-contact basis.
		FReal StaticFriction = (bApplyStaticFriction) ? State.StaticFriction : FReal(0);
		FReal DynamicFriction = (bApplyStaticFriction) ? State.DynamicFriction : FReal(0);
		const FReal Stiffness = State.Stiffness;

		// Apply the position correction so that all contacts have zero separation
		for (int32 PointIndex = 0; PointIndex < State.NumManifoldPoints; ++PointIndex)
		{
			FPBDCollisionSolverManifoldPoint& SolverManifoldPoint = State.ManifoldPoints[PointIndex];

			FVec3 ContactDelta;
			FReal ContactDeltaNormal;
			SolverManifoldPoint.CalculateContactPositionError(Body0.SolverBody(), Body1.SolverBody(), MaxPushOut, ContactDelta, ContactDeltaNormal);

			const bool bProcessManifoldPoint = (ContactDeltaNormal < FReal(0)) || !SolverManifoldPoint.NetPushOut.IsNearlyZero();
			if (bProcessManifoldPoint)
			{
				ApplyPositionCorrection(
					Stiffness,
					StaticFriction,
					DynamicFriction,
					ContactDelta,
					ContactDeltaNormal,
					SolverManifoldPoint,
					Body0,
					Body1);
			}
		}

		// We are solved if we did not move the bodies within some tolerance
		// NOTE: we can't claim to be solved until we have done at least one friction iteration so we can't early out before friction has been applied.
		// @todo(chaos): better early-out system
		State.bIsSolved = bApplyStaticFriction && (Body0.LastChangeEpoch() == State.BodyEpochs[0]) && (Body1.LastChangeEpoch() == State.BodyEpochs[1]);
		State.BodyEpochs[0] = Body0.LastChangeEpoch();
		State.BodyEpochs[1] = Body1.LastChangeEpoch();
		State.NumPositionSolves = State.NumPositionSolves + 1;

		return !State.bIsSolved;
	}

	void FPBDCollisionSolver::SolveVelocityAverage(const FReal Dt)
	{
		FConstraintSolverBody& Body0 = SolverBody0();
		FConstraintSolverBody& Body1 = SolverBody1();

		// Generate a new contact point at the average of all the active contacts
		int32 NumActiveManifoldPoints = 0;
		FVec3 WorldContactPosition = FVec3(0);
		FVec3 WorldContactNormal = FVec3(0);
		FVec3 NetPushOut = FVec3(0);
		FReal WorldContactVelocityTargetNormal = FReal(0);
		for (int32 PointIndex = 0; PointIndex < State.NumManifoldPoints; ++PointIndex)
		{
			FPBDCollisionSolverManifoldPoint& SolverManifoldPoint = State.ManifoldPoints[PointIndex];
			if (!SolverManifoldPoint.NetPushOut.IsNearlyZero())
			{
				WorldContactPosition += SolverManifoldPoint.WorldContactPosition;
				WorldContactVelocityTargetNormal += SolverManifoldPoint.WorldContactVelocityTargetNormal;
				WorldContactNormal = SolverManifoldPoint.WorldContactNormal;	// Take last value - should all be similar
				++NumActiveManifoldPoints;
			}
		}

		// Solve for velocity at the new conatct point
		// We only do this if we have multiple active contacts
		if (NumActiveManifoldPoints > 1)
		{
			const FReal DynamicFriction = FReal(0);
			const FReal Scale = FReal(1) / FReal(NumActiveManifoldPoints);

			FPBDCollisionSolverManifoldPoint AverageManifoldPoint;
			AverageManifoldPoint.WorldContactPosition = WorldContactPosition * Scale;
			AverageManifoldPoint.WorldContactNormal = WorldContactNormal;

			const FMatrix33 ContactMassInv =
				(Body0.IsDynamic() ? Collisions::ComputeFactorMatrix3(AverageManifoldPoint.WorldContactPosition - Body0.P(), Body0.InvI(), Body0.InvM()) : FMatrix33(0)) +
				(Body1.IsDynamic() ? Collisions::ComputeFactorMatrix3(AverageManifoldPoint.WorldContactPosition - Body1.P(), Body1.InvI(), Body1.InvM()) : FMatrix33(0));
			const FReal ContactMassInvNormal = FVec3::DotProduct(AverageManifoldPoint.WorldContactNormal, Utilities::Multiply(ContactMassInv, AverageManifoldPoint.WorldContactNormal));
			const FReal ContactMassNormal = (ContactMassInvNormal > FReal(SMALL_NUMBER)) ? FReal(1) / ContactMassInvNormal : FReal(0);
			AverageManifoldPoint.WorldContactMass = FMatrix33(0);
			AverageManifoldPoint.WorldContactMassNormal = ContactMassNormal;

			AverageManifoldPoint.WorldContactDelta = FVec3(0);
			AverageManifoldPoint.WorldContactVelocityTargetNormal = WorldContactVelocityTargetNormal * Scale;
			AverageManifoldPoint.NetPushOut = FVec3(0);
			AverageManifoldPoint.NetImpulse = FVec3(0);
			AverageManifoldPoint.StaticFrictionMax = FReal(0);
			AverageManifoldPoint.bInsideStaticFrictionCone = false;

			FVec3 ContactVelocityDelta;
			FReal ContactVelocityDeltaNormal;
			AverageManifoldPoint.CalculateContactVelocityError(Body0, Body1, DynamicFriction, Dt, ContactVelocityDelta, ContactVelocityDeltaNormal);

			ApplyVelocityCorrection(
				State.Stiffness,
				Dt,
				DynamicFriction,
				ContactVelocityDelta,
				ContactVelocityDeltaNormal,
				AverageManifoldPoint,
				Body0,
				Body1);
		}
	}

	bool FPBDCollisionSolver::SolveVelocity(const FReal Dt, const bool bApplyDynamicFriction)
	{
		if (!bChaos_PBDCollisionSolver_Velocity_SolveEnabled)
		{
			return false;
		}

		// Apply restitution at the average contact point
		// This means we don't need to run as many iterations to get stable bouncing
		if (State.bHaveRestitution && (State.NumManifoldPoints > 1))
		{
			SolveVelocityAverage(Dt);
		}

		FConstraintSolverBody& Body0 = SolverBody0();
		FConstraintSolverBody& Body1 = SolverBody1();

		// NOTE: this dynamic friction implementation is iteration-count sensitive
		// @todo(chaos): fix iteration count dependence of dynamic friction
		const FReal DynamicFriction = (bApplyDynamicFriction && bChaos_PBDCollisionSolver_Velocity_DynamicFrictionEnabled) ? State.DynamicFriction : (FReal)0.0f;

		for (int32 PointIndex = 0; PointIndex < State.NumManifoldPoints; ++PointIndex)
		{
			FPBDCollisionSolverManifoldPoint& SolverManifoldPoint = State.ManifoldPoints[PointIndex];
			if (!SolverManifoldPoint.NetPushOut.IsNearlyZero())
			{
				FVec3 ContactVelocityDelta;
				FReal ContactVelocityDeltaNormal;
				SolverManifoldPoint.CalculateContactVelocityError(Body0, Body1, DynamicFriction, Dt, ContactVelocityDelta, ContactVelocityDeltaNormal);
				ApplyVelocityCorrection(
					State.Stiffness,
					Dt,
					DynamicFriction,
					ContactVelocityDelta,
					ContactVelocityDeltaNormal,
					SolverManifoldPoint,
					Body0,
					Body1);
			}
		}

		State.NumVelocitySolves = State.NumVelocitySolves + 1;

		// Early-out support for the velocity solve is not currently very important because we
		// only run one iteration in the velocity solve phase.
		// @todo(chaos): support early-out in velocity solve if necessary
		return true;
	}
}