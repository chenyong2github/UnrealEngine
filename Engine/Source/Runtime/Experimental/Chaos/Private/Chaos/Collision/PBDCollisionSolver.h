// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

#include "Chaos/Core.h"
#include "Chaos/Defines.h"
#include "Chaos/Evolution/SolverBody.h"

// Set to 0 to use a linearized error calculation, and set to 1 to use a non-linear error calculation in collision detection. 
// In principle nonlinear is more accurate when large rotation corrections occur, but this is not too important for collisions because 
// when the bodies settle the corrections are small. The linearized version is significantly faster than the non-linear version because 
// the non-linear version requires a quaternion multiply and renormalization whereas the linear version is just a cross product.
#define CHAOS_NONLINEAR_COLLISIONS_ENABLED 0

namespace Chaos
{
	namespace Collisions
	{
		struct FContactIterationParameters;
		struct FContactParticleParameters;
	}
	class FManifoldPoint;
	class FPBDCollisionSolver;
	class FPBDCollisionConstraint;

	namespace CVars
	{
		extern float Chaos_PBDCollisionSolver_Position_StaticFrictionStiffness;
		extern float Chaos_PBDCollisionSolver_Position_StaticFrictionLerpRate;
		
		extern bool bChaos_PBDCollisionSolver_Velocity_NegativeImpulseEnabled;
		extern bool bChaos_PBDCollisionSolver_Velocity_ImpulseClampEnabled;
	}


	/**
	 * @brief A single contact point in a FPBDCollisionSolver
	*/
	class FPBDCollisionSolverManifoldPoint
	{
	public:
		/**
		 * @brief Initialize the geometric data for the contact
		*/
		void InitContact(
			const FConstraintSolverBody& Body0,
			const FConstraintSolverBody& Body1,
			const FVec3& InWorldAnchorPoint0,
			const FVec3& InWorldAnchorPoint1,
			const FSolverVec3& InWorldContactNormal);

		/**
		 * @brief Initialize the material related properties of the contact
		*/
		void InitMaterial(
			const FConstraintSolverBody& Body0,
			const FConstraintSolverBody& Body1,
			const FSolverReal InRestitution,
			const FSolverReal InRestitutionVelocityThreshold,
			const bool bInEnableStaticFriction,
			const FSolverReal InFrictionNetPushOutNormal);

		/**
		 * @brief Update the world-space relative contact points based on current body transforms and body-space contact positions
		*/
		void UpdateContact(
			const FConstraintSolverBody& Body0,
			const FConstraintSolverBody& Body1,
			const FVec3& InWorldAnchorPoint0,
			const FVec3& InWorldAnchorPoint1,
			const FSolverVec3& InWorldContactNormal);

		/**
		 * @brief Update the cached mass properties based on the current body transforms
		*/
		void UpdateMass(const FConstraintSolverBody& Body0, const FConstraintSolverBody& Body1);

		/**
		 * @brief Calculate the position error at the current transforms
		 * @param MaxPushOut a limit on the position error for this iteration to prevent initial-penetration explosion (a common PBD problem)
		*/
		void CalculateContactPositionErrorNormal(const FConstraintSolverBody& Body0, const FConstraintSolverBody& Body1, const FSolverReal MaxPushOut, FSolverReal& OutContactDeltaNormal) const;
		void CalculateContactPositionError(const FConstraintSolverBody& Body0, const FConstraintSolverBody& Body1, const FSolverReal MaxPushOut, FSolverReal& OutContactDeltaNormal, FSolverReal& OutContactDeltaTanget0, FSolverReal& OutContactDeltaTangent1) const;

		/**
		 * @brief Calculate the velocity error at the current transforms
		*/
		void CalculateContactVelocityError(const FConstraintSolverBody& Body0, const FConstraintSolverBody& Body1, const FSolverReal DynamicFriction, const FSolverReal Dt, FSolverReal& OutContactVelocityDeltaNormal, FSolverReal& OutContactVelocityDeltaTangent0, FSolverReal& OutContactVelocityDeltaTangent1) const;

		// @todo(chaos): make private
	public:
		friend class FPBDCollisionSolver;

		/**
		 * @brief Calculate the relative velocity at the contact point
		 * @note InitContact must be called before calling this function
		*/
		FSolverVec3 CalculateContactVelocity(const FConstraintSolverBody& Body0, const FConstraintSolverBody& Body1) const;

		// World-space body-relative contact points
		FSolverVec3 RelativeContactPosition0;
		FSolverVec3 RelativeContactPosition1;

