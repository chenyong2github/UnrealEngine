// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/ChaosDebugDraw.h"
#include "Chaos/DebugDrawQueue.h"
#include "Chaos/Joint/PBDJointSolverCholesky.h"
#include "Chaos/Joint/PBDJointSolverGaussSeidel.h"
#include "Chaos/Particle/ParticleUtilities.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDJointConstraintUtilities.h"
#include "Chaos/Utilities.h"
#include "ChaosLog.h"
#include "ChaosStats.h"

#include "HAL/IConsoleManager.h"

//#pragma optimize("", off)

namespace Chaos
{
	DECLARE_CYCLE_STAT(TEXT("TPBDJointConstraints::Sort"), STAT_Joints_Sort, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("TPBDJointConstraints::Apply"), STAT_Joints_Apply, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("TPBDJointConstraints::ApplyPushOut"), STAT_Joints_ApplyPushOut, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("TPBDJointConstraints::Drives"), STAT_Joints_Drives, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("TPBDJointConstraints::Solve"), STAT_Joints_Solve, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("TPBDJointConstraints::SolveCholesky"), STAT_Joints_Solve_Cholesky, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("TPBDJointConstraints::SolveGaussSeidel"), STAT_Joints_Solve_GaussSeidel, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("TPBDJointConstraints::Project"), STAT_Joints_Project, STATGROUP_Chaos);

	bool ChaosJoint_UseCholeskySolver = false;
	FAutoConsoleVariableRef CVarChaosImmPhysDeltaTime(TEXT("p.Chaos.Joint.UseCholeskySolver"), ChaosJoint_UseCholeskySolver, TEXT("Whether to use the new solver"));


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

	
	void FPBDJointConstraintHandle::CalculateConstraintSpace(FVec3& OutXa, FMatrix33& OutRa, FVec3& OutXb, FMatrix33& OutRb, FVec3& OutCR) const
	{
		ConstraintContainer->CalculateConstraintSpace(ConstraintIndex, OutXa, OutRa, OutXb, OutRb, OutCR);
	}

	
	void FPBDJointConstraintHandle::SetParticleLevels(const TVector<int32, 2>& ParticleLevels)
	{
		ConstraintContainer->SetParticleLevels(ConstraintIndex, ParticleLevels);
	}

	
	int32 FPBDJointConstraintHandle::GetConstraintLevel() const
	{
		return ConstraintContainer->GetConstraintLevel(ConstraintIndex);
	}

	
	const FPBDJointSettings& FPBDJointConstraintHandle::GetSettings() const
	{
		return ConstraintContainer->GetConstraintSettings(ConstraintIndex);
	}

	TVector<TGeometryParticleHandle<float,3>*, 2> FPBDJointConstraintHandle::GetConstrainedParticles() const 
	{ 
		return ConstraintContainer->GetConstrainedParticles(ConstraintIndex); 
	}

	//
	// Constraint Settings
	//

	
	FPBDJointMotionSettings::FPBDJointMotionSettings()
		: Stiffness((FReal)1)
		, LinearProjection((FReal)0)
		, AngularProjection((FReal)0)
		, LinearMotionTypes({ EJointMotionType::Locked, EJointMotionType::Locked, EJointMotionType::Locked })
		, LinearLimit(FLT_MAX)
		, AngularMotionTypes({ EJointMotionType::Free, EJointMotionType::Free, EJointMotionType::Free })
		, AngularLimits(FVec3(FLT_MAX, FLT_MAX, FLT_MAX))
		, bSoftLinearLimitsEnabled(false)
		, bSoftTwistLimitsEnabled(false)
		, bSoftSwingLimitsEnabled(false)
		, SoftLinearStiffness(0)
		, SoftTwistStiffness(0)
		, SoftSwingStiffness(0)
		, AngularDriveTarget(FRotation3::FromIdentity())
		, AngularDriveTargetAngles(FVec3(0, 0, 0))
		, bAngularSLerpDriveEnabled(false)
		, bAngularTwistDriveEnabled(false)
		, bAngularSwingDriveEnabled(false)
		, AngularDriveStiffness(0)
	{
	}

	
	FPBDJointMotionSettings::FPBDJointMotionSettings(const TVector<EJointMotionType, 3>& InLinearMotionTypes, const TVector<EJointMotionType, 3>& InAngularMotionTypes)
		: Stiffness((FReal)1)
		, LinearProjection((FReal)0)
		, AngularProjection((FReal)0)
		, LinearMotionTypes(InLinearMotionTypes)
		, LinearLimit(FLT_MAX)
		, AngularMotionTypes({ EJointMotionType::Free, EJointMotionType::Free, EJointMotionType::Free })
		, AngularLimits(FVec3(FLT_MAX, FLT_MAX, FLT_MAX))
		, bSoftLinearLimitsEnabled(false)
		, bSoftTwistLimitsEnabled(false)
		, bSoftSwingLimitsEnabled(false)
		, SoftLinearStiffness(0)
		, SoftTwistStiffness(0)
		, SoftSwingStiffness(0)
		, AngularDriveTarget(FRotation3::FromIdentity())
		, AngularDriveTargetAngles(FVec3(0, 0, 0))
		, bAngularSLerpDriveEnabled(false)
		, bAngularTwistDriveEnabled(false)
		, bAngularSwingDriveEnabled(false)
		, AngularDriveStiffness(0)
	{
	}

