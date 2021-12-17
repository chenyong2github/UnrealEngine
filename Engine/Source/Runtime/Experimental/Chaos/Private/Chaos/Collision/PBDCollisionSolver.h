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
			const FVec3& InWorldContactNormal);

		/**
		 * @brief Initialize the material related properties of the contact
		*/
		void InitMaterial(
			const FConstraintSolverBody& Body0,
			const FConstraintSolverBody& Body1,
			const FReal InRestitution,
			const FReal InRestitutionVelocityThreshold,
			const bool bInEnableStaticFriction,
			const FReal InStaticFrictionMax);

		/**
		 * @brief Update the world-space relative contact points based on current body transforms and body-space contact positions
		*/
		void UpdateContact(
			const FConstraintSolverBody& Body0,
			const FConstraintSolverBody& Body1,
			const FVec3& InWorldAnchorPoint0,
			const FVec3& InWorldAnchorPoint1,
			const FVec3& InWorldContactNormal);

		/**
		 * @brief Update the cached mass properties based on the current body transforms
		*/
		void UpdateMass(const FConstraintSolverBody& Body0, const FConstraintSolverBody& Body1);

		/**
		 * @brief Calculate the relative velocity at the contact point
		 * @note InitContact must be called before calling this function
		*/
		FVec3 CalculateContactVelocity(const FConstraintSolverBody& Body0, const FConstraintSolverBody& Body1) const;

		/**
		 * @brief Calculate the position error at the current transforms
		 * @param MaxPushOut a limit on the position error for this iteration to prevent initial-penetration explosion (a common PBD problem)
		*/
		void CalculateContactPositionError(const FConstraintSolverBody& Body0, const FConstraintSolverBody& Body1, const FReal MaxPushOut, FVec3& OutContactDelta, FReal& OutContactDeltaNormal) const;

		/**
		 * @brief Calculate the velocity error at the current transforms
		*/
		void CalculateContactVelocityError(const FConstraintSolverBody& Body0, const FConstraintSolverBody& Body1, const FReal DynamicFriction, const FReal Dt, FVec3& OutContactVelocityDelta, FReal& OutContactVelocityDeltaNormal) const;

		// @todo(chaos): make private
	public:
		friend class FPBDCollisionSolver;

		// World-space contact point
		FVec3 WorldContactPosition;

		// World-space contact normal
		FVec3 WorldContactNormal;

		// Contact mass
		FMatrix33 WorldContactMass;
		FReal WorldContactMassNormal;

		// World-space contact separation that we are trying to correct
		FVec3 WorldContactDelta;

		// Desired final normal velocity, taking Restitution into account
		FReal WorldContactVelocityTargetNormal;

		// Solver outputs
		FVec3 NetPushOut;
		FVec3 NetImpulse;

		// A smoothed NetImpulse along the normal, used for clipping to the static friction cone
		FReal StaticFrictionMax;

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

		FReal StaticFriction() const { return State.StaticFriction; }
		FReal DynamicFriction() const { return State.DynamicFriction; }

		void SetFriction(const FReal InStaticFriction, const FReal InDynamicFriction)
		{
			State.StaticFriction = InStaticFriction;
			State.DynamicFriction = InDynamicFriction;
		}

		void SetStiffness(const FReal InStiffness)
		{
			State.Stiffness = InStiffness;
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

		int32 NumPositionSolves() const
		{
			return State.NumPositionSolves;
		}

		int32 NumVelocitySolves() const
		{
			return State.NumVelocitySolves;
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
			const FReal InStaticFrictionMax);

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
		bool SolvePositionWithFriction(const FReal Dt, const FReal MaxPushOut);
		bool SolvePositionNoFriction(const FReal Dt, const FReal MaxPushOut);

		/**
		 * @brief Calculate and apply the velocity correction for this iteration
		 * @return true if we need to run more iterations, false if we did not apply any correction
		*/
		bool SolveVelocity(const FReal Dt, const bool bApplyDynamicFriction);

		/**
		 * @brief Run a velocity solve on the average point from all the points that received a position impulse
		 * This is used to enforce Restitution constraints without introducing rotation artefacts without
		 * adding more velocity iterations.
		 * This will only perform work if there is more than one active contact.
		*/
		void SolveVelocityAverage(const FReal Dt);

	private:
		/**
		 * @brief Apply the inverse mass scale the body with the lower level
		 * @param InvMassScale 
		*/
		void SetShockPropagationInvMassScale(const FReal InvMassScale);

		struct FState
		{
			FState()
				: SolverBodies()
				, ManifoldPoints()
				, NumManifoldPoints(0)
				, StaticFriction(0)
				, DynamicFriction(0)
				, Stiffness(1)
				, BodyEpochs{ INDEX_NONE, INDEX_NONE }
				, NumPositionSolves(0)
				, NumVelocitySolves(0)
				, bHaveRestitution(false)
				, bIsSolved(false)
			{
			}

			FConstraintSolverBody SolverBodies[MaxConstrainedBodies];
			FPBDCollisionSolverManifoldPoint ManifoldPoints[MaxPointsPerConstraint];
			int32 NumManifoldPoints;
			FReal StaticFriction;
			FReal DynamicFriction;
			FReal Stiffness;
			int32 BodyEpochs[MaxConstrainedBodies];
			int32 NumPositionSolves;
			int32 NumVelocitySolves;
			bool bHaveRestitution;
			bool bIsSolved;
		};

		FState State;
	};


	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////


	FORCEINLINE_DEBUGGABLE void CalculatePositionCorrectionWithoutFriction(
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


	FORCEINLINE_DEBUGGABLE void CalculatePositionCorrectionWithFriction(
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
		const FReal FrictionStiffness = CVars::Chaos_PBDCollisionSolver_Position_StaticFrictionStiffness;
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
		const FReal StaticFrictionLerpRate = CVars::Chaos_PBDCollisionSolver_Position_StaticFrictionLerpRate;
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

		// If we leave the friction cone, we will fall through into the non-friction impulse calculation
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

	FORCEINLINE_DEBUGGABLE void ApplyPositionCorrectionWithFriction(
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
			//if (!DX0.IsNearlyZero(CVars::Chaos_PBDCollisionSolver_Position_PositionSolverTolerance))
			{
				Body0.ApplyPositionDelta(DX0);
			}
			//if (!DR0.IsNearlyZero(CVars::Chaos_PBDCollisionSolver_Position_RotationSolverTolerance))
			{
				Body0.ApplyRotationDelta(DR0);
			}
		}
		if (Body1.IsDynamic())
		{
			const FVec3 AngularPushOut = FVec3::CrossProduct(ManifoldPoint.WorldContactPosition - Body1.P(), PushOut);
			const FVec3 DX1 = -(Body1.InvM() * PushOut);
			const FVec3 DR1 = -(Body1.InvI() * AngularPushOut);
			//if (!DX1.IsNearlyZero(CVars::Chaos_PBDCollisionSolver_Position_PositionSolverTolerance))
			{
				Body1.ApplyPositionDelta(DX1);
			}
			//if (!DR1.IsNearlyZero(CVars::Chaos_PBDCollisionSolver_Position_RotationSolverTolerance))
			{
				Body1.ApplyRotationDelta(DR1);
			}
		}
	}

	FORCEINLINE_DEBUGGABLE void ApplyPositionCorrectionNoFriction(
		const FReal Stiffness,
		const FVec3& ContactDelta,
		const FReal ContactDeltaNormal,
		FPBDCollisionSolverManifoldPoint& ManifoldPoint,
		FConstraintSolverBody& Body0,
		FConstraintSolverBody& Body1)
	{
		FVec3 PushOut = FVec3(0);

		CalculatePositionCorrectionWithoutFriction(
			Stiffness,
			ContactDeltaNormal,
			ManifoldPoint.WorldContactNormal,
			ManifoldPoint.WorldContactMassNormal,
			ManifoldPoint.NetPushOut,					// Out
			PushOut);									// Out

		// Update the particle state based on the pushout
		if (Body0.IsDynamic())
		{
			const FVec3 AngularPushOut = FVec3::CrossProduct(ManifoldPoint.WorldContactPosition - Body0.P(), PushOut);
			const FVec3 DX0 = Body0.InvM() * PushOut;
			const FVec3 DR0 = Body0.InvI() * AngularPushOut;
			//if (!DX0.IsNearlyZero(CVars::Chaos_PBDCollisionSolver_Position_PositionSolverTolerance))
			{
				Body0.ApplyPositionDelta(DX0);
			}
			//if (!DR0.IsNearlyZero(CVars::Chaos_PBDCollisionSolver_Position_RotationSolverTolerance))
			{
				Body0.ApplyRotationDelta(DR0);
			}
		}
		if (Body1.IsDynamic())
		{
			const FVec3 AngularPushOut = FVec3::CrossProduct(ManifoldPoint.WorldContactPosition - Body1.P(), PushOut);
			const FVec3 DX1 = -(Body1.InvM() * PushOut);
			const FVec3 DR1 = -(Body1.InvI() * AngularPushOut);
			//if (!DX1.IsNearlyZero(CVars::Chaos_PBDCollisionSolver_Position_PositionSolverTolerance))
			{
				Body1.ApplyPositionDelta(DX1);
			}
			//if (!DR1.IsNearlyZero(CVars::Chaos_PBDCollisionSolver_Position_RotationSolverTolerance))
			{
				Body1.ApplyRotationDelta(DR1);
			}
		}
	}

	FORCEINLINE_DEBUGGABLE void CalculateVelocityCorrectionImpulse(
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

		if ((ContactVelocityDeltaNormal > FReal(0)) && !CVars::bChaos_PBDCollisionSolver_Velocity_NegativeImpulseEnabled)
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
		if (CVars::bChaos_PBDCollisionSolver_Velocity_ImpulseClampEnabled && (Dt > 0))
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

	FORCEINLINE_DEBUGGABLE void ApplyVelocityCorrection(
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


	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////


	FORCEINLINE_DEBUGGABLE void FPBDCollisionSolverManifoldPoint::CalculateContactPositionError(const FConstraintSolverBody& Body0, const FConstraintSolverBody& Body1, const FReal MaxPushOut, FVec3& OutContactDelta, FReal& OutContactDeltaNormal) const
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

	FORCEINLINE_DEBUGGABLE void FPBDCollisionSolverManifoldPoint::CalculateContactVelocityError(const FConstraintSolverBody& Body0, const FConstraintSolverBody& Body1, const FReal DynamicFriction, const FReal Dt, FVec3& OutContactVelocityDelta, FReal& OutContactVelocityDeltaNormal) const
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


	FORCEINLINE_DEBUGGABLE bool FPBDCollisionSolver::SolvePositionWithFriction(const FReal Dt, const FReal MaxPushOut)
	{
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
				ApplyPositionCorrectionWithFriction(
					State.Stiffness,
					State.StaticFriction,
					State.DynamicFriction,
					ContactDelta,
					ContactDeltaNormal,
					SolverManifoldPoint,
					Body0,
					Body1);
			}
		}

		// We are solved if we did not move the bodies within some tolerance
		// @todo(chaos): better early-out system
		State.bIsSolved = (Body0.LastChangeEpoch() == State.BodyEpochs[0]) && (Body1.LastChangeEpoch() == State.BodyEpochs[1]);
		State.BodyEpochs[0] = Body0.LastChangeEpoch();
		State.BodyEpochs[1] = Body1.LastChangeEpoch();
		State.NumPositionSolves = State.NumPositionSolves + 1;

		return !State.bIsSolved;
	}

	FORCEINLINE_DEBUGGABLE bool FPBDCollisionSolver::SolvePositionNoFriction(const FReal Dt, const FReal MaxPushOut)
	{
		// SolverBody decorator used to add mass scaling
		FConstraintSolverBody& Body0 = SolverBody0();
		FConstraintSolverBody& Body1 = SolverBody1();

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
				ApplyPositionCorrectionNoFriction(
					State.Stiffness,
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
		State.bIsSolved = false;
		State.BodyEpochs[0] = Body0.LastChangeEpoch();
		State.BodyEpochs[1] = Body1.LastChangeEpoch();
		State.NumPositionSolves = State.NumPositionSolves + 1;

		return !State.bIsSolved;
	}

}

CHAOS_API DECLARE_LOG_CATEGORY_EXTERN(LogChaosCollision, Log, All);
