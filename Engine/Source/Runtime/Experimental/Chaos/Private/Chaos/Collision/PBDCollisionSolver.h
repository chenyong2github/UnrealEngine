// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

#include "Chaos/Core.h"
#include "Chaos/Defines.h"
#include "Chaos/Evolution/SolverBody.h"

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
		bool SolvePosition(const FReal Dt, const FReal MaxPushOut, const bool bApplyStaticFriction);

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
}

CHAOS_API DECLARE_LOG_CATEGORY_EXTERN(LogChaosCollision, Log, All);
