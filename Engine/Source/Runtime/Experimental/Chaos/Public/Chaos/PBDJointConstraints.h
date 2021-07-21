// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Chaos/Array.h"
#include "Chaos/Core.h"
#include "Chaos/Transform.h"
#include "Chaos/Vector.h"

#include "Chaos/ConstraintHandle.h"
#include "Chaos/Collision/CollisionApplyType.h"
#include "Chaos/Joint/JointSolverConstraints.h"
#include "Chaos/Joint/PBDJointSolverGaussSeidel.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/PBDConstraintContainer.h"
#include "Chaos/PBDJointConstraintTypes.h"
#include "Chaos/PBDJointConstraintData.h"

namespace Chaos
{
	class FJointSolverConstraints;
	class FJointSolverGaussSeidel;

	class CHAOS_API FPBDJointConstraintHandle : public TContainerConstraintHandle<FPBDJointConstraints>
	{
	public:
		using Base = TContainerConstraintHandle<FPBDJointConstraints>;
		using FConstraintContainer = FPBDJointConstraints;

		FPBDJointConstraintHandle();
		FPBDJointConstraintHandle(FConstraintContainer* InConstraintContainer, int32 InConstraintIndex);
		static EConstraintContainerType StaticType() { return EConstraintContainerType::Joint; }

		void SetConstraintEnabled(bool bEnabled);

		void CalculateConstraintSpace(FVec3& OutXa, FMatrix33& OutRa, FVec3& OutXb, FMatrix33& OutRb) const;
		int32 GetConstraintIsland() const;
		int32 GetConstraintLevel() const;
		int32 GetConstraintColor() const;

		bool IsConstraintEnabled() const;
		bool IsConstraintBreaking() const;
		void ClearConstraintBreaking();
		bool IsDriveTargetChanged() const;
		void ClearDriveTargetChanged();
		FVec3 GetLinearImpulse() const;
		FVec3 GetAngularImpulse() const;

		const FPBDJointSettings& GetSettings() const;

		void SetSettings(const FPBDJointSettings& Settings);
		TVec2<FGeometryParticleHandle*> GetConstrainedParticles() const;

	protected:
		using Base::ConstraintIndex;
		using Base::ConcreteContainer;
	};

	class CHAOS_API FPBDJointState
	{
	public:
		FPBDJointState();

		int32 Island;
		int32 Level;
		int32 Color;
		int32 IslandSize;
		bool bDisabled;
		bool bBreaking;
		bool bDriveTargetChanged;
		FVec3 LinearImpulse;
		FVec3 AngularImpulse;
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
		 * @todo(chaos): clean up this set of functions (now that ConnectorTransforms is in the settings, calling AddConstraint then SetSettings leads 
		 * to unexpected behaviour - overwriting the ConnectorTransforms with Identity)
		 */
		FConstraintContainerHandle* AddConstraint(const FParticlePair& InConstrainedParticles, const FRigidTransform3& WorldConstraintFrame);
		FConstraintContainerHandle* AddConstraint(const FParticlePair& InConstrainedParticles, const FTransformPair& ConnectorTransforms);
		FConstraintContainerHandle* AddConstraint(const FParticlePair& InConstrainedParticles, const FPBDJointSettings& InConstraintSettings);

		/**
		 * Remove the specified constraint.
		 */
		void RemoveConstraint(int ConstraintIndex);
		void RemoveConstraints(const TSet<TGeometryParticleHandle<FReal, 3>*>& RemovedParticles) {}

		/*
		* Disconnect the constraints from the attached input particles. 
		* This will set the constrained Particle elements to nullptr and 
		* set the Enable flag to false.
		* 
		* The constraint is unuseable at this point and pending deletion. 
		*/
		void DisconnectConstraints(const TSet<TGeometryParticleHandle<FReal, 3>*>& RemovedParticles);

		/*
		 * Whether the constraint is enabled
		 */
		bool IsConstraintEnabled(int32 ConstraintIndex) const;

		/*
		 * Whether the constraint is breaking this frame
		 */
		bool IsConstraintBreaking(int32 ConstraintIndex) const;

		/*
		 * Clear the constraint braking state
		 */
		void ClearConstraintBreaking(int32 ConstraintIndex);

		/*
		 * Whether the drive target has changed
		 */
		bool IsDriveTargetChanged(int32 ConstraintIndex) const;

