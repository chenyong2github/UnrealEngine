// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/ChaosDebugDraw.h"
#include "Chaos/DebugDrawQueue.h"
#include "Chaos/Joint/ChaosJointLog.h"
#include "Chaos/Joint/ColoringGraph.h"
#include "Chaos/Joint/JointConstraintsCVars.h"
#include "Chaos/Particle/ParticleUtilities.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDJointConstraintUtilities.h"
#include "Chaos/Utilities.h"
#include "ChaosLog.h"
#include "ChaosStats.h"

#include "HAL/IConsoleManager.h"

#if INTEL_ISPC
#include "PBDJointSolverGaussSeidel.ispc.generated.h"
#endif

//PRAGMA_DISABLE_OPTIMIZATION

namespace Chaos
{
	DECLARE_CYCLE_STAT(TEXT("Joints::Sort"), STAT_Joints_Sort, STATGROUP_ChaosJoint);
	DECLARE_CYCLE_STAT(TEXT("Joints::PrepareTick"), STAT_Joints_PrepareTick, STATGROUP_ChaosJoint);
	DECLARE_CYCLE_STAT(TEXT("Joints::UnprepareTick"), STAT_Joints_UnprepareTick, STATGROUP_ChaosJoint);
	DECLARE_CYCLE_STAT(TEXT("Joints::PrepareIterations"), STAT_Joints_PrepareIteration, STATGROUP_ChaosJoint);
	DECLARE_CYCLE_STAT(TEXT("Joints::UnprepareIteration"), STAT_Joints_UnprepareIteration, STATGROUP_ChaosJoint);
	DECLARE_CYCLE_STAT(TEXT("Joints::Apply"), STAT_Joints_Apply, STATGROUP_ChaosJoint);
	DECLARE_CYCLE_STAT(TEXT("Joints::ApplyBatched"), STAT_Joints_ApplyBatched, STATGROUP_ChaosJoint);
	DECLARE_CYCLE_STAT(TEXT("Joints::ApplyPushOut"), STAT_Joints_ApplyPushOut, STATGROUP_ChaosJoint);
	DECLARE_CYCLE_STAT(TEXT("Joints::ApplyPushOutBatched"), STAT_Joints_ApplyPushOutBatched, STATGROUP_ChaosJoint);

	//
	// Constraint Handle
	//

	
	FPBDJointConstraintHandle::FPBDJointConstraintHandle()
	{
	}

	
	FPBDJointConstraintHandle::FPBDJointConstraintHandle(FConstraintContainer* InConstraintContainer, int32 InConstraintIndex)
		: TContainerConstraintHandle<FPBDJointConstraints>(InConstraintContainer, InConstraintIndex)
	{
	}

	
	void FPBDJointConstraintHandle::CalculateConstraintSpace(FVec3& OutXa, FMatrix33& OutRa, FVec3& OutXb, FMatrix33& OutRb) const
	{
		ConstraintContainer->CalculateConstraintSpace(ConstraintIndex, OutXa, OutRa, OutXb, OutRb);
	}


	int32 FPBDJointConstraintHandle::GetConstraintIsland() const
	{
		return ConstraintContainer->GetConstraintIsland(ConstraintIndex);
	}


	int32 FPBDJointConstraintHandle::GetConstraintLevel() const
	{
		return ConstraintContainer->GetConstraintLevel(ConstraintIndex);
	}


	int32 FPBDJointConstraintHandle::GetConstraintColor() const
	{
		return ConstraintContainer->GetConstraintColor(ConstraintIndex);
	}


	int32 FPBDJointConstraintHandle::GetConstraintBatch() const
	{
		return ConstraintContainer->GetConstraintBatch(ConstraintIndex);
	}

	
	const FPBDJointSettings& FPBDJointConstraintHandle::GetSettings() const
	{
		return ConstraintContainer->GetConstraintSettings(ConstraintIndex);
	}

	void FPBDJointConstraintHandle::SetSettings(const FPBDJointSettings& Settings)
	{
		ConstraintContainer->SetConstraintSettings(ConstraintIndex, Settings);
	}

	TVector<TGeometryParticleHandle<float,3>*, 2> FPBDJointConstraintHandle::GetConstrainedParticles() const 
	{ 
		return ConstraintContainer->GetConstrainedParticles(ConstraintIndex); 
	}

	//
	// Constraint Settings
	//

	
	FPBDJointSettings::FPBDJointSettings()
		: Stiffness(1)
		, LinearProjection(0)
		, AngularProjection(0)
		, ParentInvMassScale(1)
		, LinearMotionTypes({ EJointMotionType::Locked, EJointMotionType::Locked, EJointMotionType::Locked })
		, LinearLimit(FLT_MAX)
		, AngularMotionTypes({ EJointMotionType::Free, EJointMotionType::Free, EJointMotionType::Free })
		, AngularLimits(FVec3(FLT_MAX, FLT_MAX, FLT_MAX))
		, bSoftLinearLimitsEnabled(false)
		, bSoftTwistLimitsEnabled(false)
		, bSoftSwingLimitsEnabled(false)
		, LinearSoftForceMode(EJointForceMode::Acceleration)
		, AngularSoftForceMode(EJointForceMode::Acceleration)
		, SoftLinearStiffness(0)
		, SoftLinearDamping(0)
		, SoftTwistStiffness(0)
		, SoftTwistDamping(0)
		, SoftSwingStiffness(0)
		, SoftSwingDamping(0)
		, LinearDriveTarget(FVec3(0, 0, 0))
		, bLinearPositionDriveEnabled(TVector<bool, 3>(false, false, false))
		, bLinearVelocityDriveEnabled(TVector<bool, 3>(false, false, false))
		, LinearDriveForceMode(EJointForceMode::Acceleration)
		, LinearDriveStiffness(0)
		, LinearDriveDamping(0)
		, AngularDrivePositionTarget(FRotation3::FromIdentity())
		, AngularDriveVelocityTarget(FVec3(0, 0, 0))
		, bAngularSLerpPositionDriveEnabled(false)
		, bAngularSLerpVelocityDriveEnabled(false)
		, bAngularTwistPositionDriveEnabled(false)
		, bAngularTwistVelocityDriveEnabled(false)
		, bAngularSwingPositionDriveEnabled(false)
		, bAngularSwingVelocityDriveEnabled(false)
		, AngularDriveForceMode(EJointForceMode::Acceleration)
		, AngularDriveStiffness(0)
		, AngularDriveDamping(0)
		, LinearBreakForce(0)
		, AngularBreakTorque(0)
	{
		if (bChaos_Joint_ISPC_Enabled)
		{
#if INTEL_ISPC
			check(sizeof(FJointSolverJointState) == ispc::SizeofFJointSolverJointState());
			check(sizeof(FJointSolverConstraintRowState) == ispc::SizeofFJointSolverConstraintRowState());
			check(sizeof(FJointSolverConstraintRowData) == ispc::SizeofFJointSolverConstraintRowData());
			check(sizeof(FJointSolverJointState) == ispc::SizeofFJointSolverJointState());
#endif
		}
	}


