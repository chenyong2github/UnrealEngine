// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Chaos/Array.h"
#include "Chaos/Core.h"
#include "Chaos/Transform.h"
#include "Chaos/Vector.h"

#include "Chaos/ConstraintHandle.h"
#include "Chaos/Collision/CollisionApplyType.h"
#include "Chaos/Evolution/SolverDatas.h"
#include "Chaos/Joint/JointSolverConstraints.h"
#include "Chaos/Joint/PBDJointSolverGaussSeidel.h"
#include "Chaos/Joint/PBDJointCachedSolverGaussSeidel.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/PBDConstraintContainer.h"
#include "Chaos/PBDJointConstraintTypes.h"
#include "Chaos/PBDJointConstraintData.h"
#include "Chaos/GeometryParticles.h"

namespace Chaos
{
	class FJointSolverConstraints;
	class FPBDJointSolver;
	class FPBDIslandSolverData;

	class CHAOS_API FPBDJointConstraintHandle final : public TIndexedContainerConstraintHandle<FPBDJointConstraints>
	{
	public:
		using Base = TIndexedContainerConstraintHandle<FPBDJointConstraints>;
		using FConstraintContainer = FPBDJointConstraints;

		FPBDJointConstraintHandle();
		FPBDJointConstraintHandle(FConstraintContainer* InConstraintContainer, int32 InConstraintIndex);

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
		const FPBDJointSettings& GetJointSettings() const { return GetSettings(); }	//needed for property macros

		void SetSettings(const FPBDJointSettings& Settings);
		
		TVec2<FGeometryParticleHandle*> GetConstrainedParticles() const override;

		void PreGatherInput(const FReal Dt, FPBDIslandSolverData& SolverData);
		void GatherInput(const FReal Dt, const int32 Particle0Level, const int32 Particle1Level, FPBDIslandSolverData& SolverData);

		static const FConstraintHandleTypeID& StaticType()
		{
			static FConstraintHandleTypeID STypeID(TEXT("FJointConstraintHandle"), &FIndexedConstraintHandle::StaticType());
			return STypeID;
		}

		ESyncState SyncState() const;
		void SetSyncState(ESyncState SyncState);

		void SetEnabledDuringResim(bool bEnabled);
		EResimType ResimType() const;

	protected:
		using Base::ConstraintIndex;
		using Base::ConcreteContainer;
	private:
		bool bLinearPlasticityInitialized;
		bool bAngularPlasticityInitialized;
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
		EResimType ResimType = EResimType::FullResim;
		ESyncState SyncState = ESyncState::InSync;
		bool bEnabledDuringResim = true;
	};

	/**
	 * A joint restricting up to 6 degrees of freedom, with linear and angular limits.
	 */
	class CHAOS_API FPBDJointConstraints : public FPBDIndexedConstraintContainer
	{
	public:
		using Base = FPBDIndexedConstraintContainer;

		using FConstraintContainerHandle = FPBDJointConstraintHandle;
		using FConstraintHandleAllocator = TConstraintHandleAllocator<FPBDJointConstraints>;
		using FVectorPair = TVector<FVec3, 2>;
		using FTransformPair = TVector<FRigidTransform3, 2>;
		using FHandles = TArray<FConstraintContainerHandle*>;
		using FConstraintSolverContainerType = FConstraintSolverContainer;	// @todo(chaos): Add island solver for this constraint type

		FPBDJointConstraints(const FPBDJointSolverSettings& InSettings = FPBDJointSolverSettings());

		virtual ~FPBDJointConstraints();

		const FPBDJointSolverSettings& GetSettings() const;
		void SetSettings(const FPBDJointSolverSettings& InSettings);

		void SetUseLinearJointSolver(const bool bInEnable) { Settings.bUseLinearSolver = bInEnable; }

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
		void SetLinearDrivePositionTarget(int32 ConstraintIndex, FVec3 InLinearDrivePositionTarget);
		void SetAngularDrivePositionTarget(int32 ConstraintIndex, FRotation3 InAngularDrivePositionTarget);

		int32 GetConstraintIsland(int32 ConstraintIndex) const;
		int32 GetConstraintLevel(int32 ConstraintIndex) const;
		int32 GetConstraintColor(int32 ConstraintIndex) const;