		// World-space contact axes (normal and 2 tangents)
		FSolverVec3 WorldContactNormal;
		FSolverVec3 WorldContactTangentU;
		FSolverVec3 WorldContactTangentV;

		// I^-1.(R x A) for each body where A is each axis (Normal, TangentU, TangentV)
		FSolverVec3 WorldContactNormalAngular0;
		FSolverVec3 WorldContactTangentUAngular0;
		FSolverVec3 WorldContactTangentVAngular0;
		FSolverVec3 WorldContactNormalAngular1;
		FSolverVec3 WorldContactTangentUAngular1;
		FSolverVec3 WorldContactTangentVAngular1;

		// Contact mass (for non-friction)
		FSolverReal ContactMassNormal;
		FSolverReal ContactMassTangentU;
		FSolverReal ContactMassTangentV;

		// Initial world-space contact separation that we are trying to correct
		FSolverReal WorldContactDeltaNormal;
		FSolverReal WorldContactDeltaTangent0;
		FSolverReal WorldContactDeltaTangent1;

		// Desired final normal velocity, taking Restitution into account
		FSolverReal WorldContactVelocityTargetNormal;

		// Solver outputs
		FSolverReal NetPushOutNormal;
		FSolverReal NetPushOutTangentU;
		FSolverReal NetPushOutTangentV;
		FSolverReal NetImpulseNormal;
		FSolverReal NetImpulseTangentU;
		FSolverReal NetImpulseTangentV;

		// A smoothed NetPushOutNormal along the normal, used for clipping to the static friction cone
		FSolverReal FrictionNetPushOutNormal;

		// Whether we are still in the static friction cone
		bool bInsideStaticFrictionCone;
	};

	/**
	 * @brief 
	 * @todo(chaos): Make this solver operate on a single contact point rather than all points in a manifold.
	 * This would be beneficial if we have many contacts with less than 4 points in the manifold. However this
	 * is dificult to do while we are still supporting non-manifold collisions.
	*/
	class FPBDCollisionSolver
	{
	public:
		static const int32 MaxConstrainedBodies = 2;
		static const int32 MaxPointsPerConstraint = 4;

		FPBDCollisionSolver();

		/** Reset the state of the collision solver */
		void Reset()
		{
			State.Reset();
		}

		FSolverReal StaticFriction() const { return State.StaticFriction; }
		FSolverReal DynamicFriction() const { return State.DynamicFriction; }

		void SetFriction(const FReal InStaticFriction, const FReal InDynamicFriction)
		{
			State.StaticFriction = FSolverReal(InStaticFriction);
			State.DynamicFriction = FSolverReal(InDynamicFriction);
		}

		void SetStiffness(const FReal InStiffness)
		{
			State.Stiffness = FSolverReal(InStiffness);
		}

		void SetSolverBodies(FSolverBody* SolverBody0, FSolverBody* SolverBody1)
		{
			State.SolverBodies[0] = *SolverBody0;
			State.SolverBodies[1] = *SolverBody1;
		}

		void ResetSolverBodies()
		{
			State.SolverBodies[0].Reset();
			State.SolverBodies[1].Reset();
		}

		int32 NumManifoldPoints() const
		{
			return State.NumManifoldPoints;
		}

		int32 SetNumManifoldPoints(const int32 InNumManifoldPoints)
		{
			State.NumManifoldPoints = FMath::Min(InNumManifoldPoints, MaxPointsPerConstraint);
			return State.NumManifoldPoints;
		}

		const FPBDCollisionSolverManifoldPoint& GetManifoldPoint(const int32 ManifoldPointIndex) const
		{
			check(ManifoldPointIndex < NumManifoldPoints());
			return State.ManifoldPoints[ManifoldPointIndex];
		}

		void InitContact(
			const int32 ManifoldPoiontIndex,
			const FVec3& InWorldAnchorPoint0,
			const FVec3& InWorldAnchorPoint1,
			const FVec3& InWorldContactNormal);

		void InitMaterial(
			const int32 ManifoldPoiontIndex,
			const FReal InRestitution,
			const FReal InRestitutionVelocityThreshold,
			const bool bInEnableStaticFriction,
			const FReal InFrictionNetPushOutNormal);

		void UpdateContact(
			const int32 ManifoldPoiontIndex,
			const FVec3& InWorldAnchorPoint0,
			const FVec3& InWorldAnchorPoint1,
			const FVec3& InWorldContactNormal);

