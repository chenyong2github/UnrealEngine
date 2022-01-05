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
		const FSolverVec3& InWorldContactNormal)
	{
		NetPushOutNormal = FSolverReal(0);
		NetPushOutTangentU = FSolverReal(0);
		NetPushOutTangentV = FSolverReal(0);
		NetImpulseNormal = FSolverReal(0);
		NetImpulseTangentU = FSolverReal(0);
		NetImpulseTangentV = FSolverReal(0);

		UpdateContact(Body0, Body1, InWorldAnchorPoint0, InWorldAnchorPoint1, InWorldContactNormal);

		UpdateMass(Body0, Body1);
	}

	void FPBDCollisionSolverManifoldPoint::InitMaterial(
		const FConstraintSolverBody& Body0,
		const FConstraintSolverBody& Body1,
		const FSolverReal InRestitution,
		const FSolverReal InRestitutionVelocityThreshold,
		const bool bInEnableStaticFriction,
		const FSolverReal InFrictionNetPushOutNormal)
	{
		FrictionNetPushOutNormal = InFrictionNetPushOutNormal;
		bInsideStaticFrictionCone = bInEnableStaticFriction;
		WorldContactVelocityTargetNormal = FSolverReal(0);

		if (InRestitution > FSolverReal(0))
		{
			const FSolverVec3 ContactVelocity = CalculateContactVelocity(Body0, Body1);
			const FSolverReal ContactVelocityNormal = FSolverVec3::DotProduct(ContactVelocity, WorldContactNormal);
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
		const FSolverVec3& InWorldContactNormal)
	{
		// The world-space point where we apply impulses/corrections (same world-space point for momentum conservation)
		const FVec3 WorldContactPosition = FReal(0.5) * (InWorldAnchorPoint0 + InWorldAnchorPoint1);
		RelativeContactPosition0 = FSolverVec3(WorldContactPosition - Body0.P());
		RelativeContactPosition1 = FSolverVec3(WorldContactPosition - Body1.P());

		// The world-space contact normal
		WorldContactNormal = InWorldContactNormal;

		// World-space contact tangents. We are treating the normal as the constraint-space Z axis
		// and the Tangent U and V as the constraint-space X and Y axes resepctively
		WorldContactTangentU = FSolverVec3::CrossProduct(FSolverVec3(0, 1, 0), InWorldContactNormal);
		if (!WorldContactTangentU.Normalize(FSolverReal(KINDA_SMALL_NUMBER)))
		{
			WorldContactTangentU = FSolverVec3::CrossProduct(FSolverVec3(1, 0, 0), InWorldContactNormal);
			WorldContactTangentU = WorldContactTangentU.GetUnsafeNormal();
		}
		WorldContactTangentV = FSolverVec3::CrossProduct(InWorldContactNormal, WorldContactTangentU);

		// The contact point error we are trying to correct in this solver
		const FSolverVec3 WorldContactDelta = InWorldAnchorPoint0 - InWorldAnchorPoint1;
		WorldContactDeltaNormal = FSolverVec3::DotProduct(WorldContactDelta, WorldContactNormal);
		WorldContactDeltaTangent0 = FSolverVec3::DotProduct(WorldContactDelta, WorldContactTangentU);
		WorldContactDeltaTangent1 = FSolverVec3::DotProduct(WorldContactDelta, WorldContactTangentV);
	}

	inline void FPBDCollisionSolverManifoldPoint::UpdateMass(const FConstraintSolverBody& Body0, const FConstraintSolverBody& Body1)
	{
		FSolverReal ContactMassInvNormal = FSolverReal(0);
		FSolverReal ContactMassInvTangentU = FSolverReal(0);
		FSolverReal ContactMassInvTangentV = FSolverReal(0);

		// These are not used if not initialized below so no need to clear
		//WorldContactNormalAngular0 = FSolverVec3(0);
		//WorldContactTangentUAngular0 = FSolverVec3(0);
		//WorldContactTangentVAngular0 = FSolverVec3(0);
		//WorldContactNormalAngular1 = FSolverVec3(0);
		//WorldContactTangentUAngular1 = FSolverVec3(0);
		//WorldContactTangentVAngular1 = FSolverVec3(0);

		if (Body0.IsDynamic())
		{
			const FSolverVec3 R0xN = FSolverVec3::CrossProduct(RelativeContactPosition0, WorldContactNormal);
			const FSolverVec3 R0xU = FSolverVec3::CrossProduct(RelativeContactPosition0, WorldContactTangentU);
			const FSolverVec3 R0xV = FSolverVec3::CrossProduct(RelativeContactPosition0, WorldContactTangentV);

			const FSolverMatrix33 InvI0 = Body0.InvI();

			WorldContactNormalAngular0 = InvI0 * R0xN;
			WorldContactTangentUAngular0 = InvI0 * R0xU;
			WorldContactTangentVAngular0 = InvI0 * R0xV;

			ContactMassInvNormal += FSolverVec3::DotProduct(R0xN, WorldContactNormalAngular0) + Body0.InvM();
			ContactMassInvTangentU += FSolverVec3::DotProduct(R0xU, WorldContactTangentUAngular0) + Body0.InvM();
			ContactMassInvTangentV += FSolverVec3::DotProduct(R0xV, WorldContactTangentVAngular0) + Body0.InvM();
		}
		if (Body1.IsDynamic())
		{
			const FSolverVec3 R1xN = FSolverVec3::CrossProduct(RelativeContactPosition1, WorldContactNormal);
			const FSolverVec3 R1xU = FSolverVec3::CrossProduct(RelativeContactPosition1, WorldContactTangentU);
			const FSolverVec3 R1xV = FSolverVec3::CrossProduct(RelativeContactPosition1, WorldContactTangentV);

			const FSolverMatrix33 InvI1 = Body1.InvI();

			WorldContactNormalAngular1 = InvI1 * R1xN;
			WorldContactTangentUAngular1 = InvI1 * R1xU;
			WorldContactTangentVAngular1 = InvI1 * R1xV;

			ContactMassInvNormal += FSolverVec3::DotProduct(R1xN, WorldContactNormalAngular1) + Body1.InvM();
			ContactMassInvTangentU += FSolverVec3::DotProduct(R1xU, WorldContactTangentUAngular1) + Body1.InvM();
			ContactMassInvTangentV += FSolverVec3::DotProduct(R1xV, WorldContactTangentVAngular1) + Body1.InvM();
		}

		ContactMassNormal = (ContactMassInvNormal > FSolverReal(SMALL_NUMBER)) ? FSolverReal(1) / ContactMassInvNormal : FSolverReal(0);
		ContactMassTangentU = (ContactMassInvTangentU > FSolverReal(SMALL_NUMBER)) ? FSolverReal(1) / ContactMassInvTangentU : FSolverReal(0);
		ContactMassTangentV = (ContactMassInvTangentV > FSolverReal(SMALL_NUMBER)) ? FSolverReal(1) / ContactMassInvTangentV : FSolverReal(0);
	}

	FSolverVec3 FPBDCollisionSolverManifoldPoint::CalculateContactVelocity(const FConstraintSolverBody& Body0, const FConstraintSolverBody& Body1) const
	{
		const FSolverVec3 ContactVelocity0 = Body0.V() + FVec3::CrossProduct(Body0.W(), RelativeContactPosition0);
		const FSolverVec3 ContactVelocity1 = Body1.V() + FVec3::CrossProduct(Body1.W(), RelativeContactPosition1);
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

	void FPBDCollisionSolver::SetShockPropagationInvMassScale(const FSolverReal InvMassScale)
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
			FSolverVec3(InWorldContactNormal));
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
			FSolverReal(InRestitution), 
			FSolverReal(InRestitutionVelocityThreshold),
			bInEnableStaticFriction, 
			FSolverReal(InFrictionNetPushOutNormal));

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
			FSolverVec3(InWorldContactNormal));
	}


	void FPBDCollisionSolver::SolveVelocityAverage(const FSolverReal Dt)
	{
		FConstraintSolverBody& Body0 = SolverBody0();
		FConstraintSolverBody& Body1 = SolverBody1();

		// Generate a new contact point at the average of all the active contacts
		int32 NumActiveManifoldPoints = 0;
		FSolverVec3 RelativeContactPosition0 = FVec3(0);
		FSolverVec3 RelativeContactPosition1 = FVec3(0);
		FSolverVec3 WorldContactNormal = FSolverVec3(0);
		FSolverReal NetPushOutNormal = FSolverReal(0);
		FSolverReal WorldContactVelocityTargetNormal = FReal(0);
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
			const FSolverReal DynamicFriction = FSolverReal(0);
			const FSolverReal Scale = FSolverReal(1) / FSolverReal(NumActiveManifoldPoints);

			FPBDCollisionSolverManifoldPoint AverageManifoldPoint;
			AverageManifoldPoint.RelativeContactPosition0 = RelativeContactPosition0 * Scale;
			AverageManifoldPoint.RelativeContactPosition1 = RelativeContactPosition1 * Scale;
			AverageManifoldPoint.WorldContactNormal = WorldContactNormal;
			AverageManifoldPoint.WorldContactVelocityTargetNormal = WorldContactVelocityTargetNormal * Scale;
			AverageManifoldPoint.NetPushOutNormal = NetPushOutNormal * Scale;

			const FSolverMatrix33 ContactMassInv =
				(Body0.IsDynamic() ? Collisions::ComputeFactorMatrix3(AverageManifoldPoint.RelativeContactPosition0, Body0.InvI(), Body0.InvM()) : FSolverMatrix33(0)) +
				(Body1.IsDynamic() ? Collisions::ComputeFactorMatrix3(AverageManifoldPoint.RelativeContactPosition1, Body1.InvI(), Body1.InvM()) : FSolverMatrix33(0));
			const FSolverReal ContactMassInvNormal = FSolverVec3::DotProduct(AverageManifoldPoint.WorldContactNormal, Utilities::Multiply(ContactMassInv, AverageManifoldPoint.WorldContactNormal));
			const FSolverReal ContactMassNormal = (ContactMassInvNormal > FSolverReal(SMALL_NUMBER)) ? FSolverReal(1) / ContactMassInvNormal : FSolverReal(0);
			AverageManifoldPoint.ContactMassNormal = ContactMassNormal;

			// @todo(chaos): All unused for the average contact solve - optimize
			AverageManifoldPoint.ContactMassTangentU = FSolverReal(0);
			AverageManifoldPoint.ContactMassTangentV = FSolverReal(0);
			AverageManifoldPoint.WorldContactTangentU = FSolverVec3(0);
			AverageManifoldPoint.WorldContactTangentV = FSolverVec3(0);
			AverageManifoldPoint.WorldContactDeltaNormal = FSolverReal(0);
			AverageManifoldPoint.WorldContactDeltaTangent0 = FSolverReal(0);
			AverageManifoldPoint.WorldContactDeltaTangent1 = FSolverReal(0);
			AverageManifoldPoint.NetPushOutTangentU = FSolverReal(0);
			AverageManifoldPoint.NetPushOutTangentV = FSolverReal(0);
			AverageManifoldPoint.NetImpulseNormal = FSolverReal(0);
			AverageManifoldPoint.NetImpulseTangentU = FSolverReal(0);
			AverageManifoldPoint.NetImpulseTangentV = FSolverReal(0);
			AverageManifoldPoint.FrictionNetPushOutNormal = FSolverReal(0);
			AverageManifoldPoint.bInsideStaticFrictionCone = false;

			// @todo(chaos): don't need tangential velocity delta here since we are setting DynamicFriction to 0
			FSolverReal ContactVelocityDeltaNormal, ContactVelocityDeltaTangentU, ContactVelocityDeltaTangentV;
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

	bool FPBDCollisionSolver::SolveVelocity(const FSolverReal Dt, const bool bApplyDynamicFriction)
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
		const FSolverReal DynamicFriction = (bApplyDynamicFriction && bChaos_PBDCollisionSolver_Velocity_DynamicFrictionEnabled) ? State.DynamicFriction : FSolverReal(0);

		for (int32 PointIndex = 0; PointIndex < State.NumManifoldPoints; ++PointIndex)
		{
			FPBDCollisionSolverManifoldPoint& SolverManifoldPoint = State.ManifoldPoints[PointIndex];
			if (!FMath::IsNearlyZero(SolverManifoldPoint.NetPushOutNormal))
			{
				FSolverReal ContactVelocityDeltaNormal, ContactVelocityDeltaTangentU, ContactVelocityDeltaTangentV;
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