		FVec3 GetConstraintLinearImpulse(int32 ConstraintIndex) const;
		FVec3 GetConstraintAngularImpulse(int32 ConstraintIndex) const;

		ESyncState GetConstraintSyncState(int32 ConstraintIndex) const;
		void SetConstraintSyncState(int32 ConstraintIndex, ESyncState SyncState);
		
		void SetConstraintEnabledDuringResim(int32 ConstraintIndex, bool bEnabled);
		
		EResimType GetConstraintResimType(int32 ConstraintIndex) const;
		
		//
		// General Rule API
		//

		void PrepareTick();

		void UnprepareTick();

		void UpdatePositionBasedState(const FReal Dt);

		//
		// Simple Rule API
		//

		void PreGatherInput(const FReal Dt, FPBDIslandSolverData& SolverData);
		void GatherInput(const FReal Dt, FPBDIslandSolverData& SolverData);
		void ScatterOutput(const FReal Dt, FPBDIslandSolverData& SolverData);

		bool ApplyPhase1(const FReal Dt, const int32 It, const int32 NumIts, FPBDIslandSolverData& SolverData);
		bool ApplyPhase2(const FReal Dt, const int32 It, const int32 NumIts, FPBDIslandSolverData& SolverData);
		bool ApplyPhase3(const FReal Dt, const int32 It, const int32 NumIts, FPBDIslandSolverData& SolverData);

		//
		// Island Rule API
		//

		void SetNumIslandConstraints(const int32 NumIslandConstraints, FPBDIslandSolverData& SolverData);
		void PreGatherInput(const FReal Dt, const int32 ConstraintIndex, FPBDIslandSolverData& SolverData);
		void GatherInput(const FReal Dt, const int32 ConstraintIndex, const int32 Particle0Level, const int32 Particle1Level, FPBDIslandSolverData& SolverData);
		bool ApplyPhase1Serial(const FReal Dt, const int32 It, const int32 NumIts, FPBDIslandSolverData& SolverData);
		bool ApplyPhase2Serial(const FReal Dt, const int32 It, const int32 NumIts, FPBDIslandSolverData& SolverData);
		bool ApplyPhase3Serial(const FReal Dt, const int32 It, const int32 NumIts, FPBDIslandSolverData& SolverData);

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
		FReal CalculateShockPropagationInvMassScale(const FConstraintSolverBody& Body0, const FConstraintSolverBody& Body1, const FPBDJointSettings& JointSettings, const int32 It, const int32 NumIts) const;

		void GetConstrainedParticleIndices(const int32 ConstraintIndex, int32& Index0, int32& Index1) const;
		void CalculateConstraintSpace(int32 ConstraintIndex, FVec3& OutX0, FMatrix33& OutR0, FVec3& OutX1, FMatrix33& OutR1) const;
		
		void ColorConstraints();
		void SortConstraints();

		bool CanEvaluate(const int32 ConstraintIndex) const;

		void PreparePhase3Serial(const FReal Dt, FPBDIslandSolverData& SolverData);

		bool ApplyPhase1Single(const FReal Dt, const int32 ConstraintIndex, const int32 NumPairIts, const int32 It, const int32 NumIts);
		bool ApplyPhase2Single(const FReal Dt, const int32 ConstraintIndex, const int32 It, const int32 NumIts);
		bool ApplyPhase3Single(const FReal Dt, const int32 ConstraintIndex, const int32 It, const int32 NumIts);
		void ApplyBreakThreshold(const FReal Dt, int32 ConstraintIndex, const FVec3& LinearImpulse, const FVec3& AngularImpulse);
		void ApplyPlasticityLimits(const int32 ConstraintIndex);

		FPBDJointSolverSettings Settings;

		TArray<FPBDJointSettings> ConstraintSettings;
		TArray<FParticlePair> ConstraintParticles;
		TArray<FPBDJointState> ConstraintStates;

		FHandles Handles;
		FConstraintHandleAllocator HandleAllocator;
		bool bJointsDirty;

		FJointBreakCallback BreakCallback;

		// @todo(ccaulfield): optimize storage for joint solver
		TArray<FPBDJointSolver> ConstraintSolvers;
		TArray<FPBDJointCachedSolver> CachedConstraintSolvers;

		EConstraintSolverType SolverType;


	};

}