		/**
		 * @brief Get the first (decaorated) solver body
		 * The decorator add a possible mass scale
		*/
		FConstraintSolverBody& SolverBody0() { return State.SolverBodies[0]; }
		const FConstraintSolverBody& SolverBody0() const { return State.SolverBodies[0]; }

		/**
		 * @brief Get the second (decaorated) solver body
		 * The decorator add a possible mass scale
		*/
		FConstraintSolverBody& SolverBody1() { return State.SolverBodies[1]; }
		const FConstraintSolverBody& SolverBody1() const { return State.SolverBodies[1]; }

		/**
		 * @brief Set up the mass scaling for shock propagation, using the position-phase mass scale
		*/
		void EnablePositionShockPropagation();

		/**
		 * @brief Set up the mass scaling for shock propagation, using the velocity-phase mass scale
		*/
		void EnableVelocityShockPropagation();

		/**
		 * @brief Disable mass scaling
		*/
		void DisableShockPropagation();

		/**
		 * @brief Calculate and apply the position correction for this iteration
		 * @return true if we need to run more iterations, false if we did not apply any correction
		*/
		bool SolvePositionWithFriction(const FSolverReal Dt, const FSolverReal MaxPushOut);
		bool SolvePositionNoFriction(const FSolverReal Dt, const FSolverReal MaxPushOut);

		/**
		 * @brief Calculate and apply the velocity correction for this iteration
		 * @return true if we need to run more iterations, false if we did not apply any correction
		*/
		bool SolveVelocity(const FSolverReal Dt, const bool bApplyDynamicFriction);

	private:
		/**
		 * @brief Apply the inverse mass scale the body with the lower level
		 * @param InvMassScale 
		*/
		void SetShockPropagationInvMassScale(const FSolverReal InvMassScale);

		/**
		 * @brief Run a velocity solve on the average point from all the points that received a position impulse
		 * This is used to enforce Restitution constraints without introducing rotation artefacts without
		 * adding more velocity iterations.
		 * This will only perform work if there is more than one active contact.
		*/
		void SolveVelocityAverage(const FSolverReal Dt);

		struct FState
		{
			FState()
				: StaticFriction(0)
				, DynamicFriction(0)
				, Stiffness(1)
				, NumManifoldPoints(0)
				, bHaveRestitution(false)
				, SolverBodies()
				, ManifoldPoints()
			{
			}

			/** Reset the state struct members to its default values */
			void Reset()
			{
				StaticFriction = 0;
				DynamicFriction = 0;
				Stiffness = 1;
				bHaveRestitution = false;
				NumManifoldPoints = 0;
			}

			FSolverReal StaticFriction;
			FSolverReal DynamicFriction;
			FSolverReal Stiffness;
			int32 NumManifoldPoints;
			bool bHaveRestitution;
			FConstraintSolverBody SolverBodies[MaxConstrainedBodies];
			FPBDCollisionSolverManifoldPoint ManifoldPoints[MaxPointsPerConstraint];
		};

		FState State;
	};


	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////


	FORCEINLINE_DEBUGGABLE void CalculatePositionCorrectionNormal(
		const FSolverReal Stiffness,
		const FSolverReal ContactDeltaNormal,
		const FSolverReal ContactMassNormal,
		const FSolverReal NetPushOutNormal,
		FSolverReal& OutPushOutNormal)
	{
		const FSolverReal PushOutNormal = -Stiffness * ContactDeltaNormal * ContactMassNormal;

		// The total pushout so far this sub-step
		// Unilateral constraint: Net-negative impulses not allowed (negative incremental impulses are allowed as long as the net is positive)
		if ((NetPushOutNormal + PushOutNormal) > FSolverReal(0))
		{
			OutPushOutNormal = PushOutNormal;
		}
		else
		{
			OutPushOutNormal = -NetPushOutNormal;
		}
	}


	FORCEINLINE_DEBUGGABLE void CalculatePositionCorrectionTangent(
		const FSolverReal Stiffness,
		const FSolverReal ContactDeltaTangent,
		const FSolverReal ContactMassTangent,
		FSolverReal& OutPushOutTangent)
	{
		const FSolverReal FrictionStiffness = CVars::Chaos_PBDCollisionSolver_Position_StaticFrictionStiffness;

		// Bilateral constraint - negative values allowed (unlike the normal correction)
		OutPushOutTangent = -Stiffness * FrictionStiffness * ContactMassTangent * ContactDeltaTangent;
	}


