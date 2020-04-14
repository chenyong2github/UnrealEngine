// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Chaos/Array.h"
#include "Chaos/Core.h"
#include "Chaos/Transform.h"
#include "Chaos/Vector.h"

#include "Chaos/ConstraintHandle.h"
#include "Chaos/Joint/JointSolverConstraints.h"
#include "Chaos/Joint/PBDJointSolverGaussSeidel.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/PBDConstraintContainer.h"
#include "Chaos/PBDJointConstraintTypes.h"

namespace Chaos
{
	class FJointSolverConstraints;
	class FJointSolverGaussSeidel;
	class FJointSolverResult;

	class CHAOS_API FPBDJointConstraintHandle : public TContainerConstraintHandle<FPBDJointConstraints>
	{
	public:
		using Base = TContainerConstraintHandle<FPBDJointConstraints>;
		using FConstraintContainer = FPBDJointConstraints;

		FPBDJointConstraintHandle();
		FPBDJointConstraintHandle(FConstraintContainer* InConstraintContainer, int32 InConstraintIndex);

		void CalculateConstraintSpace(FVec3& OutXa, FMatrix33& OutRa, FVec3& OutXb, FMatrix33& OutRb) const;
		int32 GetConstraintIsland() const;
		int32 GetConstraintLevel() const;
		int32 GetConstraintColor() const;
		int32 GetConstraintBatch() const;

		const FPBDJointSettings& GetSettings() const;
		void SetSettings(const FPBDJointSettings& Settings);
		TVector<TGeometryParticleHandle<float,3>*, 2> GetConstrainedParticles() const;

	protected:
		using Base::ConstraintIndex;
		using Base::ConstraintContainer;
	};

	class CHAOS_API FPBDJointState
	{
	public:
		FPBDJointState();

		int32 Batch;
		int32 Island;
		int32 Level;
		int32 Color;
		int32 IslandSize;
	};

	/**
	 * A joint restricting up to 6 degrees of freedom, with linear and angular limits.
	 */
	class CHAOS_API FPBDJointConstraints : public FPBDConstraintContainer
	{
	public:
		using Base = FPBDConstraintContainer;

		using FConstraintContainerHandle = FPBDJointConstraintHandle;
		using FConstraintHandleAllocator = TConstraintHandleAllocator<FPBDJointConstraints>;
		using FParticlePair = TVector<TGeometryParticleHandle<FReal, 3>*, 2>;
		using FVectorPair = TVector<FVec3, 2>;
		using FTransformPair = TVector<FRigidTransform3, 2>;
		using FHandles = TArray<FConstraintContainerHandle*>;

		FPBDJointConstraints(const FPBDJointSolverSettings& InSettings = FPBDJointSolverSettings());

		virtual ~FPBDJointConstraints();

		const FPBDJointSolverSettings& GetSettings() const;
		void SetSettings(const FPBDJointSolverSettings& InSettings);

		void SetNumPairIterations(const int32 NumPairIterationss) { Settings.ApplyPairIterations = NumPairIterationss; }
		void SetNumPushOutPairIterations(const int32 NumPairIterationss) { Settings.ApplyPushOutPairIterations = NumPairIterationss; }

		//
		// Constraint Container API
		//

		/**
		 * Get the number of constraints.
		 */
		int32 NumConstraints() const;

		/**
		 * Add a constraint with particle-space constraint offsets.
		 */
		FConstraintContainerHandle* AddConstraint(const FParticlePair& InConstrainedParticles, const FRigidTransform3& WorldConstraintFrame);
		FConstraintContainerHandle* AddConstraint(const FParticlePair& InConstrainedParticles, const FTransformPair& ConstraintFrames);
		FConstraintContainerHandle* AddConstraint(const FParticlePair& InConstrainedParticles, const FTransformPair& ConstraintFrames, const FPBDJointSettings& InConstraintSettings);

		/**
		 * Remove the specified constraint.
		 */
		void RemoveConstraint(int ConstraintIndex);

		// @todo(ccaulfield): rename/remove  this
		void RemoveConstraints(const TSet<TGeometryParticleHandle<FReal, 3>*>& RemovedParticles);

		void SetPreApplyCallback(const FJointPostApplyCallback& Callback);
		void ClearPreApplyCallback();

		void SetPostApplyCallback(const FJointPostApplyCallback& Callback);
		void ClearPostApplyCallback();

		void SetPostProjectCallback(const FJointPostApplyCallback& Callback);
		void ClearPostProjectCallback();