	void FPBDJointMotionSettings::Sanitize()
	{
		// Reset limits if they won;t be used (means we don't have to check if limited/locked in a few cases).
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

	
	FPBDJointSettings::FPBDJointSettings()
		: ConstraintFrames({ FTransform::Identity, FTransform::Identity })
	{
	}

	
	FPBDJointState::FPBDJointState()
		: Level(INDEX_NONE)
		, ParticleLevels({ INDEX_NONE, INDEX_NONE })
	{
	}

	//
	// Solver Settings
	//

	
	FPBDJointSolverSettings::FPBDJointSolverSettings()
		: ApplyPairIterations(1)
		, ApplyPushOutPairIterations(1)
		, SwingTwistAngleTolerance((FReal)1.0e-6)
		, MinParentMassRatio(0)
		, MaxInertiaRatio(0)
		, bEnableTwistLimits(true)
		, bEnableSwingLimits(true)
		, bEnableDrives(true)
		, ProjectionPhase(EJointSolverPhase::None)
		, LinearProjection((FReal)0)
		, AngularProjection((FReal)0)
		, Stiffness((FReal)0)
		, DriveStiffness((FReal)0)
		, SoftLinearStiffness((FReal)0)
		, SoftAngularStiffness((FReal)0)
	{
	}

	//
	// Constraint Container
	//

	
	FPBDJointConstraints::FPBDJointConstraints(const FPBDJointSolverSettings& InSettings)
		: Settings(InSettings)
		, PreApplyCallback(nullptr)
		, PostApplyCallback(nullptr)
		, bRequiresSort(false)
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

	
	typename FPBDJointConstraints::FConstraintContainerHandle* FPBDJointConstraints::AddConstraint(const FParticlePair& InConstrainedParticles, const FRigidTransform3& WorldConstraintFrame)
	{
		FTransformPair ConstraintFrames;
		ConstraintFrames[0] = FRigidTransform3(
			WorldConstraintFrame.GetTranslation() - InConstrainedParticles[0]->X(),
			WorldConstraintFrame.GetRotation() * InConstrainedParticles[0]->R().Inverse()
			);
		ConstraintFrames[1] = FRigidTransform3(
			WorldConstraintFrame.GetTranslation() - InConstrainedParticles[1]->X(),
			WorldConstraintFrame.GetRotation() * InConstrainedParticles[1]->R().Inverse()
			);
		return AddConstraint(InConstrainedParticles, ConstraintFrames);
	}

	
	typename FPBDJointConstraints::FConstraintContainerHandle* FPBDJointConstraints::AddConstraint(const FParticlePair& InConstrainedParticles, const FTransformPair& ConstraintFrames)
	{
		int ConstraintIndex = Handles.Num();
		Handles.Add(HandleAllocator.AllocHandle(this, ConstraintIndex));
		ConstraintParticles.Add(InConstrainedParticles);
		ConstraintSettings.Add(FPBDJointSettings());
		ConstraintSettings[ConstraintIndex].ConstraintFrames = ConstraintFrames;
		ConstraintStates.Add(FPBDJointState());
		return Handles.Last();
	}

	
	typename FPBDJointConstraints::FConstraintContainerHandle* FPBDJointConstraints::AddConstraint(const FParticlePair& InConstrainedParticles, const FPBDJointSettings& InConstraintSettings)
	{
		int ConstraintIndex = Handles.Num();
		Handles.Add(HandleAllocator.AllocHandle(this, ConstraintIndex));
		ConstraintParticles.Add(InConstrainedParticles);
		ConstraintSettings.Add(InConstraintSettings);
		ConstraintStates.Add(FPBDJointState());
		return Handles.Last();
	}

	
	void FPBDJointConstraints::RemoveConstraint(int ConstraintIndex)
	{
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

	
	int32 FPBDJointConstraints::GetConstraintLevel(int32 ConstraintIndex) const
	{
		return ConstraintStates[ConstraintIndex].Level;
	}

	
	void FPBDJointConstraints::SetParticleLevels(int32 ConstraintIndex, const TVector<int32, 2>& ParticleLevels)
	{
		int32 NewLevel = FMath::Min(ParticleLevels[0], ParticleLevels[1]);
		int32 PreviousLevel = ConstraintStates[ConstraintIndex].Level;
		ConstraintStates[ConstraintIndex].Level = NewLevel;
		ConstraintStates[ConstraintIndex].ParticleLevels = ParticleLevels;
		bRequiresSort = bRequiresSort || (NewLevel != PreviousLevel);
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
				return L.GetConstraintLevel() < R.GetConstraintLevel();
			});

		TArray<FPBDJointSettings> SortedConstraintSettings;
		TArray<FParticlePair> SortedConstraintParticles;
		TArray<FPBDJointState> SortedConstraintStates;
		int32 SortedConstraintIndex = 0;
		for (FConstraintContainerHandle* Handle : SortedHandles)
		{
			int32 UnsortedIndex = Handle->GetConstraintIndex();
			SortedConstraintSettings.Add(ConstraintSettings[UnsortedIndex]);
			SortedConstraintParticles.Add(ConstraintParticles[UnsortedIndex]);
			SortedConstraintStates.Add(ConstraintStates[UnsortedIndex]);
			SetConstraintIndex(Handle, SortedConstraintIndex++);
		}

		Swap(ConstraintSettings, SortedConstraintSettings);
		Swap(ConstraintParticles, SortedConstraintParticles);
		Swap(ConstraintStates, SortedConstraintStates);
		Swap(Handles, SortedHandles);
	}

	
	void FPBDJointConstraints::UpdatePositionBasedState(const FReal Dt)
	{
		if (bRequiresSort)
		{
			SortConstraints();
			bRequiresSort = false;
		}
	}

	
	void FPBDJointConstraints::CalculateConstraintSpace(int32 ConstraintIndex, FVec3& OutX0, FMatrix33& OutR0, FVec3& OutX1, FMatrix33& OutR1, FVec3& OutCR) const
	{
		const int32 Index0 = 1;
		const int32 Index1 = 0;
		TGenericParticleHandle<FReal, 3> Particle0 = TGenericParticleHandle<FReal, 3>(ConstraintParticles[ConstraintIndex][Index0]);
		TGenericParticleHandle<FReal, 3> Particle1 = TGenericParticleHandle<FReal, 3>(ConstraintParticles[ConstraintIndex][Index1]);
		const FVec3 P0 = FParticleUtilities::GetCoMWorldPosition(Particle0);
		const FRotation3 Q0 = FParticleUtilities::GetCoMWorldRotation(Particle0);
		const FVec3 P1 = FParticleUtilities::GetCoMWorldPosition(Particle1);
		const FRotation3 Q1 = FParticleUtilities::GetCoMWorldRotation(Particle1);

		const FPBDJointSettings& JointSettings = ConstraintSettings[ConstraintIndex];
		const FRigidTransform3 XL0 = FParticleUtilities::ParticleLocalToCoMLocal(Particle0, JointSettings.ConstraintFrames[Index0]);
		const FRigidTransform3 XL1 = FParticleUtilities::ParticleLocalToCoMLocal(Particle1, JointSettings.ConstraintFrames[Index1]);
		EJointMotionType Swing1Motion = JointSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing1];
		EJointMotionType Swing2Motion = JointSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing2];
		if ((Swing1Motion == EJointMotionType::Limited) && (Swing2Motion == EJointMotionType::Limited))
		{
			FPBDJointUtilities::CalculateConeConstraintSpace(Settings, ConstraintSettings[ConstraintIndex], XL0, XL1, P0, Q0, P1, Q1, OutX0, OutR0, OutX1, OutR1, OutCR);
		}
		else
		{
			FPBDJointUtilities::CalculateSwingConstraintSpace(Settings, ConstraintSettings[ConstraintIndex], XL0, XL1, P0, Q0, P1, Q1, OutX0, OutR0, OutX1, OutR1, OutCR);
		}
	}

	//////////////////////////////////////////////////////////////////////////
	//
	// Begin Simple API Solver. Iterate over constraints in array order.
	//
	//////////////////////////////////////////////////////////////////////////

	void FPBDJointConstraints::Apply(const FReal Dt, const int32 It, const int32 NumIts)
	{
		SCOPE_CYCLE_COUNTER(STAT_Joints_Apply);

		if (PreApplyCallback != nullptr)
		{
			PreApplyCallback(Dt, Handles);
		}

		// Solve for joint position or velocity, depending on settings
		if (Settings.ApplyPairIterations > 0)
		{
			SolvePosition(Dt, Settings.ApplyPairIterations, It, NumIts);
		}

		if (PostApplyCallback != nullptr)
		{
			PostApplyCallback(Dt, Handles);
		}

		// Correct remaining errors after last call to Solve if enabled in this phase
		if (Settings.ProjectionPhase == EJointSolverPhase::Apply)
		{
			int32 ProjectionIt = NumIts - 1;
			if (It == ProjectionIt)
			{
				ApplyProjection(Dt);
			}
		}
	}

	bool FPBDJointConstraints::ApplyPushOut(const FReal Dt, const int32 It, const int32 NumIts)
	{
		SCOPE_CYCLE_COUNTER(STAT_Joints_ApplyPushOut);

		// @todo(ccaulfield): track whether we are sufficiently solved
		bool bNeedsAnotherIteration = true;

		// Solve for positions
		if (Settings.ApplyPushOutPairIterations > 0)
		{
			SolvePosition(Dt, Settings.ApplyPushOutPairIterations, It, NumIts);
		}

		// Correct remaining errors after the last call to Solve (which depends on if PositionSolve is enabled in ApplyPushOut)
		if (Settings.ProjectionPhase == EJointSolverPhase::ApplyPushOut)
		{
			int32 ProjectionIt = (Settings.ApplyPushOutPairIterations > 0) ? NumIts - 1 : 0;
			if (It == ProjectionIt)
			{
				ApplyProjection(Dt);
			}
		}

		return bNeedsAnotherIteration;
	}

	void FPBDJointConstraints::SolvePosition(const FReal Dt, const int32 NumPairIts, const int32 It, const int32 NumIts)
	{
		if (ChaosJoint_UseCholeskySolver)
		{
			SCOPE_CYCLE_COUNTER(STAT_Joints_Solve_Cholesky);
			for (int32 ConstraintIndex = 0; ConstraintIndex < NumConstraints(); ++ConstraintIndex)
			{
				SolvePosition_Cholesky(Dt, ConstraintIndex, NumPairIts, It, NumIts);
			}
		}
		else
		{
			SCOPE_CYCLE_COUNTER(STAT_Joints_Solve_GaussSeidel);
			for (int32 ConstraintIndex = 0; ConstraintIndex < NumConstraints(); ++ConstraintIndex)
			{
				SolvePosition_GaussSiedel(Dt, ConstraintIndex, NumPairIts, It, NumIts);
			}
		}
	}

	void FPBDJointConstraints::ApplyProjection(const FReal Dt)
	{
		SCOPE_CYCLE_COUNTER(STAT_Joints_Project);

		for (int32 ConstraintIndex = 0; ConstraintIndex < NumConstraints(); ++ConstraintIndex)
		{
			ProjectPosition(Dt, ConstraintIndex, 0, 1);
		}

		if (PostProjectCallback != nullptr)
		{
			PostProjectCallback(Dt, Handles);
		}
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

	void FPBDJointConstraints::Apply(const FReal Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const int32 It, const int32 NumIts)
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

		// Solve for joint position or velocity, depending on settings
		if (Settings.ApplyPairIterations > 0)
		{
			SCOPE_CYCLE_COUNTER(STAT_Joints_Solve);
			for (FConstraintContainerHandle* ConstraintHandle : SortedConstraintHandles)
			{
				SolvePosition(Dt, ConstraintHandle->GetConstraintIndex(), Settings.ApplyPairIterations, It, NumIts);
			}
		}

		// Correct remaining errors after last call to Solve if enabled in this phase
		if (Settings.ProjectionPhase == EJointSolverPhase::Apply)
		{
			SCOPE_CYCLE_COUNTER(STAT_Joints_Project);
			int32 ProjectionIt = NumIts - 1;
			if (It == ProjectionIt)
			{
				ApplyProjection(Dt, InConstraintHandles);
			}
		}

		if (PostApplyCallback != nullptr)
		{
			PostApplyCallback(Dt, SortedConstraintHandles);
		}
	}

	
	bool FPBDJointConstraints::ApplyPushOut(const FReal Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const int32 It, const int32 NumIts)
	{
		SCOPE_CYCLE_COUNTER(STAT_Joints_ApplyPushOut);

		// @todo(ccaulfield): track whether we are sufficiently solved
		bool bNeedsAnotherIteration = true;

		TArray<FConstraintContainerHandle*> SortedConstraintHandles = InConstraintHandles;
		SortedConstraintHandles.Sort([](const FConstraintContainerHandle& L, const FConstraintContainerHandle& R)
			{
				// Sort bodies from root to leaf
				return L.GetConstraintLevel() < R.GetConstraintLevel();
			});

		// Solve for positions
		if (Settings.ApplyPushOutPairIterations > 0)
		{
			SCOPE_CYCLE_COUNTER(STAT_Joints_Solve);

			for (FConstraintContainerHandle* ConstraintHandle : SortedConstraintHandles)
			{
				SolvePosition(Dt, ConstraintHandle->GetConstraintIndex(), Settings.ApplyPushOutPairIterations, It, NumIts);
			}
		}

		// Correct remaining errors after the last call to Solve (which depends on if PositionSolve is enabled in ApplyPushOut)
		if (Settings.ProjectionPhase == EJointSolverPhase::ApplyPushOut)
		{
			SCOPE_CYCLE_COUNTER(STAT_Joints_Project);
			int32 ProjectionIt = (Settings.ApplyPushOutPairIterations > 0) ? NumIts - 1 : 0;
			if (It == ProjectionIt)
			{
				ApplyProjection(Dt, InConstraintHandles);
			}
		}

		return bNeedsAnotherIteration;
	}

	
	void FPBDJointConstraints::ApplyProjection(const FReal Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles)
	{
		TArray<FConstraintContainerHandle*> SortedConstraintHandles = InConstraintHandles;
		SortedConstraintHandles.Sort([](const FConstraintContainerHandle& L, const FConstraintContainerHandle& R)
			{
				// Sort bodies from root to leaf
				return L.GetConstraintLevel() < R.GetConstraintLevel();
			});

		for (FConstraintContainerHandle* ConstraintHandle : SortedConstraintHandles)
		{
			ProjectPosition(Dt, ConstraintHandle->GetConstraintIndex(), 0, 1);
		}
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

	void FPBDJointConstraints::UpdateParticleState(TPBDRigidParticleHandle<FReal, 3>* Rigid, const FReal Dt, const FVec3& P, const FRotation3& Q, const bool bUpdateVelocity)
	{
		if ((Rigid != nullptr) && (Rigid->ObjectState() == EObjectStateType::Dynamic))
		{
			if (bUpdateVelocity && (Dt > SMALL_NUMBER))
			{
				FVec3 PCoM = FParticleUtilities::GetCoMWorldPosition(Rigid);
				FRotation3 QCoM = FParticleUtilities::GetCoMWorldRotation(Rigid);
				FVec3 DV = FVec3::CalculateVelocity(PCoM, P, Dt);
				FVec3 DW = FRotation3::CalculateAngularVelocity(QCoM, Q, Dt);
				Rigid->SetV(Rigid->V() + DV);
				Rigid->SetW(Rigid->W() + DW);
			}
			FParticleUtilities::SetCoMWorldTransform(Rigid, P, Q);
		}
	}


	void FPBDJointConstraints::SolvePosition(const FReal Dt, const int32 ConstraintIndex, const int32 NumPairIts, const int32 It, const int32 NumIts)
	{
		if (ChaosJoint_UseCholeskySolver)
		{
			SolvePosition_Cholesky(Dt, ConstraintIndex, NumPairIts, It, NumIts);
		}
		else
		{
			SolvePosition_GaussSiedel(Dt, ConstraintIndex, NumPairIts, It, NumIts);
		}
	}


	// This position solver solves all (active) inner position and angular constraints simultaneously by building the Jacobian and solving [JMJt].DX = C
	// where DX(6x1) are the unknown position and rotation corrections, C(Nx1) is the current constraint error, J(Nx6) the Jacobian, M(6x6) the inverse mass matrix, 
	// and N the number of active constraints. "Active constraints" are all bilateral constraints plus any violated unilateral constraints.
	void FPBDJointConstraints::SolvePosition_Cholesky(const FReal Dt, const int32 ConstraintIndex, const int32 NumPairIts, const int32 It, const int32 NumIts)
	{
		const TVector<TGeometryParticleHandle<FReal, 3>*, 2>& Constraint = ConstraintParticles[ConstraintIndex];
		UE_LOG(LogChaosJoint, VeryVerbose, TEXT("Solve Joint Constraint %d %s %s (dt = %f; it = %d / %d)"), ConstraintIndex, *Constraint[0]->ToString(), *Constraint[1]->ToString(), Dt, It, NumIts);

		const FPBDJointSettings& JointSettings = ConstraintSettings[ConstraintIndex];

		// Switch particles - internally we assume the first body is the parent (i.e., the space in which constraint limits are specified)
		const int32 Index0 = 1;
		const int32 Index1 = 0;
		TGenericParticleHandle<FReal, 3> Particle0 = TGenericParticleHandle<FReal, 3>(ConstraintParticles[ConstraintIndex][Index0]);
		TGenericParticleHandle<FReal, 3> Particle1 = TGenericParticleHandle<FReal, 3>(ConstraintParticles[ConstraintIndex][Index1]);

		FJointSolverCholesky Solver;
		Solver.InitConstraints(
			Dt, 
			Settings, 
			JointSettings, 
			FParticleUtilities::GetCoMWorldPosition(Particle0),
			FParticleUtilities::GetCoMWorldRotation(Particle0),
			FParticleUtilities::GetCoMWorldPosition(Particle1),
			FParticleUtilities::GetCoMWorldRotation(Particle1),
			Particle0->InvM(), 
			Particle0->InvI(), 
			Particle1->InvM(), 
			Particle1->InvI(), 
			FParticleUtilities::ParticleLocalToCoMLocal(Particle0, JointSettings.ConstraintFrames[Index0]),
			FParticleUtilities::ParticleLocalToCoMLocal(Particle1, JointSettings.ConstraintFrames[Index1]));
		
		for (int32 PairIt = 0; PairIt < NumPairIts; ++PairIt)
		{
			Solver.ApplyDrives(Dt, JointSettings);
			Solver.ApplyConstraints(Dt, JointSettings);
		}

		UpdateParticleState(Particle0->CastToRigidParticle(), Dt, Solver.GetP(0), Solver.GetQ(0));
		UpdateParticleState(Particle1->CastToRigidParticle(), Dt, Solver.GetP(1), Solver.GetQ(1));
	}


	// This position solver iterates over each of the inner constraints (position, twist, swing) and solves them independently.
	// This will converge slowly in some cases, particularly where resolving angular constraints violates position constraints and vice versa.
	void FPBDJointConstraints::SolvePosition_GaussSiedel(const FReal Dt, const int32 ConstraintIndex, const int32 NumPairIts, const int32 It, const int32 NumIts)
	{
		const TVector<TGeometryParticleHandle<FReal, 3>*, 2>& Constraint = ConstraintParticles[ConstraintIndex];
		UE_LOG(LogChaosJoint, VeryVerbose, TEXT("Solve Joint Constraint %d %s %s (dt = %f; it = %d / %d)"), ConstraintIndex, *Constraint[0]->ToString(), *Constraint[1]->ToString(), Dt, It, NumIts);

		const FPBDJointSettings& JointSettings = ConstraintSettings[ConstraintIndex];

		// Switch particles - internally we assume the first body is the parent (i.e., the space in which constraint limits are specified)
		const int32 Index0 = 1;
		const int32 Index1 = 0;
		TGenericParticleHandle<FReal, 3> Particle0 = TGenericParticleHandle<FReal, 3>(ConstraintParticles[ConstraintIndex][Index0]);
		TGenericParticleHandle<FReal, 3> Particle1 = TGenericParticleHandle<FReal, 3>(ConstraintParticles[ConstraintIndex][Index1]);

		FJointSolverGaussSeidel Solver;
		Solver.InitConstraints(
			Dt,
			Settings,
			JointSettings,
			FParticleUtilities::GetCoMWorldPosition(Particle0),
			FParticleUtilities::GetCoMWorldRotation(Particle0),
			FParticleUtilities::GetCoMWorldPosition(Particle1),
			FParticleUtilities::GetCoMWorldRotation(Particle1),
			Particle0->InvM(),
			Particle0->InvI(),
			Particle1->InvM(),
			Particle1->InvI(),
			FParticleUtilities::ParticleLocalToCoMLocal(Particle0, JointSettings.ConstraintFrames[Index0]),
			FParticleUtilities::ParticleLocalToCoMLocal(Particle1, JointSettings.ConstraintFrames[Index1]));

		for (int32 PairIt = 0; PairIt < NumPairIts; ++PairIt)
		{
			Solver.ApplyDrives(Dt, JointSettings);
			Solver.ApplyConstraints(Dt, JointSettings);
		}

		UpdateParticleState(Particle0->CastToRigidParticle(), Dt, Solver.GetP(0), Solver.GetQ(0));
		UpdateParticleState(Particle1->CastToRigidParticle(), Dt, Solver.GetP(1), Solver.GetQ(1));
	}


	void FPBDJointConstraints::ProjectPosition(const FReal Dt, const int32 ConstraintIndex, const int32 It, const int32 NumIts)
	{
		const FPBDJointSettings& JointSettings = ConstraintSettings[ConstraintIndex];

		// Scale projection up to ProjectionSetting over NumProjectionIts
		const FReal LinearProjectionFactor = (Settings.LinearProjection > 0) ? Settings.LinearProjection : JointSettings.Motion.LinearProjection;
		const FReal AngularProjectionFactor = (Settings.AngularProjection > 0) ? Settings.AngularProjection : JointSettings.Motion.AngularProjection;
		if ((LinearProjectionFactor == (FReal)0) && (AngularProjectionFactor == (FReal)0))
		{
			return;
		}

		const TVector<TGeometryParticleHandle<FReal, 3>*, 2>& Constraint = ConstraintParticles[ConstraintIndex];
		UE_LOG(LogChaosJoint, VeryVerbose, TEXT("Project Joint Constraint %d %f %f (it = %d / %d)"), ConstraintIndex, LinearProjectionFactor, AngularProjectionFactor, It, NumIts);

		// Switch particles - internally we assume the first body is the parent (i.e., the space in which constraint limits are specified)
		const int32 Index0 = 1;
		const int32 Index1 = 0;
		TGenericParticleHandle<FReal, 3> Particle0 = TGenericParticleHandle<FReal, 3>(ConstraintParticles[ConstraintIndex][Index0]);
		TGenericParticleHandle<FReal, 3> Particle1 = TGenericParticleHandle<FReal, 3>(ConstraintParticles[ConstraintIndex][Index1]);

		FVec3 P0 = FParticleUtilities::GetCoMWorldPosition(Particle0);
		FRotation3 Q0 = FParticleUtilities::GetCoMWorldRotation(Particle0);
		FVec3 P1 = FParticleUtilities::GetCoMWorldPosition(Particle1);
		FRotation3 Q1 = FParticleUtilities::GetCoMWorldRotation(Particle1);
		const FRigidTransform3 XL0 = FParticleUtilities::ParticleLocalToCoMLocal(Particle0, JointSettings.ConstraintFrames[Index0]);
		const FRigidTransform3 XL1 = FParticleUtilities::ParticleLocalToCoMLocal(Particle1, JointSettings.ConstraintFrames[Index1]);
		float InvM0 = Particle0->InvM();
		float InvM1 = Particle1->InvM();
		FMatrix33 InvIL0 = Particle0->InvI();
		FMatrix33 InvIL1 = Particle1->InvI();

		// Freeze the closest to kinematic connection if there is a difference
		const int32 Level0 = ConstraintStates[ConstraintIndex].ParticleLevels[Index0];
		const int32 Level1 = ConstraintStates[ConstraintIndex].ParticleLevels[Index1];
		if (Level0 < Level1)
		{
			InvM0 = 0;
			InvIL0 = FMatrix33(0, 0, 0);
		}
		else if (Level1 < Level0)
		{
			InvM1 = 0;
			InvIL1 = FMatrix33(0, 0, 0);
		}

		const TVector<EJointMotionType, 3>& LinearMotion = JointSettings.Motion.LinearMotionTypes;
		EJointMotionType TwistMotion = JointSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Twist];
		EJointMotionType Swing1Motion = JointSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing1];
		EJointMotionType Swing2Motion = JointSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing2];

		if (AngularProjectionFactor > 0)
		{
			if (Settings.bEnableTwistLimits)
			{
				// Remove Twist Error
				FPBDJointUtilities::ApplyJointTwistProjection(Dt, Settings, JointSettings, AngularProjectionFactor, XL0, XL1, P0, Q0, P1, Q1, InvM0, InvIL0, InvM1, InvIL1);
			}

			if (Settings.bEnableSwingLimits)
			{
				// Remove Swing Error
				if ((Swing1Motion == EJointMotionType::Limited) && (Swing2Motion == EJointMotionType::Limited))
				{
					FPBDJointUtilities::ApplyJointConeProjection(Dt, Settings, JointSettings, AngularProjectionFactor, XL0, XL1, P0, Q0, P1, Q1, InvM0, InvIL0, InvM1, InvIL1);
				}
				else
				{
					if (Swing1Motion != EJointMotionType::Free)
					{
						FPBDJointUtilities::ApplyJointSwingProjection(Dt, Settings, JointSettings, AngularProjectionFactor, XL0, XL1, EJointAngularConstraintIndex::Swing1, EJointAngularAxisIndex::Swing1, P0, Q0, P1, Q1, InvM0, InvIL0, InvM1, InvIL1);
					}
					if (Swing2Motion != EJointMotionType::Free)
					{
						FPBDJointUtilities::ApplyJointSwingProjection(Dt, Settings, JointSettings, AngularProjectionFactor, XL0, XL1, EJointAngularConstraintIndex::Swing2, EJointAngularAxisIndex::Swing2, P0, Q0, P1, Q1, InvM0, InvIL0, InvM1, InvIL1);
					}
				}
			}
		}

		// Remove Position Error
		if (LinearProjectionFactor > 0)
		{
			if ((LinearMotion[0] != EJointMotionType::Free) || (LinearMotion[1] != EJointMotionType::Free) || (LinearMotion[2] != EJointMotionType::Free))
			{
				FPBDJointUtilities::ApplyJointPositionProjection(Dt, Settings, JointSettings, LinearProjectionFactor, XL0, XL1, P0, Q0, P1, Q1, InvM0, InvIL0, InvM1, InvIL1);
			}
		}

		// Update the particles
		UpdateParticleState(Particle0->CastToRigidParticle(), Dt, P0, Q0, false);
		UpdateParticleState(Particle1->CastToRigidParticle(), Dt, P1, Q1, false);
	}
}

namespace Chaos
{
	template class TContainerConstraintHandle<FPBDJointConstraints>;
}