	FORCEINLINE_DEBUGGABLE void ApplyFrictionCone(
		const FSolverReal StaticFriction,
		const FSolverReal DynamicFriction,
		const FSolverReal NetPushOutNormal,
		FSolverReal& InOutPushOutTangentU,
		FSolverReal& InOutPushOutTangentV,
		FSolverReal& InOutNetPushOutTangentU,
		FSolverReal& InOutNetPushOutTangentV,
		FSolverReal& InOutFrictionNetPushOutNormal,
		bool& bInOutInsideStaticFrictionCone)
	{
		bInOutInsideStaticFrictionCone = true;
		// Static friction limit: immediately increase maximum lateral correction, but smoothly decay maximum static friction limit. 
		// This is so that small variations in position (jitter) and therefore NetPushOutNormal don't cause static friction to slip
		// @todo(chaos): StaticFriction smoothing is iteration count depenendent - try to make it not so
		const FSolverReal StaticFrictionLerpRate = CVars::Chaos_PBDCollisionSolver_Position_StaticFrictionLerpRate;
		const FSolverReal StaticFrictionTarget = FMath::Max(NetPushOutNormal, FSolverReal(0));
		InOutFrictionNetPushOutNormal = FMath::Lerp(FMath::Max(InOutFrictionNetPushOutNormal, StaticFrictionTarget), StaticFrictionTarget, StaticFrictionLerpRate);

		if (NetPushOutNormal < FSolverReal(SMALL_NUMBER))
		{
			// Note: we have already added the current iteration's PushOut to the NetPushOut but it has not been applied to the body
			// so we must subtract it again to calculate the actual pushout we want to undo (i.e., the net pushout that has been applied to the body so far)
			InOutPushOutTangentU = -(InOutNetPushOutTangentU - InOutPushOutTangentU);
			InOutPushOutTangentV = -(InOutNetPushOutTangentV - InOutPushOutTangentV);
			InOutNetPushOutTangentU = FReal(0);
			InOutNetPushOutTangentV = FReal(0);
			bInOutInsideStaticFrictionCone = false;
		}

		// If we exceed the friction cone, clip to cone and stop adding frictional corrections
		if (bInOutInsideStaticFrictionCone)
		{
			const FSolverReal MaxPushOutTangent = StaticFriction * InOutFrictionNetPushOutNormal;
			const FSolverReal MaxPushOutTangentSq = FMath::Square(MaxPushOutTangent);
			const FSolverReal NetPushOutTangentSq = FMath::Square(InOutNetPushOutTangentU) + FMath::Square(InOutNetPushOutTangentV);
			if (NetPushOutTangentSq > MaxPushOutTangentSq)
			{
				// @todo(chaos): clamp to dynamic friction rather than static
				const FSolverReal ClippedMaxPushOutTangent = MaxPushOutTangent;
				const FSolverReal InvNetPushOutTangent = FMath::InvSqrt(NetPushOutTangentSq);
				const FSolverReal NetPushOutTangentU = ClippedMaxPushOutTangent * InOutNetPushOutTangentU * InvNetPushOutTangent;
				const FSolverReal NetPushOutTangentV = ClippedMaxPushOutTangent * InOutNetPushOutTangentV * InvNetPushOutTangent;
				InOutPushOutTangentU = NetPushOutTangentU - (InOutNetPushOutTangentU - InOutPushOutTangentU);
				InOutPushOutTangentV = NetPushOutTangentV - (InOutNetPushOutTangentV - InOutPushOutTangentV);
				InOutNetPushOutTangentU = NetPushOutTangentU;
				InOutNetPushOutTangentV = NetPushOutTangentV;
				bInOutInsideStaticFrictionCone = false;
			}
		}
	}


