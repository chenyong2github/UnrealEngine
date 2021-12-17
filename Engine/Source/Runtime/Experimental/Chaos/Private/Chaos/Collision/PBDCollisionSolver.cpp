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