		/*
		 * Clear the drive target state
		 */
		void ClearDriveTargetChanged(int32 ConstraintIndex);

		/*
		 * Enable or disable a constraints
		 */
		void SetConstraintEnabled(int32 ConstraintIndex, bool bEnabled);

		/*
		 * Set Breaking State
		 */
		void SetConstraintBreaking(int32 ConstraintIndex, bool bBreaking);

		/*
		* Set Drive Target Changed State
		*/
		void SetDriveTargetChanged(int32 ConstraintIndex, bool bTargetChanged);

		/*
		 * Force a constraints to break
		 */
		void BreakConstraint(int32 ConstraintIndex);

		/**
		 * Repair a broken constraints (does not adjust particle positions)
		 */
		void FixConstraints(int32 ConstraintIndex);

		/*
		* Enable or disable velocity update in apply constraints
		*/
		void SetUpdateVelocityInApplyConstraints(bool bEnabled) { bUpdateVelocityInApplyConstraints = bEnabled; }

		void SetPreApplyCallback(const FJointPostApplyCallback& Callback);
		void ClearPreApplyCallback();

		void SetPostApplyCallback(const FJointPostApplyCallback& Callback);
		void ClearPostApplyCallback();

		void SetPostProjectCallback(const FJointPostApplyCallback& Callback);
		void ClearPostProjectCallback();

		void SetBreakCallback(const FJointBreakCallback& Callback);
		void ClearBreakCallback();

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

		FVec3 GetConstraintLinearImpulse(int32 ConstraintIndex) const;
		FVec3 GetConstraintAngularImpulse(int32 ConstraintIndex) const;

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

		/**
		 * Set the solver method to use
		 */
		void SetSolverType(EConstraintSolverType InSolverType)
		{
			SolverType = InSolverType;
		}

	protected:
		using Base::GetConstraintIndex;
		using Base::SetConstraintIndex;

	private:
		friend class FPBDJointConstraintHandle;

		FReal CalculateIterationStiffness(int32 It, int32 NumIts) const;

		void GetConstrainedParticleIndices(const int32 ConstraintIndex, int32& Index0, int32& Index1) const;
		void CalculateConstraintSpace(int32 ConstraintIndex, FVec3& OutX0, FMatrix33& OutR0, FVec3& OutX1, FMatrix33& OutR1) const;
		void UpdateParticleState(TPBDRigidParticleHandle<FReal, 3>* Rigid, const FReal Dt, const FVec3& PrevP, const FRotation3& PrevQ, const FVec3& P, const FRotation3& Q, const bool bUpdateVelocity = true);
		void UpdateParticleStateExplicit(TPBDRigidParticleHandle<FReal, 3>* Rigid, const FReal Dt, const FVec3& P, const FRotation3& Q, const FVec3& V, const FVec3& W);
		
		void ColorConstraints();
		void SortConstraints();

		bool CanEvaluate(const int32 ConstraintIndex) const;

		bool ApplySingle(const FReal Dt, const int32 ConstraintIndex, const int32 NumPairIts, const int32 It, const int32 NumIts);
		bool ApplyPushOutSingle(const FReal Dt, const int32 ConstraintIndex, const int32 NumPairIts, const int32 It, const int32 NumIts);
		void ApplyBreakThreshold(const FReal Dt, int32 ConstraintIndex, const FVec3& LinearImpulse, const FVec3& AngularImpulse);
		void ApplyPlasticityLimits(const int32 ConstraintIndex);

		FPBDJointSolverSettings Settings;

		TArray<FPBDJointSettings> ConstraintSettings;
		TArray<FParticlePair> ConstraintParticles;
		TArray<FPBDJointState> ConstraintStates;

		FHandles Handles;
		FConstraintHandleAllocator HandleAllocator;
		bool bJointsDirty;
		bool bUpdateVelocityInApplyConstraints;

		FJointPreApplyCallback PreApplyCallback;
		FJointPostApplyCallback PostApplyCallback;
		FJointPostApplyCallback PostProjectCallback;
		FJointBreakCallback BreakCallback;

		// @todo(ccaulfield): optimize storage for joint solver
		TArray<FJointSolverGaussSeidel> ConstraintSolvers;

		EConstraintSolverType SolverType;
	};

}