	FORCEINLINE_DEBUGGABLE void ApplyPositionCorrectionWithFriction(
		const FSolverReal Stiffness,
		const FSolverReal StaticFriction,
		const FSolverReal DynamicFriction,
		const FSolverReal ContactDeltaNormal,
		const FSolverReal ContactDeltaTangent0,
		const FSolverReal ContactDeltaTangent1,
		FPBDCollisionSolverManifoldPoint& ManifoldPoint,
		FConstraintSolverBody& Body0,
		FConstraintSolverBody& Body1)
	{
		FSolverReal PushOutNormal = FSolverReal(0);
		FSolverReal PushOutTangentU = FSolverReal(0);
		FSolverReal PushOutTangentV = FSolverReal(0);

		CalculatePositionCorrectionNormal(
			Stiffness,
			ContactDeltaNormal,
			ManifoldPoint.ContactMassNormal,
			ManifoldPoint.NetPushOutNormal,
			PushOutNormal);						// Out

		CalculatePositionCorrectionTangent(
			Stiffness,
			ContactDeltaTangent0,
			ManifoldPoint.ContactMassTangentU,
			PushOutTangentU);					// Out

		CalculatePositionCorrectionTangent(
			Stiffness,
			ContactDeltaTangent1,
			ManifoldPoint.ContactMassTangentV,
			PushOutTangentV);					// Out

		ManifoldPoint.NetPushOutNormal += PushOutNormal;
		ManifoldPoint.NetPushOutTangentU += PushOutTangentU;
		ManifoldPoint.NetPushOutTangentV += PushOutTangentV;

		ApplyFrictionCone(
			StaticFriction,
			DynamicFriction,
			ManifoldPoint.NetPushOutNormal,
			PushOutTangentU,									// Out
			PushOutTangentV,									// Out
			ManifoldPoint.NetPushOutTangentU,					// Out
			ManifoldPoint.NetPushOutTangentV,					// Out
			ManifoldPoint.FrictionNetPushOutNormal,				// Out
			ManifoldPoint.bInsideStaticFrictionCone);			// Out

		const FSolverVec3 PushOut = PushOutNormal * ManifoldPoint.WorldContactNormal + PushOutTangentU * ManifoldPoint.WorldContactTangentU + PushOutTangentV * ManifoldPoint.WorldContactTangentV;

		// Update the particle state based on the pushout
		if (Body0.IsDynamic())
		{
			const FSolverVec3 DX0 = Body0.InvM() * PushOut;
			const FSolverVec3 DR0 = ManifoldPoint.WorldContactNormalAngular0 * PushOutNormal + ManifoldPoint.WorldContactTangentUAngular0 * PushOutTangentU + ManifoldPoint.WorldContactTangentVAngular0 * PushOutTangentV;
			Body0.ApplyPositionDelta(DX0);
			Body0.ApplyRotationDelta(DR0);
		}
		if (Body1.IsDynamic())
		{
			const FSolverVec3 DX1 = -(Body1.InvM() * PushOut);
			const FSolverVec3 DR1 = ManifoldPoint.WorldContactNormalAngular1 * -PushOutNormal + ManifoldPoint.WorldContactTangentUAngular1 * -PushOutTangentU + ManifoldPoint.WorldContactTangentVAngular1 * -PushOutTangentV;
			Body1.ApplyPositionDelta(DX1);
			Body1.ApplyRotationDelta(DR1);
		}
	}

	FORCEINLINE_DEBUGGABLE void ApplyPositionCorrectionNoFriction(
		const FSolverReal Stiffness,
		const FSolverReal ContactDeltaNormal,
		FPBDCollisionSolverManifoldPoint& ManifoldPoint,
		FConstraintSolverBody& Body0,
		FConstraintSolverBody& Body1)
	{
		FSolverReal PushOutNormal = FSolverReal(0);

		CalculatePositionCorrectionNormal(
			Stiffness,
			ContactDeltaNormal,
			ManifoldPoint.ContactMassNormal,
			ManifoldPoint.NetPushOutNormal,
			PushOutNormal);						// Out

		ManifoldPoint.NetPushOutNormal += PushOutNormal;

		// Update the particle state based on the pushout
		if (Body0.IsDynamic())
		{
			const FSolverVec3 DX0 = (Body0.InvM() * PushOutNormal) * ManifoldPoint.WorldContactNormal;
			const FSolverVec3 DR0 = ManifoldPoint.WorldContactNormalAngular0 * PushOutNormal;
			Body0.ApplyPositionDelta(DX0);
			Body0.ApplyRotationDelta(DR0);
		}
		if (Body1.IsDynamic())
		{
			const FSolverVec3 DX1 = (Body1.InvM() * -PushOutNormal) * ManifoldPoint.WorldContactNormal;
			const FSolverVec3 DR1 = ManifoldPoint.WorldContactNormalAngular1 * -PushOutNormal;
			Body1.ApplyPositionDelta(DX1);
			Body1.ApplyRotationDelta(DR1);
		}
	}


