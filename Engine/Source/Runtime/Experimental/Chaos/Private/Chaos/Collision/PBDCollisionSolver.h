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
			const FVec3LP& InWorldContactNormal);

		/**
		 * @brief Initialize the material related properties of the contact
		*/
		void InitMaterial(
			const FConstraintSolverBody& Body0,
			const FConstraintSolverBody& Body1,
			const FRealLP InRestitution,
			const FRealLP InRestitutionVelocityThreshold,
			const bool bInEnableStaticFriction,
			const FRealLP InStaticFrictionMax);

		/**
		 * @brief Update the world-space relative contact points based on current body transforms and body-space contact positions
		*/
		void UpdateContact(
			const FConstraintSolverBody& Body0,
			const FConstraintSolverBody& Body1,
			const FVec3& InWorldAnchorPoint0,
			const FVec3& InWorldAnchorPoint1,
			const FVec3LP& InWorldContactNormal);

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
		void CalculateContactPositionError(const FConstraintSolverBody& Body0, const FConstraintSolverBody& Body1, const FRealLP MaxPushOut, FVec3LP& OutContactDelta, FRealLP& OutContactDeltaNormal) const;

		/**
		 * @brief Calculate the velocity error at the current transforms
		*/
		void CalculateContactVelocityError(const FConstraintSolverBody& Body0, const FConstraintSolverBody& Body1, const FRealLP DynamicFriction, const FRealLP Dt, FVec3LP& OutContactVelocityDelta, FRealLP& OutContactVelocityDeltaNormal) const;

		// @todo(chaos): make private
	public:
		friend class FPBDCollisionSolver;

		// Contact mass (for friction)
		FMatrix33LP WorldContactMass;

		// World-space body-relative contact points
		FVec3LP RelativeContactPosition0;
		FVec3LP RelativeContactPosition1;

		// World-space contact normal
		FVec3LP WorldContactNormal;

		// I^-1.(R x N) for each body
		FVec3LP WorldContactNormalAngular0;
		FVec3LP WorldContactNormalAngular1;

		// Contact mass (for non-friction)
		FRealLP WorldContactMassNormal;

		// Initial world-space contact separation that we are trying to correct
		FVec3LP WorldContactDelta;

		// Desired final normal velocity, taking Restitution into account
		FRealLP WorldContactVelocityTargetNormal;

		// Solver outputs
		FVec3LP NetPushOut;
		FVec3LP NetImpulse;

		// A smoothed NetImpulse along the normal, used for clipping to the static friction cone
		FRealLP StaticFrictionMax;

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

		FRealLP StaticFriction() const { return State.StaticFriction; }
		FRealLP DynamicFriction() const { return State.DynamicFriction; }

		void SetFriction(const FReal InStaticFriction, const FReal InDynamicFriction)
		{
			State.StaticFriction = FRealLP(InStaticFriction);
			State.DynamicFriction = FRealLP(InDynamicFriction);
		}

		void SetStiffness(const FReal InStiffness)
		{
			State.Stiffness = FRealLP(InStiffness);
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
		bool SolvePositionWithFriction(const FRealLP Dt, const FRealLP MaxPushOut);
		bool SolvePositionNoFriction(const FRealLP Dt, const FRealLP MaxPushOut);

		/**
		 * @brief Calculate and apply the velocity correction for this iteration
		 * @return true if we need to run more iterations, false if we did not apply any correction
		*/
		bool SolveVelocity(const FRealLP Dt, const bool bApplyDynamicFriction);

	private:
		/**
		 * @brief Apply the inverse mass scale the body with the lower level
		 * @param InvMassScale 
		*/
		void SetShockPropagationInvMassScale(const FRealLP InvMassScale);

		/**
		 * @brief Run a velocity solve on the average point from all the points that received a position impulse
		 * This is used to enforce Restitution constraints without introducing rotation artefacts without
		 * adding more velocity iterations.
		 * This will only perform work if there is more than one active contact.
		*/
		void SolveVelocityAverage(const FRealLP Dt);

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

			FRealLP StaticFriction;
			FRealLP DynamicFriction;
			FRealLP Stiffness;
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


	FORCEINLINE_DEBUGGABLE void CalculatePositionCorrectionWithoutFrictionV(
		const FRealLP Stiffness,
		const FRealLP ContactDeltaNormal,
		const FVec3LP& ContactNormal,
		const FRealLP ContactMassNormal,
		FVec3LP& InOutNetPushOut,
		FVec3LP& OutPushOut)
	{
		FVec3LP PushOut = -(Stiffness * ContactDeltaNormal * ContactMassNormal) * ContactNormal;

		// The total pushout so far this sub-step
		// We allow negative incremental impulses, but not net negative impulses
		FVec3LP NetPushOut = InOutNetPushOut + PushOut;
		const FRealLP NetPushOutNormal = FVec3LP::DotProduct(NetPushOut, ContactNormal);
		if (NetPushOutNormal < 0)
		{
			PushOut = -InOutNetPushOut;
			NetPushOut = FVec3LP(0);
		}

		InOutNetPushOut = NetPushOut;
		OutPushOut = PushOut;
	}


	FORCEINLINE_DEBUGGABLE void CalculatePositionCorrectionWithoutFrictionF(
		const FRealLP Stiffness,
		const FRealLP ContactDeltaNormal,
		const FRealLP ContactMassNormal,
		const FRealLP NetPushOutNormal,
		FRealLP& OutPushOutNormal)
	{
		FRealLP PushOutNormal = -(Stiffness * ContactDeltaNormal * ContactMassNormal);

		// The total pushout so far this sub-step
		// We allow negative incremental impulses, but not net negative impulses
		if (NetPushOutNormal + PushOutNormal < 0)
		{
			PushOutNormal = -NetPushOutNormal;
		}

		OutPushOutNormal = PushOutNormal;
	}


	FORCEINLINE_DEBUGGABLE void CalculatePositionCorrectionWithFriction(
		const FRealLP Stiffness,
		const FVec3LP& ContactDelta,
		const FRealLP ContactDeltaNormal,
		const FVec3LP& ContactNormal,
		const FMatrix33LP& ContactMass,
		const FRealLP StaticFriction,
		const FRealLP DynamicFriction,
		FVec3LP& InOutNetPushOut,
		FVec3LP& OutPushOut,
		FRealLP& InOutStaticFrictionMax,
		bool& bOutInsideStaticFrictionCone)
	{
		// If static friction is enabled, calculate the correction to move the contact point back to its
		// original relative location on all axes.
		// @todo(chaos): this should be moved to the ManifoldPoint error calculation?
		FVec3LP ModifiedContactError = -ContactDelta;
		const FRealLP FrictionStiffness = CVars::Chaos_PBDCollisionSolver_Position_StaticFrictionStiffness;
		if (FrictionStiffness < 1.0f)
		{
			const FVec3LP ContactDeltaTangent = ContactDelta - ContactDeltaNormal * ContactNormal;
			ModifiedContactError = -ContactDeltaNormal * ContactNormal - FrictionStiffness * ContactDeltaTangent;
		}

		FVec3LP PushOut = Stiffness * ContactMass * ModifiedContactError;

		// If we ended up with a negative normal pushout, remove all correction from this point
		FVec3LP NetPushOut = InOutNetPushOut + PushOut;
		const FRealLP NetPushOutNormal = FVec3LP::DotProduct(NetPushOut, ContactNormal);
		bool bInsideStaticFrictionCone = true;
		if (NetPushOutNormal < FRealLP(SMALL_NUMBER))
		{
			bInsideStaticFrictionCone = false;
			PushOut = -InOutNetPushOut;
			NetPushOut = FVec3LP(0);
		}

		// Static friction limit: immediately increase maximum lateral correction, but smoothly decay maximum static friction limit. 
		// This is so that small variations in position (jitter) and therefore NetPushOutNormal don't cause static friction to slip
		// @todo(chaos): StaticFriction smoothing is iteration count depenendent - try to make it not so
		const FRealLP StaticFrictionLerpRate = CVars::Chaos_PBDCollisionSolver_Position_StaticFrictionLerpRate;
		const FRealLP StaticFrictionDest = FMath::Max(NetPushOutNormal, FRealLP(0));
		FRealLP StaticFrictionMax = FMath::Lerp(FMath::Max(InOutStaticFrictionMax, StaticFrictionDest), StaticFrictionDest, StaticFrictionLerpRate);

		// If we exceed the friction cone, stop adding frictional corrections and clip correction to cone
		// @todo(chaos): clamp to dynamic friction
		if (bInsideStaticFrictionCone)
		{
			const FRealLP MaxPushOutTangentSq = FMath::Square(StaticFriction * StaticFrictionMax);
			const FVec3LP NetPushOutTangent = (NetPushOut - NetPushOutNormal * ContactNormal);
			const FRealLP NetPushOutTangentSq = NetPushOutTangent.SizeSquared();
			if (NetPushOutTangentSq > MaxPushOutTangentSq)
			{
				NetPushOut = NetPushOutNormal * ContactNormal + (StaticFriction * StaticFrictionMax) * NetPushOutTangent * FMath::InvSqrt(NetPushOutTangentSq);
				PushOut = NetPushOut - InOutNetPushOut;
				bInsideStaticFrictionCone = false;
			}
		}

		InOutNetPushOut = NetPushOut;
		OutPushOut = PushOut;
		InOutStaticFrictionMax = StaticFrictionMax;
		bOutInsideStaticFrictionCone = bInsideStaticFrictionCone;
	}

	FORCEINLINE_DEBUGGABLE void ApplyPositionCorrectionWithFriction(
		const FRealLP Stiffness,
		const FRealLP StaticFriction,
		const FRealLP DynamicFriction,
		const FVec3LP& ContactDelta,
		const FRealLP ContactDeltaNormal,
		FPBDCollisionSolverManifoldPoint& ManifoldPoint,
		FConstraintSolverBody& Body0,
		FConstraintSolverBody& Body1)
	{
		FVec3LP PushOut = FVec3LP(0);

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
			CalculatePositionCorrectionWithoutFrictionV(
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
			const FVec3LP AngularPushOut = FVec3LP::CrossProduct(ManifoldPoint.RelativeContactPosition0, PushOut);
			const FVec3LP DX0 = Body0.InvM() * PushOut;
			const FVec3LP DR0 = Body0.InvI() * AngularPushOut;
			Body0.ApplyPositionDelta(DX0);
			Body0.ApplyRotationDelta(DR0);
		}
		if (Body1.IsDynamic())
		{
			const FVec3LP AngularPushOut = FVec3LP::CrossProduct(ManifoldPoint.RelativeContactPosition1, PushOut);
			const FVec3LP DX1 = -(Body1.InvM() * PushOut);
			const FVec3LP DR1 = -(Body1.InvI() * AngularPushOut);
			Body1.ApplyPositionDelta(DX1);
			Body1.ApplyRotationDelta(DR1);
		}
	}

	FORCEINLINE_DEBUGGABLE void ApplyPositionCorrectionNoFriction(
		const FRealLP Stiffness,
		const FRealLP ContactDeltaNormal,
		FPBDCollisionSolverManifoldPoint& ManifoldPoint,
		FConstraintSolverBody& Body0,
		FConstraintSolverBody& Body1)
	{
		FRealLP PushOutNormal = FRealLP(0);
		const FRealLP NetPushOutNormal = FVec3LP::DotProduct(ManifoldPoint.NetPushOut, ManifoldPoint.WorldContactNormal);

		CalculatePositionCorrectionWithoutFrictionF(
			Stiffness,
			ContactDeltaNormal,
			ManifoldPoint.WorldContactMassNormal,
			NetPushOutNormal,
			PushOutNormal);									// Out

		ManifoldPoint.NetPushOut += PushOutNormal * ManifoldPoint.WorldContactNormal;

		// Update the particle state based on the pushout
		if (Body0.IsDynamic())
		{
			const FVec3LP DX0 = (Body0.InvM() * PushOutNormal) * ManifoldPoint.WorldContactNormal;
			const FVec3LP DR0 = ManifoldPoint.WorldContactNormalAngular0 * PushOutNormal;
			Body0.ApplyPositionDelta(DX0);
			Body0.ApplyRotationDelta(DR0);
		}
		if (Body1.IsDynamic())
		{
			const FVec3LP DX1 = (Body1.InvM() * -PushOutNormal) * ManifoldPoint.WorldContactNormal;
			const FVec3LP DR1 = ManifoldPoint.WorldContactNormalAngular1 * -PushOutNormal;
			Body1.ApplyPositionDelta(DX1);
			Body1.ApplyRotationDelta(DR1);
		}
	}

	FORCEINLINE_DEBUGGABLE void CalculateVelocityCorrectionImpulse(
		const FRealLP Stiffness,
		const FRealLP Dt,
		const FRealLP DynamicFriction,
		const FVec3LP& ContactNormal,
		const FMatrix33LP& ContactMass,
		const FRealLP ContactMassNormal,
		const FVec3LP& ContactVelocityDelta,
		const FRealLP ContactVelocityDeltaNormal,
		const FVec3LP& NetPushOut,
		FVec3LP& InOutNetImpulse,
		FVec3LP& OutImpulse)
	{
		FVec3LP Impulse = FVec3LP(0);

		if ((ContactVelocityDeltaNormal > FRealLP(0)) && !CVars::bChaos_PBDCollisionSolver_Velocity_NegativeImpulseEnabled)
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
			const FVec3LP NetImpulse = InOutNetImpulse + Impulse;
			const FRealLP PushOutImpulseNormal = FMath::Max(0.0f, FVec3LP::DotProduct(NetPushOut, ContactNormal) / Dt);
			const FRealLP NetImpulseNormal = FVec3LP::DotProduct(NetImpulse, ContactNormal);
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
		const FRealLP Stiffness,
		const FRealLP Dt,
		const FRealLP DynamicFriction,
		const FVec3LP& ContactVelocityDelta,
		const FRealLP ContactVelocityDeltaNormal,
		FPBDCollisionSolverManifoldPoint& ManifoldPoint,
		FConstraintSolverBody& Body0,
		FConstraintSolverBody& Body1)
	{
		FVec3LP Impulse = FVec3LP(0);

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
			const FVec3LP AngularImpulse = FVec3LP::CrossProduct(ManifoldPoint.RelativeContactPosition0, Impulse);
			Body0.ApplyVelocityDelta(Body0.InvM() * Impulse, Body0.InvI() * AngularImpulse);
		}
		if (Body1.IsDynamic())
		{
			const FVec3LP AngularImpulse = FVec3LP::CrossProduct(ManifoldPoint.RelativeContactPosition1, Impulse);
			Body1.ApplyVelocityDelta(Body1.InvM() * -Impulse, Body1.InvI() * -AngularImpulse);
		}
	}


	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////


	FORCEINLINE_DEBUGGABLE void FPBDCollisionSolverManifoldPoint::CalculateContactPositionError(const FConstraintSolverBody& Body0, const FConstraintSolverBody& Body1, const FRealLP MaxPushOut, FVec3LP& OutContactDelta, FRealLP& OutContactDeltaNormal) const
	{
#if CHAOS_NONLINEAR_COLLISIONS_ENABLED
		// Non-linear version: calculate the contact delta after we have converted the current positional impulses into position and rotation corrections.
		// We could precalculate and store the LocalContactPositions if we really want to use this nonlinear version
		const FVec3 LocalContactPosition0 = Body0.Q().Inverse() * WorldRelativeContactPosition0;
		const FVec3 LocalContactPosition1 = Body1.Q().Inverse() * WorldRelativeContactPosition1;
		OutContactDelta = (Body0.CorrectedP() + Body0.CorrectedQ() * LocalContactPosition0) - (Body1.CorrectedP() + Body1.CorrectedQ() * LocalContactPosition1);
#else
		// Linear version: calculate the contact delta assuming linear motion after applying a positional impulse at the contact point. There will be an error that depends on the size of the rotation.
		const FVec3LP ContactDelta0 = FVec3LP(Body0.DP()) + FVec3LP::CrossProduct(Body0.DQ(), RelativeContactPosition0);
		const FVec3LP ContactDelta1 = FVec3LP(Body1.DP()) + FVec3LP::CrossProduct(Body1.DQ(), RelativeContactPosition1);
		OutContactDelta = WorldContactDelta + ContactDelta0 - ContactDelta1;
#endif
		OutContactDeltaNormal = FVec3LP::DotProduct(OutContactDelta, WorldContactNormal);

		// NOTE: OutContactDeltaNormal is negative for penetration
		// NOTE: MaxPushOut == 0 disables the pushout limits
		if ((MaxPushOut > 0) && (OutContactDeltaNormal < -MaxPushOut))
		{
			const FRealLP ClampedContactDeltaNormal = -MaxPushOut;
			OutContactDelta += (ClampedContactDeltaNormal - OutContactDeltaNormal) * WorldContactNormal;
			OutContactDeltaNormal = ClampedContactDeltaNormal;
		}
	}

	FORCEINLINE_DEBUGGABLE void FPBDCollisionSolverManifoldPoint::CalculateContactVelocityError(const FConstraintSolverBody& Body0, const FConstraintSolverBody& Body1, const FRealLP DynamicFriction, const FRealLP Dt, FVec3LP& OutContactVelocityDelta, FRealLP& OutContactVelocityDeltaNormal) const
	{
		const FVec3LP ContactVelocity0 = Body0.V() + FVec3LP::CrossProduct(Body0.W(), RelativeContactPosition0);
		const FVec3LP ContactVelocity1 = Body1.V() + FVec3LP::CrossProduct(Body1.W(), RelativeContactPosition1);
		const FVec3LP ContactVelocity = ContactVelocity0 - ContactVelocity1;
		const FRealLP ContactVelocityNormal = FVec3LP::DotProduct(ContactVelocity, WorldContactNormal);

		// Target normal velocity, including restitution
		const FRealLP ContactVelocityTargetNormal = WorldContactVelocityTargetNormal;

		// Add up the errors in the velocity (current velocity - desired velocity)
		OutContactVelocityDeltaNormal = (ContactVelocityNormal - WorldContactVelocityTargetNormal);
		OutContactVelocityDelta = (ContactVelocityNormal - ContactVelocityTargetNormal) * WorldContactNormal;

		const bool bApplyFriction = (DynamicFriction > 0) && (Dt > 0);
		if (bApplyFriction)
		{
			const FVec3LP ContactVelocityTangential = ContactVelocity - ContactVelocityNormal * WorldContactNormal;
			const FRealLP ContactVelocityTangentialLen = ContactVelocityTangential.Size();
			if (ContactVelocityTangentialLen > SMALL_NUMBER)
			{
				// PushOut = ContactMass * DP, where DP is the contact positional correction
				// Friction force is proportional to the normal force, so friction velocity correction
				// is proprtional to normal velocity correction, or DVn = DPn/dt = PushOut.N / (ContactMass * dt);
				const FRealLP PushOutNormal = FVec3LP::DotProduct(NetPushOut, WorldContactNormal);
				const FRealLP DynamicFrictionVelocityError = PushOutNormal / (WorldContactMassNormal * Dt);
				if (DynamicFrictionVelocityError > SMALL_NUMBER)
				{
					const FRealLP ContactVelocityErrorTangential = FMath::Min(DynamicFriction * DynamicFrictionVelocityError, ContactVelocityTangentialLen);
					OutContactVelocityDelta += ContactVelocityTangential * (ContactVelocityErrorTangential / ContactVelocityTangentialLen);
				}
			}
		}
	}


	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////////////////////////////


	FORCEINLINE_DEBUGGABLE bool FPBDCollisionSolver::SolvePositionWithFriction(const FRealLP Dt, const FRealLP MaxPushOut)
	{
		// SolverBody decorator used to add mass scaling
		FConstraintSolverBody& Body0 = SolverBody0();
		FConstraintSolverBody& Body1 = SolverBody1();

		// Apply the position correction so that all contacts have zero separation
		for (int32 PointIndex = 0; PointIndex < State.NumManifoldPoints; ++PointIndex)
		{
			FPBDCollisionSolverManifoldPoint& SolverManifoldPoint = State.ManifoldPoints[PointIndex];

			FVec3LP ContactDelta;
			FRealLP ContactDeltaNormal;
			SolverManifoldPoint.CalculateContactPositionError(Body0.SolverBody(), Body1.SolverBody(), MaxPushOut, ContactDelta, ContactDeltaNormal);

			const bool bProcessManifoldPoint = (ContactDeltaNormal < FRealLP(0)) || !SolverManifoldPoint.NetPushOut.IsNearlyZero();
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

		return false;
	}

	FORCEINLINE_DEBUGGABLE bool FPBDCollisionSolver::SolvePositionNoFriction(const FRealLP Dt, const FRealLP MaxPushOut)
	{
		// SolverBody decorator used to add mass scaling
		FConstraintSolverBody& Body0 = SolverBody0();
		FConstraintSolverBody& Body1 = SolverBody1();

		// Apply the position correction so that all contacts have zero separation
		for (int32 PointIndex = 0; PointIndex < State.NumManifoldPoints; ++PointIndex)
		{
			FPBDCollisionSolverManifoldPoint& SolverManifoldPoint = State.ManifoldPoints[PointIndex];

			FVec3LP ContactDelta;
			FRealLP ContactDeltaNormal;
			SolverManifoldPoint.CalculateContactPositionError(Body0.SolverBody(), Body1.SolverBody(), MaxPushOut, ContactDelta, ContactDeltaNormal);

			const bool bProcessManifoldPoint = (ContactDeltaNormal < FRealLP(0)) || !SolverManifoldPoint.NetPushOut.IsNearlyZero();
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