		//
		// Constraint API
		//
		FHandles& GetConstraintHandles()
		{
			return Handles;
		}
		const FHandles& GetConstConstraintHandles() const
		{
			return Handles;
		}

		const FConstraintContainerHandle* GetConstraintHandle(int32 ConstraintIndex) const;
		FConstraintContainerHandle* GetConstraintHandle(int32 ConstraintIndex);

		/**
		 * Get the particles that are affected by the specified constraint.
		 */
		const FParticlePair& GetConstrainedParticles(int32 ConstraintIndex) const;

		const FPBDJointSettings& GetConstraintSettings(int32 ConstraintIndex) const;
		void SetConstraintSettings(int32 ConstraintIndex, const FPBDJointSettings& InConstraintSettings);

		int32 GetConstraintIsland(int32 ConstraintIndex) const;
		int32 GetConstraintLevel(int32 ConstraintIndex) const;
		int32 GetConstraintColor(int32 ConstraintIndex) const;
		int32 GetConstraintBatch(int32 ConstraintIndex) const;

		//
		// General Rule API
		//

		void PrepareTick();

		void UnprepareTick();

		void PrepareIteration(FReal Dt);

		void UnprepareIteration(FReal Dt);

		void UpdatePositionBasedState(const FReal Dt);

		//
		// Simple Rule API
		//

		bool Apply(const FReal Dt, const int32 It, const int32 NumIts);
		bool ApplyPushOut(const FReal Dt, const int32 It, const int32 NumIts);

		//
		// Island Rule API
		//

		bool Apply(const FReal Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const int32 It, const int32 NumIts);
		bool ApplyPushOut(const FReal Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const int32 It, const int32 NumIts);


	protected:
		using Base::GetConstraintIndex;
		using Base::SetConstraintIndex;

	private:
		friend class FPBDJointConstraintHandle;

		void GetConstrainedParticleIndices(const int32 ConstraintIndex, int32& Index0, int32& Index1) const;
		void CalculateConstraintSpace(int32 ConstraintIndex, FVec3& OutX0, FMatrix33& OutR0, FVec3& OutX1, FMatrix33& OutR1) const;
		void UpdateParticleState(TPBDRigidParticleHandle<FReal, 3>* Rigid, const FReal Dt, const FVec3& PrevP, const FRotation3& PrevQ, const FVec3& P, const FRotation3& Q, const bool bUpdateVelocity = true);
		void UpdateParticleStateExplicit(TPBDRigidParticleHandle<FReal, 3>* Rigid, const FReal Dt, const FVec3& P, const FRotation3& Q, const FVec3& V, const FVec3& W);
		
		void InitSolverJointData();
		void DeinitSolverJointData();
		void GatherSolverJointState(int32 ConstraintIndex);
		void ScatterSolverJointState(const FReal Dt, int32 ConstraintIndex);

		void ColorConstraints();
		void SortConstraints();
		void BatchConstraints();
		void CheckBatches();

		int32 ApplyBatch(const FReal Dt, const int32 BatchIndex, const int32 NumPairIts, const int32 It, const int32 NumIts);
		int32 ApplySingle(const FReal Dt, const int32 ConstraintIndex, const int32 NumPairIts, const int32 It, const int32 NumIts);
		int32 ApplyPushOutSingle(const FReal Dt, const int32 ConstraintIndex, const int32 NumPairIts, const int32 It, const int32 NumIts);

		FPBDJointSolverSettings Settings;

		TArray<FPBDJointSettings> ConstraintSettings;
		TArray<FTransformPair> ConstraintFrames;
		TArray<FParticlePair> ConstraintParticles;
		TArray<FPBDJointState> ConstraintStates;

		FHandles Handles;
		FConstraintHandleAllocator HandleAllocator;
		bool bJointsDirty;
		bool bIsBatched;

		FJointPreApplyCallback PreApplyCallback;
		FJointPostApplyCallback PostApplyCallback;
		FJointPostApplyCallback PostProjectCallback;

		// @todo(ccaulfield): optimize storage for joint solver
		TArray<FJointSolverGaussSeidel> ConstraintSolvers;

		TArray<FJointSolverConstraints> SolverConstraints;
		TArray<TVector<int32, 2>> JointBatches;
		TArray< FJointSolverJointState> SolverConstraintStates;
		TArray<FJointSolverConstraintRowData> SolverConstraintRowDatas;
		TArray<FJointSolverConstraintRowState> SolverConstraintRowStates;
	};

}