	FORCEINLINE_DEBUGGABLE void ApplyVelocityCorrection(
		const FSolverReal Stiffness,
		const FSolverReal Dt,
		const FSolverReal DynamicFriction,
		const FSolverReal ContactVelocityDeltaNormal,
		const FSolverReal ContactVelocityDeltaTangent0,
		const FSolverReal ContactVelocityDeltaTangent1,
		FPBDCollisionSolverManifoldPoint& ManifoldPoint,
		FConstraintSolverBody& Body0,
		FConstraintSolverBody& Body1)
	{
		FSolverReal ImpulseNormal = -(Stiffness * ManifoldPoint.ContactMassNormal) * ContactVelocityDeltaNormal;
		
		// Clamp the total impulse to be positive along the normal. We can apply a net negative impulse, 
		// but only to correct the velocity that was added by pushout.
		FSolverReal NetImpulseNormal = ManifoldPoint.NetImpulseNormal + ImpulseNormal;
		const FSolverReal PushOutImpulseNormal = FMath::Max(0.0f, ManifoldPoint.NetPushOutNormal / Dt);
		if (NetImpulseNormal < -PushOutImpulseNormal)
		{
			// We are trying to apply a net negative impulse larger than one to counteract the effective pushout impulse
			// so clamp the net impulse to be equal to minus the pushout impulse along the normal.
			// NOTE: NetImpulseNormal is negative here
			ImpulseNormal = ImpulseNormal - (NetImpulseNormal + PushOutImpulseNormal);
		}

		FSolverReal ImpulseTangentU = FSolverReal(0);
		FSolverReal ImpulseTangentV = FSolverReal(0);
		const bool bApplyFriction = (DynamicFriction > 0) && !FMath::IsNearlyZero(ImpulseNormal) && (Dt > 0);
		if (bApplyFriction)
		{
			ImpulseTangentU = -(Stiffness * ManifoldPoint.ContactMassTangentU) * ContactVelocityDeltaTangent0;
			ImpulseTangentV = -(Stiffness * ManifoldPoint.ContactMassTangentV) * ContactVelocityDeltaTangent1;
		}

		ManifoldPoint.NetImpulseNormal += ImpulseNormal;
		ManifoldPoint.NetImpulseTangentU += ImpulseTangentU;
		ManifoldPoint.NetImpulseTangentV += ImpulseTangentV;

		FSolverVec3 Impulse = ImpulseNormal * ManifoldPoint.WorldContactNormal + ImpulseTangentU * ManifoldPoint.WorldContactTangentU + ImpulseTangentV * ManifoldPoint.WorldContactTangentV;

		// Calculate the velocity deltas from the impulse
		if (Body0.IsDynamic())
		{
			const FSolverVec3 AngularImpulse = FSolverVec3::CrossProduct(ManifoldPoint.RelativeContactPosition0, Impulse);
			Body0.ApplyVelocityDelta(Body0.InvM() * Impulse, Body0.InvI() * AngularImpulse);
		}
		if (Body1.IsDynamic())
		{
			const FSolverVec3 AngularImpulse = FSolverVec3::CrossProduct(ManifoldPoint.RelativeContactPosition1, Impulse);
			Body1.ApplyVelocityDelta(Body1.InvM() * -Impulse, Body1.InvI() * -AngularImpulse);
		}
	}


	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////

	FORCEINLINE_DEBUGGABLE void FPBDCollisionSolverManifoldPoint::CalculateContactPositionErrorNormal(const FConstraintSolverBody& Body0, const FConstraintSolverBody& Body1, const FSolverReal MaxPushOut, FSolverReal& OutContactDeltaNormal) const
	{
		// Linear version: calculate the contact delta assuming linear motion after applying a positional impulse at the contact point. There will be an error that depends on the size of the rotation.
		const FSolverVec3 ContactDelta0 = FSolverVec3(Body0.DP()) + FSolverVec3::CrossProduct(Body0.DQ(), RelativeContactPosition0);
		const FSolverVec3 ContactDelta1 = FSolverVec3(Body1.DP()) + FSolverVec3::CrossProduct(Body1.DQ(), RelativeContactPosition1);
		const FSolverVec3 ContactDelta = ContactDelta0 - ContactDelta1;
		OutContactDeltaNormal = WorldContactDeltaNormal + FSolverVec3::DotProduct(ContactDelta, WorldContactNormal);

		// NOTE: OutContactDeltaNormal is negative for penetration
		// NOTE: MaxPushOut == 0 disables the pushout limits
		if ((MaxPushOut > 0) && (OutContactDeltaNormal < -MaxPushOut))
		{
			const FSolverReal ClampedContactDeltaNormal = -MaxPushOut;
			OutContactDeltaNormal = ClampedContactDeltaNormal;
		}
	}