	void FPBDJointSettings::Sanitize()
	{
		// Reset limits if they won't be used (means we don't have to check if limited/locked in a few cases).
		// A side effect: if we enable a constraint, we need to reset the value of the limit.
		if ((LinearMotionTypes[0] != EJointMotionType::Limited) && (LinearMotionTypes[1] != EJointMotionType::Limited) && (LinearMotionTypes[2] != EJointMotionType::Limited))
		{
			LinearLimit = 0;
		}
		if (AngularMotionTypes[(int32)EJointAngularConstraintIndex::Twist] != EJointMotionType::Limited)
		{
			AngularLimits[(int32)EJointAngularConstraintIndex::Twist] = 0;
		}
		if (AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing1] != EJointMotionType::Limited)
		{
			AngularLimits[(int32)EJointAngularConstraintIndex::Swing1] = 0;
		}
		if (AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing2] != EJointMotionType::Limited)
		{
			AngularLimits[(int32)EJointAngularConstraintIndex::Swing2] = 0;
		}
	}

	
	FPBDJointState::FPBDJointState()
		: Batch(INDEX_NONE)
		, Island(INDEX_NONE)
		, Level(INDEX_NONE)
		, Color(INDEX_NONE)
		, IslandSize(0)
		, bDisabled(false)
	{
	}


	//
	// Solver Settings
	//

	
	FPBDJointSolverSettings::FPBDJointSolverSettings()
		: ApplyPairIterations(1)
		, ApplyPushOutPairIterations(1)
		, SwingTwistAngleTolerance(1.0e-6f)
		, PositionTolerance(0)
		, AngleTolerance(0)
		, MinParentMassRatio(0)
		, MaxInertiaRatio(0)
		, AngularConstraintPositionCorrection(1.0f)
		, ProjectionInvMassScale(0)
		, VelProjectionInvMassScale(0)
		, bEnableTwistLimits(true)
		, bEnableSwingLimits(true)
		, bEnableDrives(true)
		, LinearProjection(0)
		, AngularProjection(0)
		, Stiffness(0)
		, LinearDriveStiffness(0)
		, LinearDriveDamping(0)
		, AngularDriveStiffness(0)
		, AngularDriveDamping(0)
		, SoftLinearStiffness(0)
		, SoftLinearDamping(0)
		, SoftTwistStiffness(0)
		, SoftTwistDamping(0)
		, SoftSwingStiffness(0)
		, SoftSwingDamping(0)
	{
	}


	//
	// Constraint Container
	//

	
	FPBDJointConstraints::FPBDJointConstraints(const FPBDJointSolverSettings& InSettings)
		: Settings(InSettings)
		, bJointsDirty(false)
		, bIsBatched(false)
		, PreApplyCallback(nullptr)
		, PostApplyCallback(nullptr)
	{
	}

	
	FPBDJointConstraints::~FPBDJointConstraints()
	{
	}

	
	const FPBDJointSolverSettings& FPBDJointConstraints::GetSettings() const
	{
		return Settings;
	}

	
	void FPBDJointConstraints::SetSettings(const FPBDJointSolverSettings& InSettings)
	{
		Settings = InSettings;
	}


	int32 FPBDJointConstraints::NumConstraints() const
	{
		return ConstraintParticles.Num();
	}

	void FPBDJointConstraints::GetConstrainedParticleIndices(const int32 ConstraintIndex, int32& Index0, int32& Index1) const
	{
		// In solvers we assume Particle0 is the parent particle (which it usually is as implemented in the editor). 
		// However, it is possible to set it up so that the kinematic particle is the child which we don't support, so...
		// If particle 0 is kinematic we make it the parent, otherwise particle 1 is the parent.
		// @todo(ccaulfield): look into this and confirm/fix properly
		if (!ConstraintParticles[ConstraintIndex][0]->CastToRigidParticle())
		{
			Index0 = 0;
			Index1 = 1;
		}
		else
		{
			Index0 = 1;
			Index1 = 0;
		}
	}

	typename FPBDJointConstraints::FConstraintContainerHandle* FPBDJointConstraints::AddConstraint(const FParticlePair& InConstrainedParticles, const FRigidTransform3& WorldConstraintFrame)
	{
		FTransformPair JointFrames;
		JointFrames[0] = FRigidTransform3(
			WorldConstraintFrame.GetTranslation() - InConstrainedParticles[0]->X(),
			WorldConstraintFrame.GetRotation() * InConstrainedParticles[0]->R().Inverse()
			);
		JointFrames[1] = FRigidTransform3(
			WorldConstraintFrame.GetTranslation() - InConstrainedParticles[1]->X(),
			WorldConstraintFrame.GetRotation() * InConstrainedParticles[1]->R().Inverse()
			);
		return AddConstraint(InConstrainedParticles, JointFrames, FPBDJointSettings());
	}

	
	typename FPBDJointConstraints::FConstraintContainerHandle* FPBDJointConstraints::AddConstraint(const FParticlePair& InConstrainedParticles, const FTransformPair& InConstraintFrames)
	{
		return AddConstraint(InConstrainedParticles, InConstraintFrames, FPBDJointSettings());
	}

	
	typename FPBDJointConstraints::FConstraintContainerHandle* FPBDJointConstraints::AddConstraint(const FParticlePair& InConstrainedParticles, const FTransformPair& InConstraintFrames, const FPBDJointSettings& InConstraintSettings)
	{
		bJointsDirty = true;

		int ConstraintIndex = Handles.Num();
		Handles.Add(HandleAllocator.AllocHandle(this, ConstraintIndex));
		ConstraintParticles.Add(InConstrainedParticles);
		ConstraintSettings.Add(InConstraintSettings);
		ConstraintFrames.Add(InConstraintFrames);
		ConstraintStates.Add(FPBDJointState());
		return Handles.Last();
	}

	
	void FPBDJointConstraints::RemoveConstraint(int ConstraintIndex)
	{
		bJointsDirty = true;

		FConstraintContainerHandle* ConstraintHandle = Handles[ConstraintIndex];
		if (ConstraintHandle != nullptr)
		{
			// Release the handle for the freed constraint
			HandleAllocator.FreeHandle(ConstraintHandle);
			Handles[ConstraintIndex] = nullptr;
		}

		// Swap the last constraint into the gap to keep the array packed
		ConstraintParticles.RemoveAtSwap(ConstraintIndex);
		ConstraintSettings.RemoveAtSwap(ConstraintIndex);
		ConstraintFrames.RemoveAtSwap(ConstraintIndex);
		ConstraintStates.RemoveAtSwap(ConstraintIndex);
		Handles.RemoveAtSwap(ConstraintIndex);

		// Update the handle for the constraint that was moved
		if (ConstraintIndex < Handles.Num())
		{
			SetConstraintIndex(Handles[ConstraintIndex], ConstraintIndex);
		}
	}

	
	void FPBDJointConstraints::RemoveConstraints(const TSet<TGeometryParticleHandle<FReal, 3>*>& RemovedParticles)
	{
	}


	void FPBDJointConstraints::SortConstraints()
	{
		// Sort constraints so that constraints with lower level (closer to a kinematic joint) are first
		// @todo(ccaulfield): should probably also take islands/particle order into account
		// @todo(ccaulfield): optimize (though isn't called very often)
		SCOPE_CYCLE_COUNTER(STAT_Joints_Sort);

		FHandles SortedHandles = Handles;
		SortedHandles.StableSort([](const FConstraintContainerHandle& L, const FConstraintContainerHandle& R)
			{
				if (L.GetConstraintBatch() != R.GetConstraintBatch())
				{
					return L.GetConstraintBatch() < R.GetConstraintBatch();
				}
				else if (L.GetConstraintIsland() != R.GetConstraintIsland())
				{
					return L.GetConstraintIsland() < R.GetConstraintIsland();
				}
				else if (L.GetConstraintLevel() != R.GetConstraintLevel())
				{
					return L.GetConstraintLevel() < R.GetConstraintLevel();
				}
				return L.GetConstraintColor() < R.GetConstraintColor();
			});

		TArray<FPBDJointSettings> SortedConstraintSettings;
		TArray<FTransformPair> SortedConstraintFrames;
		TArray<FParticlePair> SortedConstraintParticles;
		TArray<FPBDJointState> SortedConstraintStates;
		SortedConstraintSettings.Reserve(SortedHandles.Num());
		SortedConstraintFrames.Reserve(SortedHandles.Num());
		SortedConstraintParticles.Reserve(SortedHandles.Num());
		SortedConstraintStates.Reserve(SortedHandles.Num());

		for (int32 SortedConstraintIndex = 0; SortedConstraintIndex < SortedHandles.Num(); ++SortedConstraintIndex)
		{
			FConstraintContainerHandle* Handle = SortedHandles[SortedConstraintIndex];
			int32 UnsortedConstraintIndex = Handle->GetConstraintIndex();

			SortedConstraintSettings.Add(ConstraintSettings[UnsortedConstraintIndex]);
			SortedConstraintFrames.Add(ConstraintFrames[UnsortedConstraintIndex]);
			SortedConstraintParticles.Add(ConstraintParticles[UnsortedConstraintIndex]);
			SortedConstraintStates.Add(ConstraintStates[UnsortedConstraintIndex]);
			SetConstraintIndex(Handle, SortedConstraintIndex);
		}

		Swap(ConstraintSettings, SortedConstraintSettings);
		Swap(ConstraintFrames, SortedConstraintFrames);
		Swap(ConstraintParticles, SortedConstraintParticles);
		Swap(ConstraintStates, SortedConstraintStates);
		Swap(Handles, SortedHandles);
	}


	bool FPBDJointConstraints::IsConstraintEnabled(int32 ConstraintIndex) const
	{
		return !ConstraintStates[ConstraintIndex].bDisabled;
	}


	void FPBDJointConstraints::SetConstraintEnabled(int32 ConstraintIndex, bool bEnabled)
	{
		ConstraintStates[ConstraintIndex].bDisabled = !bEnabled;
	}


	void FPBDJointConstraints::BreakConstraint(int32 ConstraintIndex)
	{
		SetConstraintEnabled(ConstraintIndex, false);
		BreakCallback(Handles[ConstraintIndex]);
	}


	void FPBDJointConstraints::FixConstraints(int32 ConstraintIndex)
	{
		SetConstraintEnabled(ConstraintIndex, true);
	}

	
	void FPBDJointConstraints::SetPreApplyCallback(const FJointPreApplyCallback& Callback)
	{
		PreApplyCallback = Callback;
	}

	
	void FPBDJointConstraints::ClearPreApplyCallback()
	{
		PreApplyCallback = nullptr;
	}


	void FPBDJointConstraints::SetPostApplyCallback(const FJointPostApplyCallback& Callback)
	{
		PostApplyCallback = Callback;
	}


	void FPBDJointConstraints::ClearPostApplyCallback()
	{
		PostApplyCallback = nullptr;
	}


	void FPBDJointConstraints::SetPostProjectCallback(const FJointPostApplyCallback& Callback)
	{
		PostProjectCallback = Callback;
	}


	void FPBDJointConstraints::ClearPostProjectCallback()
	{
		PostProjectCallback = nullptr;
	}


	void FPBDJointConstraints::SetBreakCallback(const FJointBreakCallback& Callback)
	{
		BreakCallback = Callback;
	}


	void FPBDJointConstraints::ClearBreakCallback()
	{
		BreakCallback = nullptr;
	}


	const typename FPBDJointConstraints::FConstraintContainerHandle* FPBDJointConstraints::GetConstraintHandle(int32 ConstraintIndex) const
	{
		return Handles[ConstraintIndex];
	}

	
	typename FPBDJointConstraints::FConstraintContainerHandle* FPBDJointConstraints::GetConstraintHandle(int32 ConstraintIndex)
	{
		return Handles[ConstraintIndex];
	}

	
	const typename FPBDJointConstraints::FParticlePair& FPBDJointConstraints::GetConstrainedParticles(int32 ConstraintIndex) const
	{
		return ConstraintParticles[ConstraintIndex];
	}

	
	const FPBDJointSettings& FPBDJointConstraints::GetConstraintSettings(int32 ConstraintIndex) const
	{
		return ConstraintSettings[ConstraintIndex];
	}


	void FPBDJointConstraints::SetConstraintSettings(int32 ConstraintIndex, const FPBDJointSettings& InConstraintSettings)
	{
		ConstraintSettings[ConstraintIndex] = InConstraintSettings;
	}


	int32 FPBDJointConstraints::GetConstraintIsland(int32 ConstraintIndex) const
	{
		return ConstraintStates[ConstraintIndex].Island;
	}


	int32 FPBDJointConstraints::GetConstraintLevel(int32 ConstraintIndex) const
	{
		return ConstraintStates[ConstraintIndex].Level;
	}


	int32 FPBDJointConstraints::GetConstraintColor(int32 ConstraintIndex) const
	{
		return ConstraintStates[ConstraintIndex].Color;
	}


	int32 FPBDJointConstraints::GetConstraintBatch(int32 ConstraintIndex) const
	{
		return ConstraintStates[ConstraintIndex].Batch;
	}


	void FPBDJointConstraints::UpdatePositionBasedState(const FReal Dt)
	{
	}

	void FPBDJointConstraints::PrepareTick()
	{
		SCOPE_CYCLE_COUNTER(STAT_Joints_PrepareTick);

		if (bJointsDirty || (bIsBatched != bChaos_Joint_Batching))
		{
			DeinitSolverJointData();

			BatchConstraints();

			InitSolverJointData();

			bIsBatched = bChaos_Joint_Batching;
			bJointsDirty = false;
		}

		if (bChaos_Joint_Batching)
		{
			SolverConstraintRowStates.SetNum(SolverConstraintRowDatas.Num());
			SolverConstraintStates.SetNum(NumConstraints());

			for (FJointSolverConstraintRowState RowState : SolverConstraintRowStates)
			{
				RowState.TickReset();
			}
		}
		else
		{
			ConstraintSolvers.SetNum(NumConstraints());
		}
	}

	void FPBDJointConstraints::UnprepareTick()
	{
		SCOPE_CYCLE_COUNTER(STAT_Joints_UnprepareTick);

		if (bChaos_Joint_Batching)
		{
			SolverConstraintRowStates.Empty();
			SolverConstraintStates.Empty();
		}
		else
		{
			ConstraintSolvers.Empty();
		}
	}

	void FPBDJointConstraints::PrepareIteration(FReal Dt)
	{
		SCOPE_CYCLE_COUNTER(STAT_Joints_PrepareIteration);

		if (bChaos_Joint_Batching)
		{
			for (int32 JointIndex = 0; JointIndex < NumConstraints(); ++JointIndex)
			{
				FJointSolverJointState& JointState = SolverConstraintStates[JointIndex];
				const FPBDJointSettings& JointSettings = ConstraintSettings[JointIndex];

				const FTransformPair& JointFrames = ConstraintFrames[JointIndex];
				int32 Index0, Index1;
				GetConstrainedParticleIndices(JointIndex, Index0, Index1);
				TGenericParticleHandle<FReal, 3> Particle0 = TGenericParticleHandle<FReal, 3>(ConstraintParticles[JointIndex][Index0]);
				TGenericParticleHandle<FReal, 3> Particle1 = TGenericParticleHandle<FReal, 3>(ConstraintParticles[JointIndex][Index1]);

				JointState.Init(
					Settings,
					JointSettings,
					FParticleUtilitiesXR::GetCoMWorldPosition(Particle0),
					FParticleUtilitiesXR::GetCoMWorldRotation(Particle0),
					FParticleUtilitiesXR::GetCoMWorldPosition(Particle1),
					FParticleUtilitiesXR::GetCoMWorldRotation(Particle1),
					Particle0->InvM(),
					Particle0->InvI().GetDiagonal(),
					Particle1->InvM(),
					Particle1->InvI().GetDiagonal(),
					FParticleUtilities::ParticleLocalToCoMLocal(Particle0, JointFrames[Index0]),
					FParticleUtilities::ParticleLocalToCoMLocal(Particle1, JointFrames[Index1]));
			}
		}
		else
		{
			for (int32 JointIndex = 0; JointIndex < NumConstraints(); ++JointIndex)
			{
				const FPBDJointSettings& JointSettings = ConstraintSettings[JointIndex];

				const FTransformPair& JointFrames = ConstraintFrames[JointIndex];
				FJointSolverGaussSeidel& Solver = ConstraintSolvers[JointIndex];

				int32 Index0, Index1;
				GetConstrainedParticleIndices(JointIndex, Index0, Index1);
				TGenericParticleHandle<FReal, 3> Particle0 = TGenericParticleHandle<FReal, 3>(ConstraintParticles[JointIndex][Index0]);
				TGenericParticleHandle<FReal, 3> Particle1 = TGenericParticleHandle<FReal, 3>(ConstraintParticles[JointIndex][Index1]);

				Solver.Init(
					Dt,
					Settings,
					JointSettings,
					FParticleUtilitiesXR::GetCoMWorldPosition(Particle0),	// Prev position
					FParticleUtilitiesXR::GetCoMWorldPosition(Particle1),	// Prev position
					FParticleUtilitiesXR::GetCoMWorldRotation(Particle0),	// Prev rotation
					FParticleUtilitiesXR::GetCoMWorldRotation(Particle1),	// Prev rotation
					Particle0->InvM(),
					Particle0->InvI().GetDiagonal(),
					Particle1->InvM(),
					Particle1->InvI().GetDiagonal(),
					FParticleUtilities::ParticleLocalToCoMLocal(Particle0, JointFrames[Index0]),
					FParticleUtilities::ParticleLocalToCoMLocal(Particle1, JointFrames[Index1]));
			}
		}
	}


	void FPBDJointConstraints::UnprepareIteration(FReal Dt)
	{
		SCOPE_CYCLE_COUNTER(STAT_Joints_UnprepareIteration);
	}

	
	void FPBDJointConstraints::CalculateConstraintSpace(int32 ConstraintIndex, FVec3& OutX0, FMatrix33& OutR0, FVec3& OutX1, FMatrix33& OutR1) const
	{
		int32 Index0, Index1;
		GetConstrainedParticleIndices(ConstraintIndex, Index0, Index1);
		TGenericParticleHandle<FReal, 3> Particle0 = TGenericParticleHandle<FReal, 3>(ConstraintParticles[ConstraintIndex][Index0]);
		TGenericParticleHandle<FReal, 3> Particle1 = TGenericParticleHandle<FReal, 3>(ConstraintParticles[ConstraintIndex][Index1]);
		const FVec3 P0 = FParticleUtilities::GetCoMWorldPosition(Particle0);
		const FRotation3 Q0 = FParticleUtilities::GetCoMWorldRotation(Particle0);
		const FVec3 P1 = FParticleUtilities::GetCoMWorldPosition(Particle1);
		const FRotation3 Q1 = FParticleUtilities::GetCoMWorldRotation(Particle1);
		const FRigidTransform3& XL0 = FParticleUtilities::ParticleLocalToCoMLocal(Particle0, ConstraintFrames[ConstraintIndex][Index0]);
		const FRigidTransform3& XL1 = FParticleUtilities::ParticleLocalToCoMLocal(Particle1, ConstraintFrames[ConstraintIndex][Index1]);

		OutX0 = P0 + Q0 * XL0.GetTranslation();
		OutX1 = P1 + Q1 * XL1.GetTranslation();
		OutR0 = FRotation3(Q0 * XL0.GetRotation()).ToMatrix();
		OutR1 = FRotation3(Q1 * XL1.GetRotation()).ToMatrix();
	}

	//////////////////////////////////////////////////////////////////////////
	//
	// Begin Simple API Solver. Iterate over constraints in array order.
	//
	//////////////////////////////////////////////////////////////////////////

	bool FPBDJointConstraints::Apply(const FReal Dt, const int32 It, const int32 NumIts)
	{
		if (PreApplyCallback != nullptr)
		{
			PreApplyCallback(Dt, Handles);
		}

		int32 NumActive = 0;
		if (Settings.ApplyPairIterations > 0)
		{
			if (bChaos_Joint_Batching)
			{
				SCOPE_CYCLE_COUNTER(STAT_Joints_ApplyBatched);
				for (int32 BatchIndex = 0; BatchIndex < JointBatches.Num(); ++BatchIndex)
				{
					NumActive += ApplyBatch(Dt, BatchIndex, Settings.ApplyPairIterations, It, NumIts);
				}
			}
			else
			{
				SCOPE_CYCLE_COUNTER(STAT_Joints_Apply);
				for (int32 ConstraintIndex = 0; ConstraintIndex < NumConstraints(); ++ConstraintIndex)
				{
					NumActive += ApplySingle(Dt, ConstraintIndex, Settings.ApplyPairIterations, It, NumIts);
				}
			}
		}

		UE_LOG(LogChaosJoint, Verbose, TEXT("Apply Iteration: %d / %d; Active: %d"), It, NumIts, NumActive);

		if (PostApplyCallback != nullptr)
		{
			PostApplyCallback(Dt, Handles);
		}

		return (NumActive > 0);
	}

	bool FPBDJointConstraints::ApplyPushOut(const FReal Dt, const int32 It, const int32 NumIts)
	{

		int32 NumActive = 0;
		if (Settings.ApplyPushOutPairIterations > 0)
		{
			if (bChaos_Joint_Batching)
			{
				SCOPE_CYCLE_COUNTER(STAT_Joints_ApplyPushOutBatched);
				// @todo(ccaulfield): batch mode pushout
			}
			else
			{
				SCOPE_CYCLE_COUNTER(STAT_Joints_ApplyPushOut);
				for (int32 ConstraintIndex = 0; ConstraintIndex < NumConstraints(); ++ConstraintIndex)
				{
					NumActive += ApplyPushOutSingle(Dt, ConstraintIndex, Settings.ApplyPushOutPairIterations, It, NumIts);
				}
			}
		}

		UE_LOG(LogChaosJoint, Verbose, TEXT("PushOut Iteration: %d / %d; Active: %d"), It, NumIts, NumActive);

		if (PostProjectCallback != nullptr)
		{
			PostProjectCallback(Dt, Handles);
		}

		return (NumActive > 0);
	}

	//////////////////////////////////////////////////////////////////////////
	//
	// End Simple API Solver.
	//
	//////////////////////////////////////////////////////////////////////////

	//////////////////////////////////////////////////////////////////////////
	//
	// Begin Graph API Solver. Iterate over constraints in connectivity order.
	//
	//////////////////////////////////////////////////////////////////////////

	bool FPBDJointConstraints::Apply(const FReal Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const int32 It, const int32 NumIts)
	{
		SCOPE_CYCLE_COUNTER(STAT_Joints_Apply);

		// @todo(ccaulfield): make sorting optional
		// @todo(ccaulfield): handles should be sorted by level by the constraint rule/graph
		// @todo(ccaulfield): the best sort order depends on whether we are freezing.
		// If we are freezing we want the root-most (nearest to kinematic) bodies solved first.
		// For normal update we want the root body last, otherwise it gets dragged away from the root by the other bodies

		TArray<FConstraintContainerHandle*> SortedConstraintHandles = InConstraintHandles;
		SortedConstraintHandles.Sort([](const FConstraintContainerHandle& L, const FConstraintContainerHandle& R)
			{
				// Sort bodies from root to leaf
				return L.GetConstraintLevel() < R.GetConstraintLevel();
			});

		if (PreApplyCallback != nullptr)
		{
			PreApplyCallback(Dt, SortedConstraintHandles);
		}


		int32 NumActive = 0;
		if (Settings.ApplyPairIterations > 0)
		{
			for (FConstraintContainerHandle* ConstraintHandle : SortedConstraintHandles)
			{
				NumActive += ApplySingle(Dt, ConstraintHandle->GetConstraintIndex(), Settings.ApplyPairIterations, It, NumIts);
			}
		}

		if (PostApplyCallback != nullptr)
		{
			PostApplyCallback(Dt, SortedConstraintHandles);
		}

		return (NumActive > 0);
	}

	
	bool FPBDJointConstraints::ApplyPushOut(const FReal Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const int32 It, const int32 NumIts)
	{
		SCOPE_CYCLE_COUNTER(STAT_Joints_ApplyPushOut);

		TArray<FConstraintContainerHandle*> SortedConstraintHandles = InConstraintHandles;
		SortedConstraintHandles.Sort([](const FConstraintContainerHandle& L, const FConstraintContainerHandle& R)
			{
				// Sort bodies from root to leaf
				return L.GetConstraintLevel() < R.GetConstraintLevel();
			});

		int32 NumActive = 0;
		if (Settings.ApplyPushOutPairIterations > 0)
		{
			for (FConstraintContainerHandle* ConstraintHandle : SortedConstraintHandles)
			{
				NumActive += ApplyPushOutSingle(Dt, ConstraintHandle->GetConstraintIndex(), Settings.ApplyPushOutPairIterations, It, NumIts);
			}
		}

		if (PostProjectCallback != nullptr)
		{
			PostProjectCallback(Dt, SortedConstraintHandles);
		}

		return (NumActive > 0);
	}

	//////////////////////////////////////////////////////////////////////////
	//
	// End Graph API Solver.
	//
	//////////////////////////////////////////////////////////////////////////


	//////////////////////////////////////////////////////////////////////////
	//
	// Begin single-particle solve methods used by APIs
	//
	//////////////////////////////////////////////////////////////////////////

	void FPBDJointConstraints::UpdateParticleState(TPBDRigidParticleHandle<FReal, 3>* Rigid, const FReal Dt, const FVec3& PrevP, const FRotation3& PrevQ, const FVec3& P, const FRotation3& Q, const bool bUpdateVelocity)
	{
		if ((Rigid != nullptr) && (Rigid->ObjectState() == EObjectStateType::Dynamic))
		{
			FParticleUtilities::SetCoMWorldTransform(Rigid, P, Q);
			if (bUpdateVelocity && (Dt > SMALL_NUMBER))
			{
				const FVec3 V = FVec3::CalculateVelocity(PrevP, P, Dt);
				const FVec3 W = FRotation3::CalculateAngularVelocity(PrevQ, Q, Dt);
				Rigid->SetV(V);
				Rigid->SetW(W);
			}
		}
	}


	void FPBDJointConstraints::UpdateParticleStateExplicit(TPBDRigidParticleHandle<FReal, 3>* Rigid, const FReal Dt, const FVec3& P, const FRotation3& Q, const FVec3& V, const FVec3& W)
	{
		if ((Rigid != nullptr) && (Rigid->ObjectState() == EObjectStateType::Dynamic))
		{
			FParticleUtilities::SetCoMWorldTransform(Rigid, P, Q);
			Rigid->SetV(V);
			Rigid->SetW(W);
		}
	}

	void FPBDJointConstraints::InitSolverJointData()
	{
		SolverConstraints.SetNum(NumConstraints());
		for (int32 JointIndex = 0; JointIndex < NumConstraints(); ++JointIndex)
		{
			const FPBDJointSettings& JointSettings = ConstraintSettings[JointIndex];
			SolverConstraints[JointIndex].SetJointIndex(JointIndex);
			SolverConstraints[JointIndex].AddPositionConstraints(SolverConstraintRowDatas, Settings, JointSettings);
		}
		for (int32 JointIndex = 0; JointIndex < NumConstraints(); ++JointIndex)
		{
			const FPBDJointSettings& JointSettings = ConstraintSettings[JointIndex];
			SolverConstraints[JointIndex].AddRotationConstraints(SolverConstraintRowDatas, Settings, JointSettings);
		}
	}

	void FPBDJointConstraints::DeinitSolverJointData()
	{
		SolverConstraints.Empty();
		SolverConstraintRowDatas.Empty();
	}

	void FPBDJointConstraints::GatherSolverJointState(int32 JointIndex)
	{
		FJointSolverJointState& JointState = SolverConstraintStates[JointIndex];

		int32 Index0, Index1;
		GetConstrainedParticleIndices(JointIndex, Index0, Index1);
		TGenericParticleHandle<FReal, 3> Particle0 = TGenericParticleHandle<FReal, 3>(ConstraintParticles[JointIndex][Index0]);
		TGenericParticleHandle<FReal, 3> Particle1 = TGenericParticleHandle<FReal, 3>(ConstraintParticles[JointIndex][Index1]);

		JointState.Update(
			FParticleUtilities::GetCoMWorldPosition(Particle0),
			FParticleUtilities::GetCoMWorldRotation(Particle0),
			FParticleUtilities::GetCoMWorldPosition(Particle1),
			FParticleUtilities::GetCoMWorldRotation(Particle1));
	}

	void FPBDJointConstraints::ScatterSolverJointState(const FReal Dt, int32 JointIndex)
	{
		int32 Index0, Index1;
		GetConstrainedParticleIndices(JointIndex, Index0, Index1);
		TPBDRigidParticleHandle<FReal, 3>* Particle0 = ConstraintParticles[JointIndex][Index0]->CastToRigidParticle();
		TPBDRigidParticleHandle<FReal, 3>* Particle1 = ConstraintParticles[JointIndex][Index1]->CastToRigidParticle();

		FJointSolverJointState& JointState = SolverConstraintStates[JointIndex];
		bool bUpdateVelocity = false;	// Position-based collision solver does not need V() and W()
		UpdateParticleState(Particle0, Dt, JointState.PrevPs[0], JointState.PrevQs[0], JointState.Ps[0], JointState.Qs[0], bUpdateVelocity);
		UpdateParticleState(Particle1, Dt, JointState.PrevPs[1], JointState.PrevQs[1], JointState.Ps[1], JointState.Qs[1], bUpdateVelocity);
	}

	int32 FPBDJointConstraints::ApplyBatch(const FReal Dt, const int32 BatchIndex, const int32 NumPairIts, const int32 It, const int32 NumIts)
	{
		UE_LOG(LogChaosJoint, VeryVerbose, TEXT("Solve Joint Batch %d %d-%d (dt = %f; it = %d / %d)"), BatchIndex, JointBatches[BatchIndex][0], JointBatches[BatchIndex][1], Dt, It, NumIts);

		int32 NumActive = 0;

		// The range of joints in the batch
		const int32 JointIndexBegin = JointBatches[BatchIndex][0];
		const int32 JointIndexEnd = JointBatches[BatchIndex][1];
		if (JointIndexEnd <= JointIndexBegin)
		{
			return NumActive;
		}

		// Initialize the state for each joint in the batch (body CoM position, inertias, etc)
		for (int32 JointIndex = JointIndexBegin; JointIndex < JointIndexEnd; ++JointIndex)
		{
			GatherSolverJointState(JointIndex);
		}

		for (int32 PairIt = 0; PairIt < NumPairIts; ++PairIt)
		{
			UE_LOG(LogChaosJoint, VeryVerbose, TEXT("  Pair Iteration %d / %d"), PairIt, NumPairIts);

			// Reset accumulators and update derived state
			if (bChaos_Joint_ISPC_Enabled)
			{
#if INTEL_ISPC
				ispc::BatchUpdateDerivedState(
					(ispc::FJointSolverJointState*)SolverConstraintStates.GetData(), 
					JointBatches[BatchIndex][0], 
					JointBatches[BatchIndex][1]);
#endif
			}
			else
			{
				for (int32 JointIndex = JointIndexBegin; JointIndex < JointIndexEnd; ++JointIndex)
				{
					SolverConstraintStates[JointIndex].UpdateDerivedState();
				}
			}

			// Update the position constraint axes and errors for all Joints in the batch
			for (int32 JointIndex = JointIndexBegin; JointIndex < JointIndexEnd; ++JointIndex)
			{
				SolverConstraints[JointIndex].UpdatePositionConstraints(
					SolverConstraintRowDatas, 
					SolverConstraintRowStates, 
					SolverConstraintStates[JointIndex], 
					ConstraintSettings[JointIndex]);
			}

			// Solve and apply the position constraints for all Joints in the batch
			const int32 LinearRowIndexBegin = SolverConstraints[JointIndexBegin].GetLinearRowIndexBegin();
			const int32 LinearRowIndexEnd = SolverConstraints[JointIndexEnd - 1].GetLinearRowIndexEnd();
			if (bChaos_Joint_ISPC_Enabled)
			{
#if INTEL_ISPC
				ispc::BatchApplyPositionConstraints(
					Dt,
					(ispc::FJointSolverJointState*)SolverConstraintStates.GetData(),
					(ispc::FJointSolverConstraintRowData*)SolverConstraintRowDatas.GetData(),
					(ispc::FJointSolverConstraintRowState*)SolverConstraintRowStates.GetData(),
					JointIndexBegin,
					JointIndexEnd,
					LinearRowIndexBegin,
					LinearRowIndexEnd);
#endif
			}
			else
			{
				NumActive += FJointSolver::ApplyPositionConstraints(
					Dt, 
					SolverConstraintStates, 
					SolverConstraintRowDatas, 
					SolverConstraintRowStates, 
					JointIndexBegin,
					JointIndexEnd,
					LinearRowIndexBegin,
					LinearRowIndexEnd);
			}

			// Reset accumulators and update derived state
			if (bChaos_Joint_ISPC_Enabled)
			{
#if INTEL_ISPC
				ispc::BatchUpdateDerivedState(
					(ispc::FJointSolverJointState*)SolverConstraintStates.GetData(), 
					JointBatches[BatchIndex][0], 
					JointBatches[BatchIndex][1]);
#endif
			}
			else
			{
				for (int32 JointIndex = JointIndexBegin; JointIndex < JointIndexEnd; ++JointIndex)
				{
					SolverConstraintStates[JointIndex].UpdateDerivedState();
				}
			}

			// Update the rotation constraint axes and errors for all Joints in the batch
			for (int32 JointIndex = JointIndexBegin; JointIndex < JointIndexEnd; ++JointIndex)
			{
				SolverConstraints[JointIndex].UpdateRotationConstraints(
					SolverConstraintRowDatas, 
					SolverConstraintRowStates, 
					SolverConstraintStates[JointIndex], 
					ConstraintSettings[JointIndex]);
			}

			// Solve and apply the rotation constraints for all Joints in the batch
			const int32 AngularRowIndexBegin = SolverConstraints[JointIndexBegin].GetAngularRowIndexBegin();
			const int32 AngularRowIndexEnd = SolverConstraints[JointIndexEnd - 1].GetAngularRowIndexEnd();
			if (bChaos_Joint_ISPC_Enabled)
			{
#if INTEL_ISPC
				ispc::BatchApplyRotationConstraints(
					Dt, 
					(ispc::FJointSolverJointState*)SolverConstraintStates.GetData(), 
					(ispc::FJointSolverConstraintRowData*)SolverConstraintRowDatas.GetData(),
					(ispc::FJointSolverConstraintRowState*)SolverConstraintRowStates.GetData(),
					JointIndexBegin,
					JointIndexEnd,
					AngularRowIndexBegin,
					AngularRowIndexEnd);
#endif
			}
			else
			{
				NumActive += FJointSolver::ApplyRotationConstraints(
					Dt, 
					SolverConstraintStates, 
					SolverConstraintRowDatas, 
					SolverConstraintRowStates, 
					JointIndexBegin,
					JointIndexEnd,
					AngularRowIndexBegin,
					AngularRowIndexEnd);
			}
		}

		// Copy the updated state back to the bodies
		for (int32 JointIndex = JointIndexBegin; JointIndex < JointIndexEnd; ++JointIndex)
		{
			ScatterSolverJointState(Dt, JointIndex);
		}

		return NumActive;
	}

	// This position solver iterates over each of the inner constraints (position, twist, swing) and solves them independently.
	// This will converge slowly in some cases, particularly where resolving angular constraints violates position constraints and vice versa.
	int32 FPBDJointConstraints::ApplySingle(const FReal Dt, const int32 ConstraintIndex, const int32 NumPairIts, const int32 It, const int32 NumIts)
	{
		if (!IsConstraintEnabled(ConstraintIndex))
		{
			return 0;
		}

		const TVector<TGeometryParticleHandle<FReal, 3>*, 2>& Constraint = ConstraintParticles[ConstraintIndex];
		UE_LOG(LogChaosJoint, VeryVerbose, TEXT("Solve Joint Constraint %d %s %s (dt = %f; it = %d / %d)"), ConstraintIndex, *Constraint[0]->ToString(), *Constraint[1]->ToString(), Dt, It, NumIts);

		const FPBDJointSettings& JointSettings = ConstraintSettings[ConstraintIndex];
		FJointSolverGaussSeidel& Solver = ConstraintSolvers[ConstraintIndex];

		int32 Index0, Index1;
		GetConstrainedParticleIndices(ConstraintIndex, Index0, Index1);
		TGenericParticleHandle<FReal, 3> Particle0 = TGenericParticleHandle<FReal, 3>(ConstraintParticles[ConstraintIndex][Index0]);
		TGenericParticleHandle<FReal, 3> Particle1 = TGenericParticleHandle<FReal, 3>(ConstraintParticles[ConstraintIndex][Index1]);

		const FVec3 P0 = FParticleUtilities::GetCoMWorldPosition(Particle0);
		const FRotation3 Q0 = FParticleUtilities::GetCoMWorldRotation(Particle0);
		const FVec3 P1 = FParticleUtilities::GetCoMWorldPosition(Particle1);
		const FRotation3 Q1 = FParticleUtilities::GetCoMWorldRotation(Particle1);

		Solver.Update(
			Dt,
			P0,
			Q0,
			Particle0->V(),
			Particle0->W(),
			P1,
			Q1,
			Particle1->V(),
			Particle1->W());

		int32 NumActive = 0;
		for (int32 PairIt = 0; PairIt < NumPairIts; ++PairIt)
		{
			UE_LOG(LogChaosJoint, VeryVerbose, TEXT("  Pair Iteration %d / %d"), PairIt, NumPairIts);

			NumActive += Solver.ApplyConstraints(Dt, Settings, JointSettings);
			NumActive +=  Solver.ApplyDrives(Dt, Settings, JointSettings);

			if ((NumActive == 0) && bChaos_Joint_EarlyOut_Enabled)
			{
				break;
			}
		}

		bool bUpdateVelocity = false;	// Position-based collision solver does not need V() and W()
		UpdateParticleState(Particle0->CastToRigidParticle(), Dt, Solver.GetPrevP(0), Solver.GetPrevQ(0), Solver.GetP(0), Solver.GetQ(0), bUpdateVelocity);
		UpdateParticleState(Particle1->CastToRigidParticle(), Dt, Solver.GetPrevP(1), Solver.GetPrevQ(1), Solver.GetP(1), Solver.GetQ(1), bUpdateVelocity);

		// @todo(ccaulfield): The break limit should really be applied to the impulse in the solver to prevent 1-frame impulses larger than the threshold
		if ((JointSettings.LinearBreakForce > 0.0f) || (JointSettings.AngularBreakTorque > 0.0f))
		{
			ApplyBreakThreshold(Dt, ConstraintIndex, Solver.GetNetLinearImpulse(), Solver.GetNetAngularImpulse());
		}

		return NumActive;
	}

	int32 FPBDJointConstraints::ApplyPushOutSingle(const FReal Dt, const int32 ConstraintIndex, const int32 NumPairIts, const int32 It, const int32 NumIts)
	{
		if (!IsConstraintEnabled(ConstraintIndex))
		{
			return 0;
		}

		const TVector<TGeometryParticleHandle<FReal, 3>*, 2>& Constraint = ConstraintParticles[ConstraintIndex];
		UE_LOG(LogChaosJoint, VeryVerbose, TEXT("Project Joint Constraint %d %s %s (dt = %f; it = %d / %d)"), ConstraintIndex, *Constraint[0]->ToString(), *Constraint[1]->ToString(), Dt, It, NumIts);

		const FPBDJointSettings& JointSettings = ConstraintSettings[ConstraintIndex];
		FJointSolverGaussSeidel& Solver = ConstraintSolvers[ConstraintIndex];

		int32 Index0, Index1;
		GetConstrainedParticleIndices(ConstraintIndex, Index0, Index1);
		TGenericParticleHandle<FReal, 3> Particle0 = TGenericParticleHandle<FReal, 3>(ConstraintParticles[ConstraintIndex][Index0]);
		TGenericParticleHandle<FReal, 3> Particle1 = TGenericParticleHandle<FReal, 3>(ConstraintParticles[ConstraintIndex][Index1]);

		Solver.Update(
			Dt,
			FParticleUtilities::GetCoMWorldPosition(Particle0),
			FParticleUtilities::GetCoMWorldRotation(Particle0),
			Particle0->V(),
			Particle0->W(),
			FParticleUtilities::GetCoMWorldPosition(Particle1),
			FParticleUtilities::GetCoMWorldRotation(Particle1),
			Particle1->V(),
			Particle1->W());

		int32 NumActive = 0;
		for (int32 PairIt = 0; PairIt < NumPairIts; ++PairIt)
		{
			NumActive = Solver.ApplyProjections(Dt, Settings, JointSettings);
			
			if ((NumActive == 0) && bChaos_Joint_EarlyOut_Enabled)
			{
				break;
			}
		}

		UpdateParticleStateExplicit(Particle0->CastToRigidParticle(), Dt, Solver.GetP(0), Solver.GetQ(0), Solver.GetV(0), Solver.GetW(0));
		UpdateParticleStateExplicit(Particle1->CastToRigidParticle(), Dt, Solver.GetP(1), Solver.GetQ(1), Solver.GetV(1), Solver.GetW(1));

		// @todo(ccaulfield): should probably add to net impulses in push out too...

		return NumActive;
	}

	void FPBDJointConstraints::ApplyBreakThreshold(const FReal Dt, int32 ConstraintIndex, const FVec3& LinearImpulse, const FVec3& AngularImpulse)
	{
		const FPBDJointSettings& JointSettings = ConstraintSettings[ConstraintIndex];

		// NOTE: LinearImpulse/AngularImpulse are not really an impulses - it is a mass-weighted position/rotation delta, or (impulse x dt).
		// The Threshold is a force limit, so we need to convert it to a position delta caused by that force in one timestep

		bool bBreak = false;
		if (!bBreak && (JointSettings.LinearBreakForce > 0.0f))
		{
			const FReal LinearThreshold = JointSettings.LinearBreakForce * Dt * Dt;
			UE_LOG(LogChaosJoint, VeryVerbose, TEXT("Constraint %d Linear Break Check: %f / %f"), ConstraintIndex, LinearImpulse.Size(), LinearThreshold);

			const FReal LinearThresholdSq = LinearThreshold * LinearThreshold;
			bBreak = LinearImpulse.SizeSquared() > LinearThresholdSq;
		}

		if (!bBreak && (JointSettings.AngularBreakTorque > 0.0f))
		{
			const FReal AngularThreshold = JointSettings.AngularBreakTorque * Dt * Dt;
			UE_LOG(LogChaosJoint, VeryVerbose, TEXT("Constraint %d Angular Break Check: %f / %f"), ConstraintIndex, AngularImpulse.Size(), AngularThreshold);

			const FReal AngularThresholdSq = AngularThreshold * AngularThreshold;
			bBreak = AngularImpulse.SizeSquared() > AngularThresholdSq;
		}

		if (bBreak)
		{
			BreakConstraint(ConstraintIndex);
		}
	}


	// Assign an Island, Level and Color to each constraint. Constraints must be processed in Level order, but
	// constraints of the same color are independent and can be processed in parallel (SIMD or Task)
	// NOTE: Constraints are the Vertices, and Edges connect constraints sharing a Particle
	void FPBDJointConstraints::ColorConstraints()
	{
		// Add a Vertex for all constraints involving at least one dynamic body
		// Maintain a map from Constraint Index to Vertex Index
		FColoringGraph Graph;
		TArray<int32> ConstraintVertices; // Map of ConstraintIndex -> VertexIndex
		Graph.ReserveVertices(NumConstraints());
		ConstraintVertices.SetNumZeroed(NumConstraints());
		for (int32 ConstraintIndex = 0; ConstraintIndex < NumConstraints(); ++ConstraintIndex)
		{
			TPBDRigidParticleHandle<FReal, 3>* Particle0 = ConstraintParticles[ConstraintIndex][0]->CastToRigidParticle();
			TPBDRigidParticleHandle<FReal, 3>* Particle1 = ConstraintParticles[ConstraintIndex][1]->CastToRigidParticle();

			bool bContainsDynamic = (Particle0 != nullptr) || (Particle1 != nullptr);
			if (bContainsDynamic)
			{
				ConstraintVertices[ConstraintIndex] = Graph.AddVertex();

				// Set kinematic-connected constraints to level 0 to initialize level calculation
				bool bContainsKinematic = (Particle0 == nullptr) || (Particle1 == nullptr);
				if (bContainsKinematic)
				{
					Graph.SetVertexLevel(ConstraintVertices[ConstraintIndex], 0);
				}
			}
			else
			{
				ConstraintVertices[ConstraintIndex] = INDEX_NONE;
			}
		}

		// Also build a map of particles to constraint indices. We only care about dynamic particles since
		// two constraints that share only a kinematic particle will not interact.
		TMap<TPBDRigidParticleHandle<FReal, 3>*, TArray<int32>> ParticleConstraints; // Map of ParticleHandle -> Constraint Indices involving the particle
		for (int32 ConstraintIndex = 0; ConstraintIndex < NumConstraints(); ++ConstraintIndex)
		{
			TPBDRigidParticleHandle<FReal, 3>* Particle0 = ConstraintParticles[ConstraintIndex][0]->CastToRigidParticle();
			TPBDRigidParticleHandle<FReal, 3>* Particle1 = ConstraintParticles[ConstraintIndex][1]->CastToRigidParticle();
			if (Particle0 != nullptr)
			{
				ParticleConstraints.FindOrAdd(Particle0).Add(ConstraintIndex);
			}
			if (Particle1 != nullptr)
			{
				ParticleConstraints.FindOrAdd(Particle1).Add(ConstraintIndex);
			}
		}

		// Connect constraints that share a dynamic particle
		Graph.ReserveEdges((ParticleConstraints.Num() * (ParticleConstraints.Num() - 1)) / 2);
		for (auto& ParticleConstraintsElement : ParticleConstraints)
		{
			TArray<int32>& ParticleConstraintIndices = ParticleConstraintsElement.Value;
			int32 NumParticleConstraintIndices = ParticleConstraintIndices.Num();
			for (int32 ParticleConstraintIndex0 = 0; ParticleConstraintIndex0 < NumParticleConstraintIndices; ++ParticleConstraintIndex0)
			{
				int32 ConstraintIndex0 = ParticleConstraintIndices[ParticleConstraintIndex0];
				int32 VertexIndex0 = ConstraintVertices[ConstraintIndex0];
				for (int32 ParticleConstraintIndex1 = ParticleConstraintIndex0 + 1; ParticleConstraintIndex1 < NumParticleConstraintIndices; ++ParticleConstraintIndex1)
				{
					int32 ConstraintIndex1 = ParticleConstraintIndices[ParticleConstraintIndex1];
					int32 VertexIndex1 = ConstraintVertices[ConstraintIndex1];
					Graph.AddEdge(VertexIndex0, VertexIndex1);
				}
			}
		}

		// Colorize the graph
		Graph.Islandize();
		Graph.Levelize();
		Graph.Colorize();

		// Set the constraint colors
		for (int32 ConstraintIndex = 0; ConstraintIndex < NumConstraints(); ++ConstraintIndex)
		{
			int32 VertexIndex = ConstraintVertices[ConstraintIndex];
			ConstraintStates[ConstraintIndex].Island = Graph.GetVertexIsland(VertexIndex);
			ConstraintStates[ConstraintIndex].IslandSize = Graph.GetVertexIslandSize(VertexIndex);
			ConstraintStates[ConstraintIndex].Level = Graph.GetVertexLevel(VertexIndex);
			ConstraintStates[ConstraintIndex].Color = Graph.GetVertexColor(VertexIndex);
		}
	}

	// Assign constraints to batches based on Level and Color. A batch is all constraints that shared the same Level-Color and so may be processed in parallel.
	// NOTE: some constraints may have no dynamic bodies and therefore should be ignored (They will have Level = 0 and Color = -1).
	// @todo(ccaulfield): eliminate all the sorting (just use indices until we have the final batch ordering and then sort the actual constraint list)
	void FPBDJointConstraints::BatchConstraints()
	{
		// Reset
		for (int32 ConstraintIndex = 0; ConstraintIndex < NumConstraints(); ++ConstraintIndex)
		{
			ConstraintStates[ConstraintIndex].Island = INDEX_NONE;
			ConstraintStates[ConstraintIndex].Level = INDEX_NONE;
			ConstraintStates[ConstraintIndex].Color = INDEX_NONE;
			ConstraintStates[ConstraintIndex].Batch = INDEX_NONE;
			ConstraintStates[ConstraintIndex].IslandSize = 0;
		}

		// Assign all constraints to islands and set colors
		ColorConstraints();

		// If batching is disabled, just sort and put in one batch
		if (!bChaos_Joint_Batching)
		{
			for (int32 ConstraintIndex = 0; ConstraintIndex < NumConstraints(); ++ConstraintIndex)
			{
				ConstraintStates[ConstraintIndex].Batch = 0;
			}
			JointBatches.Reset();
			JointBatches.Add(TVector<int32, 2>(0, NumConstraints()));
			SortConstraints();
			return;
		}

		// Build the list of constraints per island
		TArray<TArray<int32>> IslandConstraints;
		for (int32 ConstraintIndex = 0; ConstraintIndex < NumConstraints(); ++ConstraintIndex)
		{
			int32 IslandIndex = ConstraintStates[ConstraintIndex].Island;
			if (IslandIndex >= IslandConstraints.Num())
			{
				IslandConstraints.SetNum(IslandIndex + 1);
			}
			IslandConstraints[IslandIndex].Add(ConstraintIndex);
		}

		// For each island, sort the constraints so that the ones to process first are at the end of the list
		// Also ensure that constraints of same color are adjacent
		for (int32 IslandIndex = 0; IslandIndex < IslandConstraints.Num(); ++IslandIndex)
		{
			IslandConstraints[IslandIndex].StableSort([this](int32 L, int32 R)
				{
					int32 LevelL = ConstraintStates[L].Level;
					int32 LevelR = ConstraintStates[R].Level;
					if (LevelL != LevelR)
					{
						return LevelL > LevelR;
					}

					int32 ColorL = ConstraintStates[L].Color;
					int32 ColorR = ConstraintStates[R].Color;
					return ColorL < ColorR;
				});
		}

		// Now assign constraints to batches of BatchSize, taking the first same-colored items from each island (which will be at the end of the island's array).
		// This way we depopulate the larger islands first, filling batches with items from smaller islands.
		int32 BatchSize = bChaos_Joint_MaxBatchSize;
		int32 NumBatches = 0;
		int32 NumItemsToBatch = NumConstraints();
		while (NumItemsToBatch > 0)
		{
			// Sort the islands so that the larger ones are first
			// @todo(ccaulfield): optimize (does it use MoveTemp?)
			IslandConstraints.StableSort([](const TArray<int32>& L, const TArray<int32>& R)
				{
					return L.Num() > R.Num();
				});

			int32 NumBatchItems = 0;
			for (int32 IslandIndex = 0; (IslandIndex < IslandConstraints.Num()) && (NumBatchItems < BatchSize); ++IslandIndex)
			{
				if (IslandConstraints[IslandIndex].Num() == 0)
				{
					// Once we hit an empty island we are done (we have sorted on island size)
					break;
				}

				// Take all the constraints of the same level and color from this island (up to batch size).
				int32 ConstraintIndex = IslandConstraints[IslandIndex].Last();
				int32 IslandBatchColor = ConstraintStates[ConstraintIndex].Color;
				while ((IslandConstraints[IslandIndex].Num() > 0) && (NumBatchItems < BatchSize))
				{
					ConstraintIndex = IslandConstraints[IslandIndex].Last();
					int32 ConstraintColor = ConstraintStates[ConstraintIndex].Color;
					if (ConstraintColor == IslandBatchColor)
					{
						IslandConstraints[IslandIndex].Pop(false);

						ConstraintStates[ConstraintIndex].Batch = NumBatches;
						++NumBatchItems;
						--NumItemsToBatch;
					}
					else
					{
						break;
					}
				}
			}
			if (NumBatchItems > 0)
			{
				++NumBatches;
			}
		}
		check(NumItemsToBatch == 0);

		// Sort constraints by batch
		SortConstraints();

		// Set up the batch begin/end indices
		JointBatches.SetNum(NumBatches);
		int32 BatchIndex = INDEX_NONE;
		for (int32 ConstraintIndex = 0; ConstraintIndex < NumConstraints(); ++ConstraintIndex)
		{
			int32 ConstraintBatchIndex = ConstraintStates[ConstraintIndex].Batch;
			if (ConstraintBatchIndex != BatchIndex)
			{
				if (BatchIndex != INDEX_NONE)
				{
					JointBatches[BatchIndex][1] = ConstraintIndex;
				}
				++BatchIndex;
				JointBatches[BatchIndex][0] = ConstraintIndex;
			}
		}
		if (BatchIndex != INDEX_NONE)
		{
			JointBatches[BatchIndex][1] = NumConstraints();
		}

		CheckBatches();
	}

	void FPBDJointConstraints::CheckBatches()
	{
#if DO_CHECK
		for (const TVector<int32, 2>& BatchRange : JointBatches)
		{
			// No two Constraints in a batch should operate on the same dynamic particle
			// TODO: validate Level (i.e., all lower level particles in same Island are in a prior batch)
			TArray<const TPBDRigidParticleHandle<FReal, 3>*> UsedParticles;
			for (int32 ConstraintIndex = BatchRange[0]; ConstraintIndex < BatchRange[1]; ++ConstraintIndex)
			{
				const TPBDRigidParticleHandle<FReal, 3>* Particle0 = ConstraintParticles[ConstraintIndex][0]->CastToRigidParticle();
				const TPBDRigidParticleHandle<FReal, 3>* Particle1 = ConstraintParticles[ConstraintIndex][1]->CastToRigidParticle();
				if (Particle0 != nullptr)
				{
					ensure(!UsedParticles.Contains(Particle0));
					UsedParticles.Add(Particle0);
				}
				if (Particle1 != nullptr)
				{
					ensure(!UsedParticles.Contains(Particle1));
					UsedParticles.Add(Particle1);
				}
			}
		}
#endif
	}
}

namespace Chaos
{
	template class TContainerConstraintHandle<FPBDJointConstraints>;
}
