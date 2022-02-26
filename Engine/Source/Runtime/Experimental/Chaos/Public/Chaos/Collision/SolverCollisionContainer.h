// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/Collision/CollisionApplyType.h"
#include "Chaos/Evolution/SolverConstraintContainer.h"

namespace Chaos
{
	namespace Collisions
	{
		struct FContactParticleParameters;
		struct FContactIterationParameters;
	}

	class FPBDCollisionConstraint;
	class FPBDCollisionSolver;
	class FPBDCollisionSolverAdapter;
	class FPBDCollisionSolverManifoldPoint;
	class FSolverBodyContainer;

	/**
	 * @brief Settings to control the low-level collision solver behaviour
	*/
	class FPBDCollisionSolverSettings
	{
	public:
		FPBDCollisionSolverSettings();

		// Maximum speed at which two objects can depenetrate (actually, how much relative velocity can be added
		// to a contact per frame when depentrating. Stacks and deep penetrations can lead to larger velocities)
		FReal MaxPushOutVelocity;

		// How many of the position iterations should run static/dynamic friction
		int32 NumPositionFrictionIterations;

		// How many of the velocity iterations should run dynamic friction
		// @todo(chaos): if NumVelocityFrictionIterations > 1, then dynamic friction in the velocity phase will be iteration 
		// count dependent (velocity-solve friction is currentlyused by quadratic shapes and RBAN)
		int32 NumVelocityFrictionIterations;

		// How many position iterations should have shock propagation enabled
		int32 NumPositionShockPropagationIterations;

		// How many velocity iterations should have shock propagation enabled
		int32 NumVelocityShockPropagationIterations;
	};

	/**
	 * @brief A container of low-level data used to solve collision constraints
	*/
	class FPBDCollisionSolverContainer : public FConstraintSolverContainer
	{
	public:
		FPBDCollisionSolverContainer();
		~FPBDCollisionSolverContainer();

		int32 NumSolvers() const { return CollisionSolvers.Num(); }

		virtual void Reset(const int32 InMaxCollisions) override;

		/** Set the number of collision solvers in the container
		 * @param MaxCollisions Max number of collision solvers
		 */
		void SetNum(const int32 MaxCollisions);

		// Add a solver constraint for the specified constraint and gather the required solver data
		void PreAddConstraintSolver(FPBDCollisionConstraint& Constraint, FSolverBodyContainer& SolverBodyContainer, int32& ConstraintIndex);
		void AddConstraintSolver(FReal Dt, FPBDCollisionConstraint& Constraint, const int32 Particle0Level, const int32 Particle1Level, FSolverBodyContainer& SolverBodyContainer, const FPBDCollisionSolverSettings& SolverSettings);

		bool SolvePositionSerial(const FReal Dt, const int32 It, const int32 NumIts, const int32 BeginIndex, const int32 EndIndex, const FPBDCollisionSolverSettings& SolverSettings);
		bool SolvePositionParallel(const FReal Dt, const int32 It, const int32 NumIts, const int32 BeginIndex, const int32 EndIndex, const FPBDCollisionSolverSettings& SolverSettings);

		bool SolveVelocitySerial(const FReal Dt, const int32 It, const int32 NumIts, const int32 BeginIndex, const int32 EndIndex, const FPBDCollisionSolverSettings& SolverSettings);
		bool SolveVelocityParallel(const FReal Dt, const int32 It, const int32 NumIts, const int32 BeginIndex, const int32 EndIndex, const FPBDCollisionSolverSettings& SolverSettings);

		void ScatterOutput(const FReal Dt, const int32 BeginIndex, const int32 EndIndex);

	private:
		void UpdatePositionShockPropagation(const FReal Dt, const int32 It, const int32 NumIts, const int32 BeginIndex, const int32 EndIndex, const FPBDCollisionSolverSettings& SolverSettings);
		void UpdateVelocityShockPropagation(const FReal Dt, const int32 It, const int32 NumIts, const int32 BeginIndex, const int32 EndIndex, const FPBDCollisionSolverSettings& SolverSettings);
		bool SolvePositionImpl(const FReal Dt, const int32 It, const int32 NumIts, const int32 BeginIndex, const int32 EndIndex, const FPBDCollisionSolverSettings& SolverSettings, const bool bParallel);
		bool SolvePositionIncrementalImpl(const FReal Dt, const int32 BeginIndex, const int32 EndIndex, const FReal MaxPushOut, const bool bApplyStaticFriction);
		bool SolvePositionWithFrictionImpl(const FReal Dt, const int32 BeginIndex, const int32 EndIndex, const FReal MaxPushOut, const bool bParallel);
		bool SolvePositionNoFrictionImpl(const FReal Dt, const int32 BeginIndex, const int32 EndIndex, const FReal MaxPushOut, const bool bParallel);
		bool SolveVelocityImpl(const FReal Dt, const int32 It, const int32 NumIts, const int32 BeginIndex, const int32 EndIndex, const FPBDCollisionSolverSettings& SolverSettings, const bool bParallel);
		void ScatterOutputImpl(const FReal Dt, const int32 BeginIndex, const int32 EndIndex, const bool bParallel);

		TArray<FPBDCollisionSolverAdapter> CollisionSolvers;
		bool bRequiresIncrementalCollisionDetection;
	};
}
