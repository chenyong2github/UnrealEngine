// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/Collision/CollisionApplyType.h"
#include "Chaos/Collision/PBDCollisionSolverSimd.h"
#include "Chaos/Collision/PBDCollisionSolverSettings.h"
#include "Chaos/Evolution/SolverConstraintContainer.h"

namespace Chaos
{
	class FPBDCollisionConstraint;
	class FPBDCollisionConstraints;
	class FSolverBody;
	class FSolverBodyContainer;

	namespace Private
	{
		class FPBDIsland;
		class FPBDIslandConstraint;

		/**
		 * The solver for a set of collision constraints. This collects all the data required to solve a set of collision
		 * constraints into a contiguous, ordered buffer.
		 * 
		 * This version runs a Gauss-Seidel outer loop over manifolds, and a Jacobi loop over contacts in each manifold.
		*/
		class FPBDCollisionContainerSolverSimd : public FConstraintContainerSolver
		{
		public:
			UE_NONCOPYABLE(FPBDCollisionContainerSolverSimd);

			FPBDCollisionContainerSolverSimd(const FPBDCollisionConstraints& InConstraintContainer, const int32 InPriority);
			~FPBDCollisionContainerSolverSimd();

			virtual void Reset(const int32 InMaxCollisions) override final;

			virtual int32 GetNumConstraints() const override final { return NumConstraints; }

			//
			// IslandGroup API
			//
			virtual void AddConstraints() override final;
			virtual void AddConstraints(const TArrayView<Private::FPBDIslandConstraint>& ConstraintHandles) override final;
			virtual void AddBodies(FSolverBodyContainer& SolverBodyContainer) override final;
			virtual void GatherInput(const FReal Dt) override final;
			virtual void GatherInput(const FReal Dt, const int32 BeginIndex, const int32 EndIndex) override final;
			virtual void ScatterOutput(const FReal Dt) override final;
			virtual void ScatterOutput(const FReal Dt, const int32 BeginIndex, const int32 EndIndex) override final;
			virtual void ApplyPositionConstraints(const FReal Dt, const int32 It, const int32 NumIts) override final;
			virtual void ApplyVelocityConstraints(const FReal Dt, const int32 It, const int32 NumIts) override final;
			virtual void ApplyProjectionConstraints(const FReal Dt, const int32 It, const int32 NumIts) override final;

			template<int TNumLanes>
			struct TConstraintIndexSimd
			{
				TConstraintIndexSimd()
				{
					for (int32 LaneIndex = 0; LaneIndex < TNumLanes; ++LaneIndex)
					{
						ConstraintIndex[LaneIndex] = INDEX_NONE;
					}
				}

				int32 ConstraintIndex[TNumLanes];
			};

			// Rows of collision constraints and solvers. 
			// We solve each row using SIMD. 
			// NumLanes is the width of a row and will be the same as the float SIMD register width (or less).
			template<int TNumLanes>
			struct FDataSimd
			{
				FDataSimd()
					: SimdNumConstraints{ 0, }
					, SimdNumManifoldPoints{ 0, }
				{
				}

				int32 SimdNumConstraints[TNumLanes];
				int32 SimdNumManifoldPoints[TNumLanes];

				TArray<TSolverBodyPtrPairSimd<TNumLanes>> SimdSolverBodies;
				TArray<TConstraintIndexSimd<TNumLanes>> SimdConstraintIndices;
				TArray<TPBDCollisionSolverManifoldPointsSimd<TNumLanes>> SimdManifoldPoints;
			};

			// For testing
			const FPBDCollisionSolverSimd& GetConstraintSolver(const int32 ConstraintIndex) const { return Solvers[ConstraintIndex]; }
			const TPBDCollisionSolverManifoldPointsSimd<4>& GetManifoldPointSolver(const int32 RowIndex) const { return Data.SimdManifoldPoints[RowIndex]; }

		private:
			int32 GetNumLanes() const { return 4; }
			void CreateSolvers();
			void UpdatePositionShockPropagation(const FReal Dt, const int32 It, const int32 NumIts, const FPBDCollisionSolverSettings& SolverSettings);
			void UpdateVelocityShockPropagation(const FReal Dt, const int32 It, const int32 NumIts, const FPBDCollisionSolverSettings& SolverSettings);
			void UpdateCollisions(const FReal InDt);

			const FPBDCollisionConstraints& ConstraintContainer;

			TArray<FPBDCollisionConstraint*> Constraints;
			TArray<FPBDCollisionSolverSimd> Solvers;
			FDataSimd<4> Data;
			int32 NumConstraints;

			FSolverBody DummySolverBody;
			FConstraintSolverBody DummyConstraintSolverBody;
		};

	}	// namespace Private
}	// namespace Chaos