	FORCEINLINE_DEBUGGABLE void FPBDCollisionSolverManifoldPoint::CalculateContactPositionError(const FConstraintSolverBody& Body0, const FConstraintSolverBody& Body1, const FSolverReal MaxPushOut, FSolverReal& OutContactDeltaNormal, FSolverReal& OutContactDeltaTangent0, FSolverReal& OutContactDeltaTangent1) const
	{
#if CHAOS_NONLINEAR_COLLISIONS_ENABLED
#error "Non-linear collision solver no longer functional - fix it"
		// Non-linear version: calculate the contact delta after we have converted the current positional impulses into position and rotation corrections.
		// We could precalculate and store the LocalContactPositions if we really want to use this nonlinear version
		const FSolverVec3 LocalContactPosition0 = Body0.Q().Inverse() * WorldRelativeContactPosition0;
		const FSolverVec3 LocalContactPosition1 = Body1.Q().Inverse() * WorldRelativeContactPosition1;
		const FSolverVec3 ContactDelta = (Body0.CorrectedP() + Body0.CorrectedQ() * LocalContactPosition0) - (Body1.CorrectedP() + Body1.CorrectedQ() * LocalContactPosition1);
#else
		// Linear version: calculate the contact delta assuming linear motion after applying a positional impulse at the contact point. There will be an error that depends on the size of the rotation.
		const FSolverVec3 ContactDelta0 = FSolverVec3(Body0.DP()) + FSolverVec3::CrossProduct(Body0.DQ(), RelativeContactPosition0);
		const FSolverVec3 ContactDelta1 = FSolverVec3(Body1.DP()) + FSolverVec3::CrossProduct(Body1.DQ(), RelativeContactPosition1);
		const FSolverVec3 ContactDelta = ContactDelta0 - ContactDelta1;
		OutContactDeltaNormal = WorldContactDeltaNormal + FSolverVec3::DotProduct(ContactDelta, WorldContactNormal);
		OutContactDeltaTangent0 = WorldContactDeltaTangent0 + FSolverVec3::DotProduct(ContactDelta, WorldContactTangentU);
		OutContactDeltaTangent1 = WorldContactDeltaTangent1 + FSolverVec3::DotProduct(ContactDelta, WorldContactTangentV);
#endif

		// NOTE: OutContactDeltaNormal is negative for penetration
		// NOTE: MaxPushOut == 0 disables the pushout limits
		if ((MaxPushOut > 0) && (OutContactDeltaNormal < -MaxPushOut))
		{
			OutContactDeltaNormal = -MaxPushOut;
		}
	}

	FORCEINLINE_DEBUGGABLE void FPBDCollisionSolverManifoldPoint::CalculateContactVelocityError(const FConstraintSolverBody& Body0, const FConstraintSolverBody& Body1, const FSolverReal DynamicFriction, const FSolverReal Dt, FSolverReal& OutContactVelocityDeltaNormal, FSolverReal& OutContactVelocityDeltaTangent0, FSolverReal& OutContactVelocityDeltaTangent1) const
	{
		const FSolverVec3 ContactVelocity0 = Body0.V() + FSolverVec3::CrossProduct(Body0.W(), RelativeContactPosition0);
		const FSolverVec3 ContactVelocity1 = Body1.V() + FSolverVec3::CrossProduct(Body1.W(), RelativeContactPosition1);
		const FSolverVec3 ContactVelocity = ContactVelocity0 - ContactVelocity1;
		const FSolverReal ContactVelocityNormal = FSolverVec3::DotProduct(ContactVelocity, WorldContactNormal);

		// Target normal velocity, including restitution
		const FSolverReal ContactVelocityTargetNormal = WorldContactVelocityTargetNormal;

		// Add up the errors in the velocity (current velocity - desired velocity)
		OutContactVelocityDeltaNormal = (ContactVelocityNormal - WorldContactVelocityTargetNormal);
		OutContactVelocityDeltaTangent0 = FSolverReal(0);
		OutContactVelocityDeltaTangent1 = FSolverReal(0);

		const bool bApplyFriction = (DynamicFriction > 0) && (Dt > 0);
		if (bApplyFriction)
		{
			const FSolverReal ContactVelocityTangent0 = FSolverVec3::DotProduct(ContactVelocity, WorldContactTangentU);
			const FSolverReal ContactVelocityTangent1 = FSolverVec3::DotProduct(ContactVelocity, WorldContactTangentV);
			OutContactVelocityDeltaTangent0 = ContactVelocityTangent0;
			OutContactVelocityDeltaTangent1 = ContactVelocityTangent1;

			const FSolverReal ContactVelocityTangentialSq = FMath::Square(ContactVelocityTangent0) + FMath::Square(ContactVelocityTangent1);
			if (ContactVelocityTangentialSq > FSolverReal(SMALL_NUMBER))
			{
				// PushOut = ContactMass * DP, where DP is the contact positional correction
				// Friction force is proportional to the normal force, so friction velocity correction
				// is proprtional to normal velocity correction, or DVn = DPn/dt = PushOut.N / (ContactMass * dt);
				const FSolverReal MaxContactVelocityTangential = DynamicFriction * NetPushOutNormal / (ContactMassNormal * Dt);
				if (ContactVelocityTangentialSq > FMath::Square(MaxContactVelocityTangential))
				{
					const FSolverReal ContactVelocityTangentialScale = MaxContactVelocityTangential * FMath::InvSqrt(ContactVelocityTangentialSq);
					OutContactVelocityDeltaTangent0 *= ContactVelocityTangentialScale;
					OutContactVelocityDeltaTangent1 *= ContactVelocityTangentialScale;
				}
			}
		}
	}


	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////


