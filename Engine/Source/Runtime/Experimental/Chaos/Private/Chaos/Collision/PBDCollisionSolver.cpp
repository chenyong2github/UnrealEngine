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
		const FVec3LP& InWorldContactNormal)
	{
		NetPushOutNormal = FRealLP(0);
		NetPushOutTangentU = FRealLP(0);
		NetPushOutTangentV = FRealLP(0);
		NetImpulseNormal = FRealLP(0);
		NetImpulseTangentU = FRealLP(0);
		NetImpulseTangentV = FRealLP(0);

		UpdateContact(Body0, Body1, InWorldAnchorPoint0, InWorldAnchorPoint1, InWorldContactNormal);

		UpdateMass(Body0, Body1);
	}

	void FPBDCollisionSolverManifoldPoint::InitMaterial(
		const FConstraintSolverBody& Body0,
		const FConstraintSolverBody& Body1,
		const FRealLP InRestitution,
		const FRealLP InRestitutionVelocityThreshold,
		const bool bInEnableStaticFriction,
		const FRealLP InFrictionNetPushOutNormal)
	{
		FrictionNetPushOutNormal = InFrictionNetPushOutNormal;
		bInsideStaticFrictionCone = bInEnableStaticFriction;
		WorldContactVelocityTargetNormal = FRealLP(0);

		if (InRestitution > FRealLP(0))
		{
			const FVec3LP ContactVelocity = CalculateContactVelocity(Body0, Body1);
			const FRealLP ContactVelocityNormal = FVec3LP::DotProduct(ContactVelocity, WorldContactNormal);
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
		const FVec3LP& InWorldContactNormal)
	{
		// The world-space point where we apply impulses/corrections (same world-space point for momentum conservation)
		const FVec3 WorldContactPosition = FReal(0.5) * (InWorldAnchorPoint0 + InWorldAnchorPoint1);
		RelativeContactPosition0 = FVec3LP(WorldContactPosition - Body0.P());
		RelativeContactPosition1 = FVec3LP(WorldContactPosition - Body1.P());

		// The world-space contact normal
		WorldContactNormal = InWorldContactNormal;

		// World-space contact tangents. We are treating the normal as the constraint-space Z axis
		// and the Tangent U and V as the constraint-space X and Y axes resepctively
		WorldContactTangentU = FVec3LP::CrossProduct(FVec3LP(0, 1, 0), InWorldContactNormal);
		if (!WorldContactTangentU.Normalize(FRealLP(KINDA_SMALL_NUMBER)))
		{
			WorldContactTangentU = FVec3LP::CrossProduct(FVec3LP(1, 0, 0), InWorldContactNormal);
			WorldContactTangentU = WorldContactTangentU.GetUnsafeNormal();
		}
		WorldContactTangentV = FVec3LP::CrossProduct(InWorldContactNormal, WorldContactTangentU);

		// The contact point error we are trying to correct in this solver
		const FVec3LP WorldContactDelta = InWorldAnchorPoint0 - InWorldAnchorPoint1;
		WorldContactDeltaNormal = FVec3LP::DotProduct(WorldContactDelta, WorldContactNormal);
		WorldContactDeltaTangent0 = FVec3LP::DotProduct(WorldContactDelta, WorldContactTangentU);
		WorldContactDeltaTangent1 = FVec3LP::DotProduct(WorldContactDelta, WorldContactTangentV);
	}

	inline void FPBDCollisionSolverManifoldPoint::UpdateMass(const FConstraintSolverBody& Body0, const FConstraintSolverBody& Body1)
	{
		FRealLP ContactMassInvNormal = FRealLP(0);
		FRealLP ContactMassInvTangentU = FRealLP(0);
		FRealLP ContactMassInvTangentV = FRealLP(0);

		// These are not used if not initialized below so no need to clear
		//WorldContactNormalAngular0 = FVec3LP(0);
		//WorldContactTangentUAngular0 = FVec3LP(0);
		//WorldContactTangentVAngular0 = FVec3LP(0);
		//WorldContactNormalAngular1 = FVec3LP(0);
		//WorldContactTangentUAngular1 = FVec3LP(0);
		//WorldContactTangentVAngular1 = FVec3LP(0);

		if (Body0.IsDynamic())
		{
			const FVec3LP R0xN = FVec3LP::CrossProduct(RelativeContactPosition0, WorldContactNormal);
			const FVec3LP R0xU = FVec3LP::CrossProduct(RelativeContactPosition0, WorldContactTangentU);
			const FVec3LP R0xV = FVec3LP::CrossProduct(RelativeContactPosition0, WorldContactTangentV);

			const FMatrix33LP InvI0 = Body0.InvI();

			WorldContactNormalAngular0 = InvI0 * R0xN;
			WorldContactTangentUAngular0 = InvI0 * R0xU;
			WorldContactTangentVAngular0 = InvI0 * R0xV;

			ContactMassInvNormal += FVec3LP::DotProduct(R0xN, WorldContactNormalAngular0) + Body0.InvM();
			ContactMassInvTangentU += FVec3LP::DotProduct(R0xU, WorldContactTangentUAngular0) + Body0.InvM();
			ContactMassInvTangentV += FVec3LP::DotProduct(R0xV, WorldContactTangentVAngular0) + Body0.InvM();
		}
		if (Body1.IsDynamic())
		{
			const FVec3LP R1xN = FVec3LP::CrossProduct(RelativeContactPosition1, WorldContactNormal);
			const FVec3LP R1xU = FVec3LP::CrossProduct(RelativeContactPosition1, WorldContactTangentU);
			const FVec3LP R1xV = FVec3LP::CrossProduct(RelativeContactPosition1, WorldContactTangentV);

			const FMatrix33LP InvI1 = Body1.InvI();

			WorldContactNormalAngular1 = InvI1 * R1xN;
			WorldContactTangentUAngular1 = InvI1 * R1xU;
			WorldContactTangentVAngular1 = InvI1 * R1xV;

			ContactMassInvNormal += FVec3LP::DotProduct(R1xN, WorldContactNormalAngular1) + Body1.InvM();
			ContactMassInvTangentU += FVec3LP::DotProduct(R1xU, WorldContactTangentUAngular1) + Body1.InvM();
			ContactMassInvTangentV += FVec3LP::DotProduct(R1xV, WorldContactTangentVAngular1) + Body1.InvM();
		}

		ContactMassNormal = (ContactMassInvNormal > FRealLP(SMALL_NUMBER)) ? FRealLP(1) / ContactMassInvNormal : FRealLP(0);
		ContactMassTangentU = (ContactMassInvTangentU > FRealLP(SMALL_NUMBER)) ? FRealLP(1) / ContactMassInvTangentU : FRealLP(0);
		ContactMassTangentV = (ContactMassInvTangentV > FRealLP(SMALL_NUMBER)) ? FRealLP(1) / ContactMassInvTangentV : FRealLP(0);
	}

	FVec3LP FPBDCollisionSolverManifoldPoint::CalculateContactVelocity(const FConstraintSolverBody& Body0, const FConstraintSolverBody& Body1) const
	{
		const FVec3LP ContactVelocity0 = Body0.V() + FVec3::CrossProduct(Body0.W(), RelativeContactPosition0);
		const FVec3LP ContactVelocity1 = Body1.V() + FVec3::CrossProduct(Body1.W(), RelativeContactPosition1);
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

	void FPBDCollisionSolver::SetShockPropagationInvMassScale(const FRealLP InvMassScale)
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
		State.ManifoldPoints[ManifoldPoiontIndex].InitContact(
			State.SolverBodies[0], 
			State.SolverBodies[1], 
			InWorldAnchorPoint0, 
			InWorldAnchorPoint1, 
			FVec3LP(InWorldContactNormal));
	}

	void FPBDCollisionSolver::InitMaterial(
		const int32 ManifoldPoiontIndex,
		const FReal InRestitution,
		const FReal InRestitutionVelocityThreshold,
		const bool bInEnableStaticFriction,
		const FReal InFrictionNetPushOutNormal)
	{
		State.ManifoldPoints[ManifoldPoiontIndex].InitMaterial(
			State.SolverBodies[0], 
			State.SolverBodies[1], 
			FRealLP(InRestitution), 
			FRealLP(InRestitutionVelocityThreshold),
			bInEnableStaticFriction, 
			FRealLP(InFrictionNetPushOutNormal));

		// Track if any points have restitution enabled. See SolveVelocity
		State.bHaveRestitution = State.bHaveRestitution || (State.ManifoldPoints[ManifoldPoiontIndex].WorldContactVelocityTargetNormal > 0);
	}

	void FPBDCollisionSolver::UpdateContact(
		const int32 ManifoldPoiontIndex,
		const FVec3& InWorldAnchorPoint0,
		const FVec3& InWorldAnchorPoint1,
		const FVec3& InWorldContactNormal)
	{
		State.ManifoldPoints[ManifoldPoiontIndex].UpdateContact(
			State.SolverBodies[0], 
			State.SolverBodies[1], 
			InWorldAnchorPoint0, 
			InWorldAnchorPoint1, 
			FVec3LP(InWorldContactNormal));
	}


	void FPBDCollisionSolver::SolveVelocityAverage(const FRealLP Dt)
	{
		FConstraintSolverBody& Body0 = SolverBody0();
		FConstraintSolverBody& Body1 = SolverBody1();

		// Generate a new contact point at the average of all the active contacts
		int32 NumActiveManifoldPoints = 0;
		FVec3LP RelativeContactPosition0 = FVec3(0);
		FVec3LP RelativeContactPosition1 = FVec3(0);
		FVec3LP WorldContactNormal = FVec3LP(0);
		FRealLP NetPushOutNormal = FRealLP(0);
		FRealLP WorldContactVelocityTargetNormal = FReal(0);
		for (int32 PointIndex = 0; PointIndex < State.NumManifoldPoints; ++PointIndex)
		{
			FPBDCollisionSolverManifoldPoint& SolverManifoldPoint = State.ManifoldPoints[PointIndex];
			if (!FMath::IsNearlyZero(SolverManifoldPoint.NetPushOutNormal))
			{
				RelativeContactPosition0 += SolverManifoldPoint.RelativeContactPosition0;
				RelativeContactPosition1 += SolverManifoldPoint.RelativeContactPosition1;
				WorldContactVelocityTargetNormal += SolverManifoldPoint.WorldContactVelocityTargetNormal;
				WorldContactNormal = SolverManifoldPoint.WorldContactNormal;	// Take last value - should all be similar
				NetPushOutNormal += SolverManifoldPoint.NetPushOutNormal;
				++NumActiveManifoldPoints;
			}
		}

		// Solve for velocity at the new conatct point
		// We only do this if we have multiple active contacts
		// NOTE: this average contact isn't really correct, especially when all contacts do not equally
		// contribute to the pushout (which is normal even for a simple box on a plane). E.g., the WorldContactVelocityTargetNormal
		// and NetPushOutNormal will not be right. The goal here though is just to get close to the correct result in frewer iterations
		// than we would have if we just solved the corners of the box in sequence.
		if (NumActiveManifoldPoints > 1)
		{
			const FRealLP DynamicFriction = FRealLP(0);
			const FRealLP Scale = FRealLP(1) / FRealLP(NumActiveManifoldPoints);

			FPBDCollisionSolverManifoldPoint AverageManifoldPoint;
			AverageManifoldPoint.RelativeContactPosition0 = RelativeContactPosition0 * Scale;
			AverageManifoldPoint.RelativeContactPosition1 = RelativeContactPosition1 * Scale;
			AverageManifoldPoint.WorldContactNormal = WorldContactNormal;
			AverageManifoldPoint.WorldContactVelocityTargetNormal = WorldContactVelocityTargetNormal * Scale;
			AverageManifoldPoint.NetPushOutNormal = NetPushOutNormal * Scale;

			const FMatrix33LP ContactMassInv =
				(Body0.IsDynamic() ? Collisions::ComputeFactorMatrix3(AverageManifoldPoint.RelativeContactPosition0, Body0.InvI(), Body0.InvM()) : FMatrix33LP(0)) +
				(Body1.IsDynamic() ? Collisions::ComputeFactorMatrix3(AverageManifoldPoint.RelativeContactPosition1, Body1.InvI(), Body1.InvM()) : FMatrix33LP(0));
			const FRealLP ContactMassInvNormal = FVec3LP::DotProduct(AverageManifoldPoint.WorldContactNormal, Utilities::Multiply(ContactMassInv, AverageManifoldPoint.WorldContactNormal));
			const FRealLP ContactMassNormal = (ContactMassInvNormal > FRealLP(SMALL_NUMBER)) ? FRealLP(1) / ContactMassInvNormal : FRealLP(0);
			AverageManifoldPoint.ContactMassNormal = ContactMassNormal;

			// @todo(chaos): All unused for the average contact solve - optimize
			AverageManifoldPoint.ContactMassTangentU = FRealLP(0);
			AverageManifoldPoint.ContactMassTangentV = FRealLP(0);
			AverageManifoldPoint.WorldContactTangentU = FVec3LP(0);
			AverageManifoldPoint.WorldContactTangentV = FVec3LP(0);
			AverageManifoldPoint.WorldContactDeltaNormal = FRealLP(0);
			AverageManifoldPoint.WorldContactDeltaTangent0 = FRealLP(0);
			AverageManifoldPoint.WorldContactDeltaTangent1 = FRealLP(0);
			AverageManifoldPoint.NetPushOutTangentU = FRealLP(0);
			AverageManifoldPoint.NetPushOutTangentV = FRealLP(0);
			AverageManifoldPoint.NetImpulseNormal = FRealLP(0);
			AverageManifoldPoint.NetImpulseTangentU = FRealLP(0);
			AverageManifoldPoint.NetImpulseTangentV = FRealLP(0);
			AverageManifoldPoint.FrictionNetPushOutNormal = FRealLP(0);
			AverageManifoldPoint.bInsideStaticFrictionCone = false;

			// @todo(chaos): don't need tangential velocity delta here since we are setting DynamicFriction to 0
			FRealLP ContactVelocityDeltaNormal, ContactVelocityDeltaTangentU, ContactVelocityDeltaTangentV;
			AverageManifoldPoint.CalculateContactVelocityError(Body0, Body1, DynamicFriction, Dt, ContactVelocityDeltaNormal, ContactVelocityDeltaTangentU, ContactVelocityDeltaTangentV);

			ApplyVelocityCorrection(
				State.Stiffness,
				Dt,
				DynamicFriction,
				ContactVelocityDeltaNormal,
				ContactVelocityDeltaTangentU,
				ContactVelocityDeltaTangentV,
				AverageManifoldPoint,
				Body0,
				Body1);
		}
	}

	bool FPBDCollisionSolver::SolveVelocity(const FRealLP Dt, const bool bApplyDynamicFriction)
	{
		// Apply restitution at the average contact point
		// This means we don't need to run as many iterations to get stable bouncing
		// It also helps with zero restitution to counter any velocioty added by the PBD solve
		if (State.NumManifoldPoints > 1)
		{
			SolveVelocityAverage(Dt);
		}

		FConstraintSolverBody& Body0 = SolverBody0();
		FConstraintSolverBody& Body1 = SolverBody1();

		// NOTE: this dynamic friction implementation is iteration-count sensitive
		// @todo(chaos): fix iteration count dependence of dynamic friction
		const FRealLP DynamicFriction = (bApplyDynamicFriction && bChaos_PBDCollisionSolver_Velocity_DynamicFrictionEnabled) ? State.DynamicFriction : FRealLP(0);

		for (int32 PointIndex = 0; PointIndex < State.NumManifoldPoints; ++PointIndex)
		{
			FPBDCollisionSolverManifoldPoint& SolverManifoldPoint = State.ManifoldPoints[PointIndex];
			if (!FMath::IsNearlyZero(SolverManifoldPoint.NetPushOutNormal))
			{
				FRealLP ContactVelocityDeltaNormal, ContactVelocityDeltaTangentU, ContactVelocityDeltaTangentV;
				SolverManifoldPoint.CalculateContactVelocityError(Body0, Body1, DynamicFriction, Dt, ContactVelocityDeltaNormal, ContactVelocityDeltaTangentU, ContactVelocityDeltaTangentV);

				ApplyVelocityCorrection(
					State.Stiffness,
					Dt,
					DynamicFriction,
					ContactVelocityDeltaNormal,
					ContactVelocityDeltaTangentU,
					ContactVelocityDeltaTangentV,
					SolverManifoldPoint,
					Body0,
					Body1);
			}
		}

		// Early-out support for the velocity solve is not currently very important because we
		// only run one iteration in the velocity solve phase.
		// @todo(chaos): support early-out in velocity solve if necessary
		return true;
	}
}