	FORCEINLINE_DEBUGGABLE bool FPBDCollisionSolver::SolvePositionWithFriction(const FSolverReal Dt, const FSolverReal MaxPushOut)
	{
		// SolverBody decorator used to add mass scaling
		FConstraintSolverBody& Body0 = SolverBody0();
		FConstraintSolverBody& Body1 = SolverBody1();

		// Apply the position correction so that all contacts have zero separation
		for (int32 PointIndex = 0; PointIndex < State.NumManifoldPoints; ++PointIndex)
		{
			FPBDCollisionSolverManifoldPoint& SolverManifoldPoint = State.ManifoldPoints[PointIndex];

			FSolverReal ContactDeltaNormal, ContactDeltaTangent0, ContactDeltaTangent1;
			SolverManifoldPoint.CalculateContactPositionError(Body0.SolverBody(), Body1.SolverBody(), MaxPushOut, ContactDeltaNormal, ContactDeltaTangent0, ContactDeltaTangent1);

			const bool bProcessManifoldPoint = (ContactDeltaNormal < FSolverReal(0)) || !FMath::IsNearlyZero(SolverManifoldPoint.NetPushOutNormal);
			if (bProcessManifoldPoint)
			{
				// If we want to add static friction...
				// (note: we run a few iterations without friction by temporarily setting StaticFriction to 0)
				const bool bApplyFriction = (State.StaticFriction > 0) && SolverManifoldPoint.bInsideStaticFrictionCone;
				if (bApplyFriction)
				{
					ApplyPositionCorrectionWithFriction(
						State.Stiffness,
						State.StaticFriction,
						State.DynamicFriction,
						ContactDeltaNormal,
						ContactDeltaTangent0,
						ContactDeltaTangent1,
						SolverManifoldPoint,
						Body0,
						Body1);
				}
				else
				{
					ApplyPositionCorrectionNoFriction(
						State.Stiffness, 
						ContactDeltaNormal, 
						SolverManifoldPoint, 
						Body0, 
						Body1);
				}
			}
		}

		return false;
	}

	FORCEINLINE_DEBUGGABLE bool FPBDCollisionSolver::SolvePositionNoFriction(const FSolverReal Dt, const FSolverReal MaxPushOut)
	{
		// SolverBody decorator used to add mass scaling
		FConstraintSolverBody& Body0 = SolverBody0();
		FConstraintSolverBody& Body1 = SolverBody1();

		// Apply the position correction so that all contacts have zero separation
		for (int32 PointIndex = 0; PointIndex < State.NumManifoldPoints; ++PointIndex)
		{
			FPBDCollisionSolverManifoldPoint& SolverManifoldPoint = State.ManifoldPoints[PointIndex];

			FSolverReal ContactDeltaNormal;
			SolverManifoldPoint.CalculateContactPositionErrorNormal(Body0.SolverBody(), Body1.SolverBody(), MaxPushOut, ContactDeltaNormal);

			const bool bProcessManifoldPoint = (ContactDeltaNormal < FSolverReal(0)) || !FMath::IsNearlyZero(SolverManifoldPoint.NetPushOutNormal);
			if (bProcessManifoldPoint)
			{
				ApplyPositionCorrectionNoFriction(
					State.Stiffness,
					ContactDeltaNormal,
					SolverManifoldPoint,
					Body0,
					Body1);
			}
		}

		return false;
	}

}

CHAOS_API DECLARE_LOG_CATEGORY_EXTERN(LogChaosCollision, Log, All);
