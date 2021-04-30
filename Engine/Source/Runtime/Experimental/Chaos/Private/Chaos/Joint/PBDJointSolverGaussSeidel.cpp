// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Joint/PBDJointSolverGaussSeidel.h"
#include "Chaos/Joint/ChaosJointLog.h"
#include "Chaos/Joint/JointConstraintsCVars.h"
#include "Chaos/Joint/JointSolverConstraints.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDJointConstraintUtilities.h"
#include "Chaos/Utilities.h"
#include "ChaosStats.h"
#if INTEL_ISPC
#include "PBDJointSolverGaussSeidel.ispc.generated.h"
#endif

//PRAGMA_DISABLE_OPTIMIZATION

namespace Chaos
{


	//
	//
	//////////////////////////////////////////////////////////////////////////
	//
	//

	FJointSolverGaussSeidel::FJointSolverGaussSeidel()
	{
		if (bChaos_Joint_ISPC_Enabled)
		{
#if INTEL_ISPC
			check(sizeof(FJointSolverGaussSeidel) == ispc::SizeofFJointSolverGaussSeidel());
#endif
		}
	}


	void FJointSolverGaussSeidel::InitDerivedState()
	{
		InitXs[0] = InitPs[0] + InitQs[0] * XLs[0].GetTranslation();
		InitXs[1] = InitPs[1] + InitQs[1] * XLs[1].GetTranslation();
		InitRs[0] = InitQs[0] * XLs[0].GetRotation();
		InitRs[1] = InitQs[1] * XLs[1].GetRotation();
		InitRs[1].EnforceShortestArcWith(InitRs[0]);

		Xs[0] = InitXs[0];
		Rs[0] = InitRs[0];
		InvIs[0] = (InvMs[0] > 0.0f) ? Utilities::ComputeWorldSpaceInertia(InitQs[0], InvILs[0]) : FMatrix33(0, 0, 0);

		Xs[1] = InitXs[1];
		Rs[1] = InitRs[1];
		InvIs[1] = (InvMs[1] > 0.0f) ? Utilities::ComputeWorldSpaceInertia(InitQs[1], InvILs[1]) : FMatrix33(0, 0, 0);
	}


	void FJointSolverGaussSeidel::UpdateDerivedState()
	{
		// Kinematic bodies will not be moved, so we don't update derived state during iterations
		if (InvMs[0] > 0.0f)
		{
			Xs[0] = Ps[0] + Qs[0] * XLs[0].GetTranslation();
			Rs[0] = Qs[0] * XLs[0].GetRotation();
			InvIs[0] = Utilities::ComputeWorldSpaceInertia(Qs[0], InvILs[0]);
		}
		if (InvMs[1] > 0.0f)
		{
			Xs[1] = Ps[1] + Qs[1] * XLs[1].GetTranslation();
			Rs[1] = Qs[1] * XLs[1].GetRotation();
			InvIs[1] = Utilities::ComputeWorldSpaceInertia(Qs[1], InvILs[1]);
		}
		Rs[1].EnforceShortestArcWith(Rs[0]);
	}


	void FJointSolverGaussSeidel::UpdateDerivedState(const int32 BodyIndex)
	{
		Xs[BodyIndex] = Ps[BodyIndex] + Qs[BodyIndex] * XLs[BodyIndex].GetTranslation();
		Rs[BodyIndex] = Qs[BodyIndex] * XLs[BodyIndex].GetRotation();
		Rs[1].EnforceShortestArcWith(Rs[0]);
	
		InvIs[BodyIndex] = Utilities::ComputeWorldSpaceInertia(Qs[BodyIndex], InvILs[BodyIndex]);
	}

	bool FJointSolverGaussSeidel::UpdateIsActive(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		// NumActiveConstraints is initialized to -1, so there's no danger of getting invalid LastPs/Qs
		// We also check SolverStiffness mainly for testing when solver stiffness is 0 (so we don't exit immediately)
		if ((NumActiveConstraints >= 0) && (SolverStiffness > 0.0f))
		{
			bool bIsSolved =
				FVec3::IsNearlyEqual(Ps[0], LastPs[0], PositionTolerance)
				&& FVec3::IsNearlyEqual(Ps[1], LastPs[1], PositionTolerance)
				&& FRotation3::IsNearlyEqual(Qs[0], LastQs[0], 0.5f * AngleTolerance)
				&& FRotation3::IsNearlyEqual(Qs[1], LastQs[1], 0.5f * AngleTolerance);
			bIsActive = !bIsSolved;
		}

		LastPs[0] = Ps[0];
		LastPs[1] = Ps[1];
		LastQs[0] = Qs[0];
		LastQs[1] = Qs[1];

		return bIsActive;
	}


	void FJointSolverGaussSeidel::Init(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FVec3& PrevP0,
		const FVec3& PrevP1,
		const FRotation3& PrevQ0,
		const FRotation3& PrevQ1,
		const FReal InvM0,
		const FVec3& InvIL0,
		const FReal InvM1,
		const FVec3& InvIL1,
		const FRigidTransform3& XL0,
		const FRigidTransform3& XL1)
	{
		XLs[0] = XL0;
		XLs[1] = XL1;

		InvILs[0] = JointSettings.ParentInvMassScale * InvIL0;
		InvILs[1] = InvIL1;
		InvMs[0] = JointSettings.ParentInvMassScale * InvM0;
		InvMs[1] = InvM1;

		FPBDJointUtilities::ConditionInverseMassAndInertia(InvMs[0], InvMs[1], InvILs[0], InvILs[1], SolverSettings.MinParentMassRatio, SolverSettings.MaxInertiaRatio);

		InitPs[0] = PrevP0;
		InitPs[1] = PrevP1;
		InitQs[0] = PrevQ0;
		InitQs[1] = PrevQ1;

		NetLinearImpulse = FVec3(0);
		NetAngularImpulse = FVec3(0);

		LinearSoftLambda = 0;
		TwistSoftLambda = 0;
		SwingSoftLambda = 0;
		LinearDriveLambdas = FVec3(0);
		RotationDriveLambdas = FVec3(0);

		LinearConstraintPadding = FVec3(-1);
		AngularConstraintPadding = FVec3(-1);

		// Tolerances are positional errors below visible detection. But in PBD the errors
		// we leave behind get converted to velocity, so we need to ensure that the resultant
		// movement from that erroneous velocity is less than the desired position tolerance.
		// Assume that the tolerances were defined for a 60Hz simulation, then it must be that
		// the position error is less than the position change from constant external forces
		// (e.g., gravity). So, we are saying that the tolerance was chosen because the position
		// error is less that F.dt^2. We need to scale the tolerance to work at our current dt.
		const FReal ToleranceScale = FMath::Min(1.0f, 60.0f * 60.0f * Dt * Dt);
		PositionTolerance = ToleranceScale * SolverSettings.PositionTolerance;
		AngleTolerance = ToleranceScale * SolverSettings.AngleTolerance;

		NumActiveConstraints = -1;
		bIsActive = true;

		SolverStiffness = 1.0f;

		InitDerivedState();
	}


	void FJointSolverGaussSeidel::Update(
		const FReal Dt,
		const FReal InSolverStiffness,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FVec3& P0,
		const FRotation3& Q0,
		const FVec3& V0,
		const FVec3& W0,
		const FVec3& P1,
		const FRotation3& Q1,
		const FVec3& V1,
		const FVec3& W1)
	{
		Ps[0] = P0;
		Ps[1] = P1;
		Qs[0] = Q0;
		Qs[1] = Q1;
		Qs[1].EnforceShortestArcWith(Qs[0]);

		Vs[0] = V0;
		Vs[1] = V1;
		Ws[0] = W0;
		Ws[1] = W1;

		SolverStiffness = InSolverStiffness;

		UpdateDerivedState();

		UpdateIsActive(Dt, SolverSettings, JointSettings);
	}


	void FJointSolverGaussSeidel::ApplyConstraints(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		NumActiveConstraints = 0;

		if (bChaos_Joint_EnableMatrixSolve)
		{
			ApplyConstraintsMatrix(Dt, SolverSettings, JointSettings);
		}
		else
		{
			ApplyPositionConstraints(Dt, SolverSettings, JointSettings);
			ApplyRotationConstraints(Dt, SolverSettings, JointSettings);
		}
	
		ApplyPositionDrives(Dt, SolverSettings, JointSettings);
		ApplyRotationDrives(Dt, SolverSettings, JointSettings);

		UpdateIsActive(Dt, SolverSettings, JointSettings);
	}


	void FJointSolverGaussSeidel::ApplyProjections(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		if (InvMs[1] < SMALL_NUMBER)
		{
			// If child is kinematic, return. 
			return;
		}
		
		const FReal LinearProjection = FPBDJointUtilities::GetLinearProjection(SolverSettings, JointSettings);
		const FReal AngularProjection = FPBDJointUtilities::GetAngularProjection(SolverSettings, JointSettings);

		FVec3 DP1 = FVec3(0);
		FVec3 DR1 = FVec3(0);

		// Position Projection
		const bool bLinearSoft = FPBDJointUtilities::GetSoftLinearLimitEnabled(SolverSettings, JointSettings);
		const bool bLinearProjectionEnabled = (bLinearSoft && JointSettings.bSoftProjectionEnabled) || (!bLinearSoft && JointSettings.bProjectionEnabled);
		const TVec3<EJointMotionType>& LinearMotion = JointSettings.LinearMotionTypes;
		const bool bLinearLocked =
			(LinearMotion[0] == EJointMotionType::Locked)
			&& (LinearMotion[1] == EJointMotionType::Locked)
			&& (LinearMotion[2] == EJointMotionType::Locked);
		const bool bLinearLimited =
			(LinearMotion[0] == EJointMotionType::Limited)
			&& (LinearMotion[1] == EJointMotionType::Limited)
			&& (LinearMotion[2] == EJointMotionType::Limited);
		if (bLinearProjectionEnabled && (LinearProjection > 0))
		{
			if (bLinearLocked)
			{
				ApplyPointProjection(Dt, SolverSettings, JointSettings, LinearProjection, DP1, DR1);
			}
			else if (bLinearLimited)
			{
				ApplySphereProjection(Dt, SolverSettings, JointSettings, LinearProjection, DP1, DR1);
			}
			// @todo(ccaulfield): support mixed linear projection
		}

		// Twist projection
		const bool bTwistSoft = FPBDJointUtilities::GetSoftTwistLimitEnabled(SolverSettings, JointSettings);
		const bool bTwistProjectionEnabled = SolverSettings.bEnableTwistLimits && ((bTwistSoft && JointSettings.bSoftProjectionEnabled) || (!bTwistSoft && JointSettings.bProjectionEnabled));
		if (bTwistProjectionEnabled && (AngularProjection > 0.0f))
		{
			const EJointMotionType TwistMotion = JointSettings.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Twist];
			if (TwistMotion != EJointMotionType::Free)
			{
				ApplyTwistProjection(Dt, SolverSettings, JointSettings, AngularProjection, bLinearLocked, DP1, DR1);
			}
		}

		// Swing projection
		const bool bSwingSoft = FPBDJointUtilities::GetSoftSwingLimitEnabled(SolverSettings, JointSettings);
		const bool bSwingProjectionEnabled = SolverSettings.bEnableSwingLimits && ((bSwingSoft && JointSettings.bSoftProjectionEnabled) || (!bSwingSoft && JointSettings.bProjectionEnabled));
		if (bSwingProjectionEnabled && (AngularProjection > 0.0f))
		{
			const EJointMotionType Swing1Motion = JointSettings.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing1];
			const EJointMotionType Swing2Motion = JointSettings.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing2];
			if ((Swing1Motion == EJointMotionType::Limited) && (Swing2Motion == EJointMotionType::Limited))
			{
				ApplyConeProjection(Dt, SolverSettings, JointSettings, AngularProjection, bLinearLocked, DP1, DR1);
			}
			else if ((Swing1Motion == EJointMotionType::Limited) && (Swing2Motion == EJointMotionType::Locked))
			{
				ApplySingleLockedSwingProjection(Dt, SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing2, AngularProjection, bLinearLocked, DP1, DR1);
				ApplySwingProjection(Dt, SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing1, AngularProjection, bLinearLocked, DP1, DR1);
			}
			else if ((Swing1Motion == EJointMotionType::Limited) && (Swing2Motion == EJointMotionType::Free))
			{
				ApplyDualConeSwingProjection(Dt, SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing1, AngularProjection, bLinearLocked, DP1, DR1);
			}
			else if ((Swing1Motion == EJointMotionType::Locked) && (Swing2Motion == EJointMotionType::Limited))
			{
				ApplySingleLockedSwingProjection(Dt, SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing1, AngularProjection, bLinearLocked, DP1, DR1);
				ApplySwingProjection(Dt, SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing2, AngularProjection, bLinearLocked, DP1, DR1);
			}
			else if ((Swing1Motion == EJointMotionType::Locked) && (Swing2Motion == EJointMotionType::Locked))
			{
				ApplyDoubleLockedSwingProjection(Dt, SolverSettings, JointSettings, AngularProjection, bLinearLocked, DP1, DR1);
			}
			else if ((Swing1Motion == EJointMotionType::Locked) && (Swing2Motion == EJointMotionType::Free))
			{
				ApplySingleLockedSwingProjection(Dt, SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing1, AngularProjection, bLinearLocked, DP1, DR1);
			}
			else if ((Swing1Motion == EJointMotionType::Free) && (Swing2Motion == EJointMotionType::Limited))
			{
				ApplyDualConeSwingProjection(Dt, SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing2, AngularProjection, bLinearLocked, DP1, DR1);
			}
			else if ((Swing1Motion == EJointMotionType::Free) && (Swing2Motion == EJointMotionType::Locked))
			{
				ApplySingleLockedSwingProjection(Dt, SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing2, AngularProjection, bLinearLocked, DP1, DR1);
			}
		}

		// Final position fixup
		if (bLinearProjectionEnabled && (LinearProjection > 0))
		{
			ApplyTranslateProjection(Dt, SolverSettings, JointSettings, LinearProjection, DP1, DR1);
		}

		// Add velocity correction from the net projection motion
		if (Chaos_Joint_VelProjectionAlpha > 0.0f)
		{
			ApplyVelocityProjection(Dt, SolverSettings, JointSettings, Chaos_Joint_VelProjectionAlpha, DP1, DR1);
		}
	
		UpdateIsActive(Dt, SolverSettings, JointSettings);
	}


	void FJointSolverGaussSeidel::ApplyRotationConstraints(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		bool bHasRotationConstraints =
			(JointSettings.AngularMotionTypes[0] != EJointMotionType::Free)
			|| (JointSettings.AngularMotionTypes[1] != EJointMotionType::Free)
			|| (JointSettings.AngularMotionTypes[2] != EJointMotionType::Free);
		if (!bHasRotationConstraints)
		{
			return;
		}

		// Locked axes always use hard constraints. Limited axes use hard or soft depending on settings
		EJointMotionType TwistMotion = JointSettings.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Twist];
		EJointMotionType Swing1Motion = JointSettings.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing1];
		EJointMotionType Swing2Motion = JointSettings.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing2];
		bool bTwistSoft = FPBDJointUtilities::GetSoftTwistLimitEnabled(SolverSettings, JointSettings);
		bool bSwingSoft = FPBDJointUtilities::GetSoftSwingLimitEnabled(SolverSettings, JointSettings);

		// If the twist axes are opposing, we cannot decompose the orientation into swing and twist angles, so just give up
		const FVec3 Twist0 = Rs[0] * FJointConstants::TwistAxis();
		const FVec3 Twist1 = Rs[1] * FJointConstants::TwistAxis();
		const FReal Twist01Dot = FVec3::DotProduct(Twist0, Twist1);
		const bool bDegenerate = (Twist01Dot < Chaos_Joint_DegenerateRotationLimit);
		if (bDegenerate)
		{
			UE_LOG(LogChaosJoint, VeryVerbose, TEXT(" Degenerate rotation at Swing %f deg"), FMath::RadiansToDegrees(FMath::Acos(Twist01Dot)));
		}

		// Apply twist constraint
		// NOTE: Cannot calculate twist angle at 180degree swing
		if (SolverSettings.bEnableTwistLimits && !bDegenerate)
		{
			if (TwistMotion == EJointMotionType::Limited)
			{
				ApplyTwistConstraint(Dt, SolverSettings, JointSettings, bTwistSoft);
			}
			else if (TwistMotion == EJointMotionType::Locked)
			{
				// Covered below
			}
			else if (TwistMotion == EJointMotionType::Free)
			{
			}
		}

		// Apply swing constraints
		// NOTE: Cannot separate swing angles at 180degree swing (but we can still apply locks)
		if (SolverSettings.bEnableSwingLimits)
		{
			if ((Swing1Motion == EJointMotionType::Limited) && (Swing2Motion == EJointMotionType::Limited))
			{
				ApplyConeConstraint(Dt, SolverSettings, JointSettings, bSwingSoft);
			}
			else if ((Swing1Motion == EJointMotionType::Limited) && (Swing2Motion == EJointMotionType::Locked))
			{
				ApplySingleLockedSwingConstraint(Dt, SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing2, false);
				if (!bDegenerate)
				{
					ApplySwingConstraint(Dt, SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing1, bSwingSoft);
				}
			}
			else if ((Swing1Motion == EJointMotionType::Limited) && (Swing2Motion == EJointMotionType::Free))
			{
				if (!bDegenerate)
				{
					ApplyDualConeSwingConstraint(Dt, SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing1, bSwingSoft);
				}
			}
			else if ((Swing1Motion == EJointMotionType::Locked) && (Swing2Motion == EJointMotionType::Limited))
			{
				ApplySingleLockedSwingConstraint(Dt, SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing1, false);
				if (!bDegenerate)
				{
					ApplySwingConstraint(Dt, SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing2, bSwingSoft);
				}
			}
			else if ((Swing1Motion == EJointMotionType::Locked) && (Swing2Motion == EJointMotionType::Locked))
			{
				// Covered below
			}
			else if ((Swing1Motion == EJointMotionType::Locked) && (Swing2Motion == EJointMotionType::Free))
			{
				ApplySingleLockedSwingConstraint(Dt, SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing1, false);
			}
			else if ((Swing1Motion == EJointMotionType::Free) && (Swing2Motion == EJointMotionType::Limited))
			{
				if (!bDegenerate)
				{
					ApplyDualConeSwingConstraint(Dt, SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing2, bSwingSoft);
				}
			}
			else if ((Swing1Motion == EJointMotionType::Free) && (Swing2Motion == EJointMotionType::Locked))
			{
				ApplySingleLockedSwingConstraint(Dt, SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing2, false);
			}
			else if ((Swing1Motion == EJointMotionType::Free) && (Swing2Motion == EJointMotionType::Free))
			{
			}
		}

		// Note: single-swing locks are already handled above so we only need to do something here if both are locked
		bool bLockedTwist = SolverSettings.bEnableTwistLimits 
			&& (JointSettings.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Twist] == EJointMotionType::Locked);
		bool bLockedSwing = SolverSettings.bEnableSwingLimits 
			&& (JointSettings.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing1] == EJointMotionType::Locked) 
			&& (JointSettings.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing2] == EJointMotionType::Locked);
		if (bLockedTwist || bLockedSwing)
		{
			ApplyLockedRotationConstraints(Dt, SolverSettings, JointSettings, bLockedTwist, bLockedSwing);
		}
	}


	void FJointSolverGaussSeidel::ApplyRotationDrives(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		bool bHasRotationDrives =
			JointSettings.bAngularTwistPositionDriveEnabled
			|| JointSettings.bAngularTwistVelocityDriveEnabled
			|| JointSettings.bAngularSwingPositionDriveEnabled
			|| JointSettings.bAngularSwingVelocityDriveEnabled
			|| JointSettings.bAngularSLerpPositionDriveEnabled
			|| JointSettings.bAngularSLerpVelocityDriveEnabled;
		if (!bHasRotationDrives)
		{
			return;
		}

		EJointMotionType TwistMotion = JointSettings.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Twist];
		EJointMotionType Swing1Motion = JointSettings.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing1];
		EJointMotionType Swing2Motion = JointSettings.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing2];

		if (SolverSettings.bEnableDrives)
		{
			bool bTwistLocked = TwistMotion == EJointMotionType::Locked;
			bool bSwing1Locked = Swing1Motion == EJointMotionType::Locked;
			bool bSwing2Locked = Swing2Motion == EJointMotionType::Locked;

			// No SLerp drive if we have a locked rotation (it will be grayed out in the editor in this case, but could still have been set before the rotation was locked)
			// @todo(ccaulfield): setting should be cleaned up before being passed to the solver
			if ((JointSettings.bAngularSLerpPositionDriveEnabled || JointSettings.bAngularSLerpVelocityDriveEnabled) && !bTwistLocked && !bSwing1Locked && !bSwing2Locked)
			{
				ApplySLerpDrive(Dt, SolverSettings, JointSettings);
			}
			else
			{
				const bool bTwistDriveEnabled = ((JointSettings.bAngularTwistPositionDriveEnabled || JointSettings.bAngularTwistVelocityDriveEnabled) && !bTwistLocked);
				const bool bSwingDriveEnabled = (JointSettings.bAngularSwingPositionDriveEnabled || JointSettings.bAngularSwingVelocityDriveEnabled);
				const bool bSwing1DriveEnabled = bSwingDriveEnabled && !bSwing1Locked;
				const bool bSwing2DriveEnabled = bSwingDriveEnabled && !bSwing2Locked;
				if (bTwistDriveEnabled || bSwing1DriveEnabled || bSwing2DriveEnabled)
				{
					ApplySwingTwistDrives(Dt, SolverSettings, JointSettings, bTwistDriveEnabled, bSwing1DriveEnabled, bSwing2DriveEnabled);
				}
			}
		}
	}


	void FJointSolverGaussSeidel::ApplyPositionConstraints(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		bool bHasPositionConstraints =
			(JointSettings.LinearMotionTypes[0] != EJointMotionType::Free)
			|| (JointSettings.LinearMotionTypes[1] != EJointMotionType::Free)
			|| (JointSettings.LinearMotionTypes[2] != EJointMotionType::Free);
		if (!bHasPositionConstraints)
		{
			return;
		}

		const TVec3<EJointMotionType>& LinearMotion = JointSettings.LinearMotionTypes;
		const TVec3<bool> bLinearLocked =
		{
			(LinearMotion[0] == EJointMotionType::Locked),
			(LinearMotion[1] == EJointMotionType::Locked),
			(LinearMotion[2] == EJointMotionType::Locked),
		};
		const TVec3<bool> bLinearLimted =
		{
			(LinearMotion[0] == EJointMotionType::Limited),
			(LinearMotion[1] == EJointMotionType::Limited),
			(LinearMotion[2] == EJointMotionType::Limited),
		};

		if (bLinearLocked[0] && bLinearLocked[1] && bLinearLocked[2])
		{
			// Hard point constraint (most common case)
			if (InvMs[0] == 0)
			{
				ApplyPointPositionConstraintKD(0, 1, Dt, SolverSettings, JointSettings);
			}
			else if (InvMs[1] == 0)
			{
				ApplyPointPositionConstraintKD(1, 0, Dt, SolverSettings, JointSettings);
			}
			else
			{
				ApplyPointPositionConstraintDD(Dt, SolverSettings, JointSettings);
			}
		}
		else if (bLinearLimted[0] && bLinearLimted[1] && bLinearLimted[2])
		{
			// Spherical constraint
			ApplySphericalPositionConstraint(Dt, SolverSettings, JointSettings);
		}
		else if (bLinearLocked[1] && bLinearLocked[2] && !bLinearLocked[0])
		{
			// Line constraint along X axis
			ApplyCylindricalPositionConstraint(Dt, 0, LinearMotion[0], EJointMotionType::Locked, SolverSettings, JointSettings);
		}
		else if (bLinearLocked[0] && bLinearLocked[2] && !bLinearLocked[1])
		{
			// Line constraint along Y axis
			ApplyCylindricalPositionConstraint(Dt, 1, LinearMotion[1], EJointMotionType::Locked, SolverSettings, JointSettings);
		}
		else if (bLinearLocked[0] && bLinearLocked[1] && !bLinearLocked[2])
		{
			// Line constraint along Z axis
			ApplyCylindricalPositionConstraint(Dt, 2, LinearMotion[2], EJointMotionType::Locked, SolverSettings, JointSettings);
		}
		else if (bLinearLimted[1] && bLinearLimted[2] && !bLinearLimted[0])
		{
			// Cylindrical constraint along X axis
			ApplyCylindricalPositionConstraint(Dt, 0, LinearMotion[0], EJointMotionType::Limited, SolverSettings, JointSettings);
		}
		else if (bLinearLimted[0] && bLinearLimted[2] && !bLinearLimted[1])
		{
			// Cylindrical constraint along Y axis
			ApplyCylindricalPositionConstraint(Dt, 1, LinearMotion[1], EJointMotionType::Limited, SolverSettings, JointSettings);
		}
		else if (bLinearLimted[0] && bLinearLimted[1] && !bLinearLimted[2])
		{
			// Cylindrical constraint along Z axis
			ApplyCylindricalPositionConstraint(Dt, 2, LinearMotion[2], EJointMotionType::Limited, SolverSettings, JointSettings);
		}
		else if (bLinearLocked[0] || bLinearLimted[0])
		{
			// Planar constraint along X axis
			ApplyPlanarPositionConstraint(Dt, 0, LinearMotion[0], SolverSettings, JointSettings);
		}
		else if (bLinearLocked[1] || bLinearLimted[1])
		{
			// Planar constraint along Y axis
			ApplyPlanarPositionConstraint(Dt, 1, LinearMotion[1], SolverSettings, JointSettings);
		}
		else if (bLinearLocked[2] || bLinearLimted[2])
		{
			// Planar constraint along Z axis
			ApplyPlanarPositionConstraint(Dt, 2, LinearMotion[2], SolverSettings, JointSettings);
		}
	}


	void FJointSolverGaussSeidel::ApplyPositionDrives(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		if (SolverSettings.bEnableDrives)
		{
			TVec3<bool> bDriven =
			{
				(JointSettings.bLinearPositionDriveEnabled[0] || JointSettings.bLinearVelocityDriveEnabled[0]) && (JointSettings.LinearMotionTypes[0] != EJointMotionType::Locked),
				(JointSettings.bLinearPositionDriveEnabled[1] || JointSettings.bLinearVelocityDriveEnabled[1]) && (JointSettings.LinearMotionTypes[1] != EJointMotionType::Locked),
				(JointSettings.bLinearPositionDriveEnabled[2] || JointSettings.bLinearVelocityDriveEnabled[2]) && (JointSettings.LinearMotionTypes[2] != EJointMotionType::Locked),
			};

			// Rectangular position drives
			if (bDriven[0] || bDriven[1] || bDriven[2])
			{
				const FMatrix33 R0M = Rs[0].ToMatrix();
				const FVec3 XTarget = Xs[0] + Rs[0] * JointSettings.LinearDrivePositionTarget;
				const FVec3 VTarget = Rs[0] * JointSettings.LinearDriveVelocityTarget;
				const FVec3 CX = Xs[1] - XTarget;

				for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
				{
					if (bDriven[AxisIndex])
					{
						const FVec3 Axis = R0M.GetAxis(AxisIndex);
						const FReal DeltaPos = FVec3::DotProduct(CX, Axis);
						const FReal DeltaVel = FVec3::DotProduct(VTarget, Axis);

						ApplyPositionDrive(Dt, AxisIndex, SolverSettings, JointSettings, Axis, DeltaPos, DeltaVel);
					}
				}
			}
		}
	}

	//
	//
	//////////////////////////////////////////////////////////////////////////
	//
	//


	void FJointSolverGaussSeidel::ApplyPositionDelta(
		const int32 BodyIndex,
		const FVec3& DP)
	{
		//UE_LOG(LogChaosJoint, VeryVerbose, TEXT("      Apply DP%d %f %f %f"), BodyIndex, DP.X, DP.Y, DP.Z);

		Ps[BodyIndex] += DP;

		Xs[BodyIndex] += DP;
	}


	void FJointSolverGaussSeidel::ApplyPositionDelta(
		const FVec3& DP0,
		const FVec3& DP1)
	{
		//UE_LOG(LogChaosJoint, VeryVerbose, TEXT("      Apply DP%d %f %f %f"), 0, DP0.X, DP0.Y, DP0.Z);
		//UE_LOG(LogChaosJoint, VeryVerbose, TEXT("      Apply DP%d %f %f %f"), 1, DP1.X, DP1.Y, DP1.Z);

		Ps[0] += DP0;
		Ps[1] += DP1;

		Xs[0] += DP0;
		Xs[1] += DP1;
	}


	void FJointSolverGaussSeidel::ApplyRotationDelta(
		const int32 BodyIndex,
		const FVec3& DR)
	{
		//UE_LOG(LogChaosJoint, VeryVerbose, TEXT("      Apply DR%d %f %f %f"), BodyIndex, DR.X, DR.Y, DR.Z);

		const FRotation3 DQ = (FRotation3::FromElements(DR, 0) * Qs[BodyIndex]) * (FReal)0.5;
		Qs[BodyIndex] = (Qs[BodyIndex] + DQ).GetNormalized();
		Qs[1].EnforceShortestArcWith(Qs[0]);

		UpdateDerivedState(BodyIndex);
	}


	void FJointSolverGaussSeidel::ApplyRotationDelta(
		const FVec3& DR0,
		const FVec3& DR1)
	{
		//UE_LOG(LogChaosJoint, VeryVerbose, TEXT("      Apply DR%d %f %f %f"), 0, DR0.X, DR0.Y, DR0.Z);
		//UE_LOG(LogChaosJoint, VeryVerbose, TEXT("      Apply DR%d %f %f %f"), 1, DR1.X, DR1.Y, DR1.Z);

		if (bRealTypeCompatibleWithISPC && bChaos_Joint_ISPC_Enabled)
		{
#if INTEL_ISPC
			ispc::ApplyRotationDelta2((ispc::FJointSolverGaussSeidel*)this, (ispc::FVector&)DR0, (ispc::FVector&)DR1);
#endif
		}
		else
		{
			if (InvMs[0] > 0.0f)
			{
				const FRotation3 DQ0 = (FRotation3::FromElements(DR0, 0) * Qs[0]) * (FReal)0.5;
				Qs[0] = (Qs[0] + DQ0).GetNormalized();
			}
			if (InvMs[1] > 0.0f)
			{
				const FRotation3 DQ1 = (FRotation3::FromElements(DR1, 0) * Qs[1]) * (FReal)0.5;
				Qs[1] = (Qs[1] + DQ1).GetNormalized();
			}
			Qs[1].EnforceShortestArcWith(Qs[0]);

			UpdateDerivedState();
		}
	}

	void FJointSolverGaussSeidel::ApplyDelta(
		const int32 BodyIndex,
		const FVec3& DP,
		const FVec3& DR)
	{
		//UE_LOG(LogChaosJoint, VeryVerbose, TEXT("      Apply DP%d %f %f %f"), BodyIndex, DP.X, DP.Y, DP.Z);
		//UE_LOG(LogChaosJoint, VeryVerbose, TEXT("      Apply DR%d %f %f %f"), BodyIndex, DR.X, DR.Y, DR.Z);

		Ps[BodyIndex] += DP;
		const FRotation3 DQ = (FRotation3::FromElements(DR, 0) * Qs[BodyIndex]) * (FReal)0.5;
		Qs[BodyIndex] = (Qs[BodyIndex] + DQ).GetNormalized();
		Qs[1].EnforceShortestArcWith(Qs[0]);

		UpdateDerivedState(BodyIndex);
	}


	void FJointSolverGaussSeidel::ApplyVelocityDelta(
		const int32 BodyIndex,
		const FVec3& DV,
		const FVec3& DW)
	{
		Vs[BodyIndex] = Vs[BodyIndex] + DV;
		Ws[BodyIndex] = Ws[BodyIndex] + DW;
	}


	void FJointSolverGaussSeidel::ApplyVelocityDelta(
		const FVec3& DV0,
		const FVec3& DW0,
		const FVec3& DV1,
		const FVec3& DW1)
	{
		Vs[0] += DV0;
		Vs[1] += DV1;
		Ws[0] += DW0;
		Ws[1] += DW1;
	}


	void FJointSolverGaussSeidel::ApplyPositionConstraint(
		const FReal JointStiffness,
		const FVec3& Axis,
		const FReal Delta)
	{
		const FReal Stiffness = SolverStiffness * JointStiffness;

		const FVec3 AngularAxis0 = FVec3::CrossProduct(Xs[0] - Ps[0], Axis);
		const FVec3 AngularAxis1 = FVec3::CrossProduct(Xs[1] - Ps[1], Axis);
		const FVec3 IA0 = Utilities::Multiply(InvIs[0], AngularAxis0);
		const FVec3 IA1 = Utilities::Multiply(InvIs[1], AngularAxis1);

		// Joint-space inverse mass
		const FReal II0 = FVec3::DotProduct(AngularAxis0, IA0);
		const FReal II1 = FVec3::DotProduct(AngularAxis1, IA1);
		const FReal IM = InvMs[0] + II0 + InvMs[1] + II1;

		// Apply constraint correction
		const FVec3 DX = Axis * (Stiffness * Delta / IM);
		const FVec3 DP0 = InvMs[0] * DX;
		const FVec3 DP1 = -InvMs[1] * DX;
		const FVec3 DR0 = Utilities::Multiply(InvIs[0], FVec3::CrossProduct(Xs[0] - Ps[0], DX));
		const FVec3 DR1 = Utilities::Multiply(InvIs[1], FVec3::CrossProduct(Xs[1] - Ps[1], -DX));

		ApplyPositionDelta(DP0, DP1);
		ApplyRotationDelta(DR0, DR1);

		NetLinearImpulse += DX;

		++NumActiveConstraints;
	}


	void FJointSolverGaussSeidel::ApplyPositionConstraintSoft(
		const FReal Dt,
		const FReal JointStiffness,
		const FReal JointDamping,
		const bool bAccelerationMode,
		const FVec3& Axis,
		const FReal Delta,
		const FReal TargetVel,
		FReal& Lambda)
	{
		if (bRealTypeCompatibleWithISPC && bChaos_Joint_ISPC_Enabled)
		{
#if INTEL_ISPC
			ispc::ApplyPositionConstraintSoft((ispc::FJointSolverGaussSeidel*)this, Dt, JointStiffness, JointDamping, bAccelerationMode, (ispc::FVector&)Axis, Delta, TargetVel, Lambda);
#endif
		}
		else
		{
			// Joint-space inverse mass
			const FVec3 AngularAxis0 = FVec3::CrossProduct(Xs[0] - Ps[0], Axis);
			const FVec3 AngularAxis1 = FVec3::CrossProduct(Xs[1] - Ps[1], Axis);
			const FVec3 IA0 = Utilities::Multiply(InvIs[0], AngularAxis0);
			const FVec3 IA1 = Utilities::Multiply(InvIs[1], AngularAxis1);
			const FReal II0 = FVec3::DotProduct(AngularAxis0, IA0);
			const FReal II1 = FVec3::DotProduct(AngularAxis1, IA1);
			const FReal II = (InvMs[0] + II0 + InvMs[1] + II1);
	
			FReal VelDt = 0;
			if (JointDamping > KINDA_SMALL_NUMBER)
			{
				const FVec3 V0Dt = FVec3::CalculateVelocity(InitXs[0], Xs[0], 1.0f);
				const FVec3 V1Dt = FVec3::CalculateVelocity(InitXs[1], Xs[1], 1.0f);
				VelDt = TargetVel * Dt + FVec3::DotProduct(V0Dt - V1Dt, Axis);
			}
	
			const FReal SpringMassScale = (bAccelerationMode) ? 1.0f / (InvMs[0] + InvMs[1]) : 1.0f;
			const FReal S = SpringMassScale * JointStiffness * Dt * Dt;
			const FReal D = SpringMassScale * JointDamping * Dt;
			const FReal Multiplier = (FReal)1 / ((S + D) * II + (FReal)1);
			const FReal DLambda = SolverStiffness * Multiplier * (S * Delta - D * VelDt - Lambda);
	
			const FVec3 DP0 = (InvMs[0] * DLambda) * Axis;
			const FVec3 DP1 = (-InvMs[1] * DLambda) * Axis;
			const FVec3 DR0 = DLambda * Utilities::Multiply(InvIs[0], AngularAxis0);
			const FVec3 DR1 = -DLambda * Utilities::Multiply(InvIs[1], AngularAxis1);
	
			ApplyPositionDelta(DP0, DP1);
			ApplyRotationDelta(DR0, DR1);

			Lambda += DLambda;
			NetLinearImpulse += DLambda * Axis;
		}

		++NumActiveConstraints;
	}
	

	void FJointSolverGaussSeidel::ApplyRotationConstraintKD(
		const int32 KIndex,
		const int32 DIndex,
		const FReal JointStiffness,
		const FVec3& Axis,
		const FReal Angle)
	{
		const FReal Stiffness = SolverStiffness * JointStiffness;

		const FVec3 IA1 = Utilities::Multiply(InvIs[DIndex], Axis);
		const FReal II1 = FVec3::DotProduct(Axis, IA1);
		const FReal DR = Stiffness * (Angle / II1);
		const FVec3 DR1 = IA1 * -DR;
		ApplyRotationDelta(DIndex, DR1);

		NetAngularImpulse += (KIndex == 0 )? DR * Axis : -DR * Axis;
	}


	void FJointSolverGaussSeidel::ApplyRotationConstraintDD(
		const FReal JointStiffness,
		const FVec3& Axis,
		const FReal Angle)
	{
		const FReal Stiffness = SolverStiffness * JointStiffness;

		// Joint-space inverse mass
		const FVec3 IA0 = Utilities::Multiply(InvIs[0], Axis);
		const FVec3 IA1 = Utilities::Multiply(InvIs[1], Axis);
		const FReal II0 = FVec3::DotProduct(Axis, IA0);
		const FReal II1 = FVec3::DotProduct(Axis, IA1);

		const FReal DR = Stiffness * Angle / (II0 + II1);
		const FVec3 DR0 = IA0 * DR;
		const FVec3 DR1 = IA1 * -DR;

		ApplyRotationDelta(DR0, DR1);

		NetAngularImpulse += Axis * DR;
	}


	void FJointSolverGaussSeidel::ApplyRotationConstraint(
		const FReal JointStiffness,
		const FVec3& Axis,
		const FReal Angle)
	{
		if (InvMs[0] == 0)
		{
			ApplyRotationConstraintKD(0, 1, JointStiffness, Axis, Angle);
		}
		else if (InvMs[1] == 0)
		{
			ApplyRotationConstraintKD(1, 0, JointStiffness, Axis, -Angle);
		}
		else
		{
			ApplyRotationConstraintDD(JointStiffness, Axis, Angle);
		}

		++NumActiveConstraints;
	}


	// See "XPBD: Position-Based Simulation of Compliant Constrained Dynamics"
	void FJointSolverGaussSeidel::ApplyRotationConstraintSoftKD(
		const int32 KIndex,
		const int32 DIndex,
		const FReal Dt,
		const FReal JointStiffness,
		const FReal JointDamping,
		const bool bAccelerationMode,
		const FVec3& Axis,
		const FReal Angle,
		const FReal AngVelTarget,
		FReal& Lambda)
	{
		check(InvMs[DIndex] > 0);

		if (bRealTypeCompatibleWithISPC && bChaos_Joint_ISPC_Enabled)
		{
#if INTEL_ISPC
			ispc::ApplyRotationConstraintSoftKD((ispc::FJointSolverGaussSeidel*)this, KIndex, DIndex, Dt, JointStiffness, JointDamping, bAccelerationMode, (ispc::FVector&) Axis, Angle, AngVelTarget, Lambda);
#endif
		}
		else
		{
			// World-space inverse mass
			const FVec3 IA1 = Utilities::Multiply(InvIs[DIndex], Axis);

			// Joint-space inverse mass
			FReal II1 = FVec3::DotProduct(Axis, IA1);
			const FReal II = II1;

			// Damping angular velocity
			FReal AngVelDt = 0;
			if (JointDamping > KINDA_SMALL_NUMBER)
			{
				const FVec3 W0Dt = FRotation3::CalculateAngularVelocity(InitRs[KIndex], Rs[KIndex], 1.0f);
				const FVec3 W1Dt = FRotation3::CalculateAngularVelocity(InitRs[DIndex], Rs[DIndex], 1.0f);
				AngVelDt = AngVelTarget * Dt + FVec3::DotProduct(Axis, W0Dt - W1Dt);
			}

			const FReal SpringMassScale = (bAccelerationMode) ? 1.0f / II : 1.0f;
			const FReal S = SpringMassScale * JointStiffness * Dt * Dt;
			const FReal D = SpringMassScale * JointDamping * Dt;
			const FReal Multiplier = (FReal)1 / ((S + D) * II + (FReal)1);
			const FReal DLambda = SolverStiffness * Multiplier * (S * Angle - D * AngVelDt - Lambda);

			//const FVec3 DR1 = IA1 * -DLambda;
			const FVec3 DR1 = Axis * -(DLambda * II1);

			ApplyRotationDelta(DIndex, DR1);
	
			Lambda += DLambda;
			NetAngularImpulse += (KIndex == 0 ? 1 : -1) * DLambda * Axis;
		}
	}

	// See "XPBD: Position-Based Simulation of Compliant Constrained Dynamics"
	void FJointSolverGaussSeidel::ApplyRotationConstraintSoftDD(
		const FReal Dt,
		const FReal JointStiffness,
		const FReal JointDamping,
		const bool bAccelerationMode,
		const FVec3& Axis,
		const FReal Angle,
		const FReal AngVelTarget,
		FReal& Lambda)
	{
		check(InvMs[0] > 0);
		check(InvMs[1] > 0);

		if (bRealTypeCompatibleWithISPC && bChaos_Joint_ISPC_Enabled)
		{
#if INTEL_ISPC
			ispc::ApplyRotationConstraintSoftDD((ispc::FJointSolverGaussSeidel*)this, Dt, JointStiffness, JointDamping, bAccelerationMode, (ispc::FVector&) Axis, Angle, AngVelTarget, Lambda);
#endif
		}
		else
		{
			// World-space inverse mass
			const FVec3 IA0 = Utilities::Multiply(InvIs[0], Axis);
			const FVec3 IA1 = Utilities::Multiply(InvIs[1], Axis);

			// Joint-space inverse mass
			FReal II0 = FVec3::DotProduct(Axis, IA0);
			FReal II1 = FVec3::DotProduct(Axis, IA1);
			const FReal II = (II0 + II1);

			// Damping angular velocity
			FReal AngVelDt = 0;
			if (JointDamping > KINDA_SMALL_NUMBER)
			{
				const FVec3 W0Dt = FRotation3::CalculateAngularVelocity(InitRs[0], Rs[0], 1.0f);
				const FVec3 W1Dt = FRotation3::CalculateAngularVelocity(InitRs[1], Rs[1], 1.0f);
				AngVelDt = AngVelTarget * Dt + FVec3::DotProduct(Axis, W0Dt - W1Dt);
			}

			const FReal SpringMassScale = (bAccelerationMode) ? 1.0f / II : 1.0f;
			const FReal S = SpringMassScale * JointStiffness * Dt * Dt;
			const FReal D = SpringMassScale * JointDamping * Dt;
			const FReal Multiplier = (FReal)1 / ((S + D) * II + (FReal)1);
			const FReal DLambda = SolverStiffness * Multiplier * (S * Angle - D * AngVelDt - Lambda);

			//const FVec3 DR0 = IA0 * DLambda;
			//const FVec3 DR1 = IA1 * -DLambda;
			const FVec3 DR0 = Axis * (DLambda * II0);
			const FVec3 DR1 = Axis * -(DLambda * II1);

			ApplyRotationDelta(DR0, DR1);

			Lambda += DLambda;
			NetAngularImpulse += DLambda * Axis;
		}
	}

	void FJointSolverGaussSeidel::ApplyRotationConstraintSoft(
		const FReal Dt,
		const FReal JointStiffness,
		const FReal JointDamping,
		const bool bAccelerationMode,
		const FVec3& Axis,
		const FReal Angle,
		const FReal AngVelTarget,
		FReal& Lambda)
	{
		if (InvMs[0] == 0)
		{
			ApplyRotationConstraintSoftKD(0, 1, Dt, JointStiffness, JointDamping, bAccelerationMode, Axis, Angle, AngVelTarget, Lambda);
		}
		else if (InvMs[1] == 0)
		{
			ApplyRotationConstraintSoftKD(1, 0, Dt, JointStiffness, JointDamping, bAccelerationMode, Axis, -Angle, -AngVelTarget, Lambda);
		}
		else
		{
			ApplyRotationConstraintSoftDD(Dt, JointStiffness, JointDamping, bAccelerationMode, Axis, Angle, AngVelTarget, Lambda);
		}

		++NumActiveConstraints;
	}

	// Used for non-zero restitution. We pad constraints by an amount such that the velocity
	// calculated after solving constraint positions will as required for the restitution.
	void FJointSolverGaussSeidel::CalculateLinearConstraintPadding(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FReal Restitution,
		const int32 AxisIndex,
		const FVec3 Axis,
		FReal& InOutPos)
	{
		// NOTE: We only calculate the padding after the constraint is first violated, and after
		// that the padding is fixed for the rest of the iterations in the current step.
		if ((Restitution > 0.0f) && (InOutPos > 0.0f) && !HasLinearConstraintPadding(AxisIndex))
		{
			SetLinearConstraintPadding(AxisIndex, 0.0f);

			// Calculate the velocity we want to match
			const FVec3 V0Dt = FVec3::CalculateVelocity(InitXs[0], Xs[0], 1.0f);
			const FVec3 V1Dt = FVec3::CalculateVelocity(InitXs[1], Xs[1], 1.0f);
			const FReal AxisVDt = FVec3::DotProduct(V1Dt - V0Dt, Axis);

			// Calculate the padding to apply to the constraint that will result in the
			// desired outward velocity (assuming the constraint is fully resolved)
			const FReal Padding = (1.0f + Restitution) * AxisVDt - InOutPos;
			if (Padding > 0.0f)
			{
				SetLinearConstraintPadding(AxisIndex, Padding);
				InOutPos += Padding;
			}
		}
	}

	// Used for non-zero restitution. We pad constraints by an amount such that the velocity
	// calculated after solving constraint positions will as required for the restitution.
	void FJointSolverGaussSeidel::CalculateAngularConstraintPadding(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FReal Restitution,
		const EJointAngularConstraintIndex ConstraintIndex,
		const FVec3 Axis,
		FReal& InOutAngle)
	{
		// NOTE: We only calculate the padding after the constraint is first violated, and after
		// that the padding is fixed for the rest of the iterations in the current step.
		if ((Restitution > 0.0f) && (InOutAngle > 0.0f) && !HasAngularConstraintPadding(ConstraintIndex))
		{
			SetAngularConstraintPadding(ConstraintIndex, 0.0f);

			// Calculate the velocity we want to match
			const FVec3 W0Dt = FRotation3::CalculateAngularVelocity(InitRs[0], Rs[0], 1.0f);
			const FVec3 W1Dt = FRotation3::CalculateAngularVelocity(InitRs[1], Rs[1], 1.0f);
			const FReal AxisWDt = FVec3::DotProduct(W1Dt - W0Dt, Axis);

			// Calculate the padding to apply to the constraint that will result in the
			// desired outward velocity (assuming the constraint is fully resolved)
			const FReal Padding = (1.0f + Restitution) * AxisWDt - InOutAngle;
			if (Padding > 0.0f)
			{
				SetAngularConstraintPadding(ConstraintIndex, Padding);
				InOutAngle += Padding;
			}
		}
	}

	//
	//
	//////////////////////////////////////////////////////////////////////////
	//
	//

	void FJointSolverGaussSeidel::ApplyLockedRotationConstraints(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const bool bApplyTwist,
		const bool bApplySwing)
	{
		FVec3 Axis0, Axis1, Axis2;
		FPBDJointUtilities::GetLockedRotationAxes(Rs[0], Rs[1], Axis0, Axis1, Axis2);

		const FRotation3 R01 = Rs[0].Inverse() * Rs[1];

		if (bApplyTwist)
		{
			FReal TwistStiffness = FPBDJointUtilities::GetTwistStiffness(SolverSettings, JointSettings);
			ApplyRotationConstraint(TwistStiffness, Axis0, R01.X);
		}

		if (bApplySwing)
		{
			FReal SwingStiffness = FPBDJointUtilities::GetSwingStiffness(SolverSettings, JointSettings);
			ApplyRotationConstraint(SwingStiffness, Axis1, R01.Y);
			ApplyRotationConstraint(SwingStiffness, Axis2, R01.Z);
		}
	}

	void FJointSolverGaussSeidel::ApplyTwistConstraint(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const bool bUseSoftLimit)
	{
		FVec3 TwistAxis;
		FReal TwistAngle;
		FPBDJointUtilities::GetTwistAxisAngle(Rs[0], Rs[1], TwistAxis, TwistAngle);

		// Calculate the twist correction to apply to each body
		const FReal LimitPadding = GetAngularConstraintPadding(EJointAngularConstraintIndex::Twist);
		FReal DTwistAngle = 0;
		FReal TwistAngleMax = FMath::Max(JointSettings.AngularLimits[(int32)EJointAngularConstraintIndex::Twist] - LimitPadding, 0.0f);
		if (TwistAngle > TwistAngleMax)
		{
			DTwistAngle = TwistAngle - TwistAngleMax;
		}
		else if (TwistAngle < -TwistAngleMax)
		{
			// Keep Twist error positive
			DTwistAngle = -TwistAngle - TwistAngleMax;
			TwistAxis = -TwistAxis;
		}

		UE_LOG(LogChaosJoint, VeryVerbose, TEXT("    Twist Angle %f [Limit %f]"), FMath::RadiansToDegrees(TwistAngle), FMath::RadiansToDegrees(TwistAngleMax));

		// Apply twist correction
		if (DTwistAngle > AngleTolerance)
		{
			if (bUseSoftLimit)
			{
				const FReal TwistStiffness = FPBDJointUtilities::GetSoftTwistStiffness(SolverSettings, JointSettings);
				const FReal TwistDamping = FPBDJointUtilities::GetSoftTwistDamping(SolverSettings, JointSettings);
				const bool bAccelerationMode = FPBDJointUtilities::GetAngularSoftAccelerationMode(SolverSettings, JointSettings);
				ApplyRotationConstraintSoft(Dt, TwistStiffness, TwistDamping, bAccelerationMode, TwistAxis, DTwistAngle, 0.0f, TwistSoftLambda);
			}
			else
			{
				if (JointSettings.TwistRestitution > 0.0f)
				{
					CalculateAngularConstraintPadding(Dt, SolverSettings, JointSettings, JointSettings.TwistRestitution, EJointAngularConstraintIndex::Twist, TwistAxis, DTwistAngle);
				}

				FReal TwistStiffness = FPBDJointUtilities::GetTwistStiffness(SolverSettings, JointSettings);
				ApplyRotationConstraint(TwistStiffness, TwistAxis, DTwistAngle);
			}
		}
	}

	void FJointSolverGaussSeidel::ApplyConeConstraint(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const bool bUseSoftLimit)
	{
		FVec3 SwingAxisLocal;
		FReal DSwingAngle = 0.0f;

		const FReal LimitPadding = GetAngularConstraintPadding(EJointAngularConstraintIndex::Swing1);
		const FReal Swing1Limit = FMath::Max(JointSettings.AngularLimits[(int32)EJointAngularConstraintIndex::Swing1] - LimitPadding, 0.0f);
		const FReal Swing2Limit = FMath::Max(JointSettings.AngularLimits[(int32)EJointAngularConstraintIndex::Swing2] - LimitPadding, 0.0f);
		FPBDJointUtilities::GetEllipticalConeAxisErrorLocal(Rs[0], Rs[1], Swing2Limit, Swing1Limit, SwingAxisLocal, DSwingAngle);

		UE_LOG(LogChaosJoint, VeryVerbose, TEXT("    Cone Error %f [Limits %f %f]"), FMath::RadiansToDegrees(DSwingAngle), FMath::RadiansToDegrees(Swing2Limit), FMath::RadiansToDegrees(Swing1Limit));

		const FVec3 SwingAxis = Rs[0] * SwingAxisLocal;

		// Apply swing correction to each body
		if (DSwingAngle > AngleTolerance)
		{
			if (bUseSoftLimit)
			{
				const FReal SoftSwingStiffness = FPBDJointUtilities::GetSoftSwingStiffness(SolverSettings, JointSettings);
				const FReal SoftSwingDamping = FPBDJointUtilities::GetSoftSwingDamping(SolverSettings, JointSettings);
				const bool bAccelerationMode = FPBDJointUtilities::GetAngularSoftAccelerationMode(SolverSettings, JointSettings);
				ApplyRotationConstraintSoft(Dt, SoftSwingStiffness, SoftSwingDamping, bAccelerationMode, SwingAxis, DSwingAngle, 0.0f, SwingSoftLambda);
			}
			else
			{
				if (JointSettings.SwingRestitution > 0.0f)
				{
					CalculateAngularConstraintPadding(Dt, SolverSettings, JointSettings, JointSettings.SwingRestitution, EJointAngularConstraintIndex::Swing1, SwingAxis, DSwingAngle);
				}

				FReal SwingStiffness = FPBDJointUtilities::GetSwingStiffness(SolverSettings, JointSettings);
				ApplyRotationConstraint(SwingStiffness, SwingAxis, DSwingAngle);
			}
		}
	}


	void FJointSolverGaussSeidel::ApplySingleLockedSwingConstraint(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const EJointAngularConstraintIndex SwingConstraintIndex,
		const bool bUseSoftLimit)
	{
		// NOTE: SwingAxis is not normalized in this mode. It has length Sin(SwingAngle).
		// Likewise, the SwingAngle is actually Sin(SwingAngle)
		FVec3 SwingAxis;
		FReal SwingAngle;
		FPBDJointUtilities::GetLockedSwingAxisAngle(Rs[0], Rs[1], SwingConstraintIndex, SwingAxis, SwingAngle);

		UE_LOG(LogChaosJoint, VeryVerbose, TEXT("    LockedSwing%d Angle %f [Tolerance %f]"), (SwingConstraintIndex == EJointAngularConstraintIndex::Swing1) ? 1 : 2, FMath::RadiansToDegrees(SwingAngle), FMath::RadiansToDegrees(AngleTolerance));

		// Apply swing correction
		FReal DSwingAngle = SwingAngle;
		if (FMath::Abs(DSwingAngle) > AngleTolerance)
		{
			if (bUseSoftLimit)
			{
				const FReal SoftSwingStiffness = FPBDJointUtilities::GetSoftSwingStiffness(SolverSettings, JointSettings);
				const FReal SoftSwingDamping = FPBDJointUtilities::GetSoftSwingDamping(SolverSettings, JointSettings);
				const bool bAccelerationMode = FPBDJointUtilities::GetAngularSoftAccelerationMode(SolverSettings, JointSettings);
				ApplyRotationConstraintSoft(Dt, SoftSwingStiffness, SoftSwingDamping, bAccelerationMode, SwingAxis, DSwingAngle, 0.0f, SwingSoftLambda);
			}
			else
			{
				const FReal SwingStiffness = FPBDJointUtilities::GetSwingStiffness(SolverSettings, JointSettings);
				ApplyRotationConstraint(SwingStiffness, SwingAxis, DSwingAngle);
			}
		}
	}


	void FJointSolverGaussSeidel::ApplyDualConeSwingConstraint(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const EJointAngularConstraintIndex SwingConstraintIndex,
		const bool bUseSoftLimit)
	{
		FVec3 SwingAxis;
		FReal SwingAngle;
		FPBDJointUtilities::GetDualConeSwingAxisAngle(Rs[0], Rs[1], SwingConstraintIndex, SwingAxis, SwingAngle);

		// Calculate swing error we need to correct
		const FReal LimitPadding = GetAngularConstraintPadding(SwingConstraintIndex);
		FReal DSwingAngle = 0;
		const FReal SwingAngleMax = FMath::Max(JointSettings.AngularLimits[(int32)SwingConstraintIndex] - LimitPadding, 0.0f);
		if (SwingAngle > SwingAngleMax)
		{
			DSwingAngle = SwingAngle - SwingAngleMax;
		}
		else if (SwingAngle < -SwingAngleMax)
		{
			// Keep the error positive
			DSwingAngle = -SwingAngle - SwingAngleMax;
			SwingAxis = -SwingAxis;
		}

		UE_LOG(LogChaosJoint, VeryVerbose, TEXT("    DualConeSwing%d Angle %f [Limit %f]"), (SwingConstraintIndex == EJointAngularConstraintIndex::Swing1) ? 1 : 2, FMath::RadiansToDegrees(SwingAngle), FMath::RadiansToDegrees(SwingAngleMax));

		// Apply swing correction
		if (DSwingAngle > SolverSettings.AngleTolerance)
		{
			if (bUseSoftLimit)
			{
				const FReal SoftSwingStiffness = FPBDJointUtilities::GetSoftSwingStiffness(SolverSettings, JointSettings);
				const FReal SoftSwingDamping = FPBDJointUtilities::GetSoftSwingDamping(SolverSettings, JointSettings);
				const bool bAccelerationMode = FPBDJointUtilities::GetAngularSoftAccelerationMode(SolverSettings, JointSettings);
				ApplyRotationConstraintSoft(Dt, SoftSwingStiffness, SoftSwingDamping, bAccelerationMode, SwingAxis, DSwingAngle, 0.0f, SwingSoftLambda);
			}
			else
			{
				if (JointSettings.SwingRestitution > 0.0f)
				{
					CalculateAngularConstraintPadding(Dt, SolverSettings, JointSettings, JointSettings.SwingRestitution, SwingConstraintIndex, SwingAxis, DSwingAngle);
				}

				const FReal SwingStiffness = FPBDJointUtilities::GetSwingStiffness(SolverSettings, JointSettings);
				ApplyRotationConstraint(SwingStiffness, SwingAxis, DSwingAngle);
			}
		}
	}


	void FJointSolverGaussSeidel::ApplySwingConstraint(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const EJointAngularConstraintIndex SwingConstraintIndex,
		const bool bUseSoftLimit)
	{
		FVec3 SwingAxis;
		FReal SwingAngle;
		FPBDJointUtilities::GetSwingAxisAngle(Rs[0], Rs[1], SolverSettings.SwingTwistAngleTolerance, SwingConstraintIndex, SwingAxis, SwingAngle);

		// Calculate swing error we need to correct
		const FReal LimitPadding = GetAngularConstraintPadding(SwingConstraintIndex);
		FReal DSwingAngle = 0;
		const FReal SwingAngleMax = FMath::Max(JointSettings.AngularLimits[(int32)SwingConstraintIndex] - LimitPadding, 0.0f);
		if (SwingAngle > SwingAngleMax)
		{
			DSwingAngle = SwingAngle - SwingAngleMax;
		}
		else if (SwingAngle < -SwingAngleMax)
		{
			// Keep swing error positive
			DSwingAngle = -SwingAngle - SwingAngleMax;
			SwingAxis = -SwingAxis;
		}

		UE_LOG(LogChaosJoint, VeryVerbose, TEXT("    Swing%d Angle %f [Limit %f]"), (SwingConstraintIndex == EJointAngularConstraintIndex::Swing1) ? 1 : 2, FMath::RadiansToDegrees(SwingAngle), FMath::RadiansToDegrees(SwingAngleMax));

		// Apply swing correction
		if (DSwingAngle > AngleTolerance)
		{
			if (bUseSoftLimit)
			{
				const FReal SoftSwingStiffness = FPBDJointUtilities::GetSoftSwingStiffness(SolverSettings, JointSettings);
				const FReal SoftSwingDamping = FPBDJointUtilities::GetSoftSwingDamping(SolverSettings, JointSettings);
				const bool bAccelerationMode = FPBDJointUtilities::GetAngularSoftAccelerationMode(SolverSettings, JointSettings);
				ApplyRotationConstraintSoft(Dt, SoftSwingStiffness, SoftSwingDamping, bAccelerationMode, SwingAxis, DSwingAngle, 0.0f, SwingSoftLambda);
			}
			else
			{
				if (JointSettings.SwingRestitution > 0.0f)
				{
					CalculateAngularConstraintPadding(Dt, SolverSettings, JointSettings, JointSettings.SwingRestitution, SwingConstraintIndex, SwingAxis, DSwingAngle);
				}

				const FReal SwingStiffness = FPBDJointUtilities::GetSwingStiffness(SolverSettings, JointSettings);
				ApplyRotationConstraint(SwingStiffness, SwingAxis, DSwingAngle);
			}
		}
	}


	void FJointSolverGaussSeidel::ApplySwingTwistDrives(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const bool bTwistDriveEnabled,
		const bool bSwing1DriveEnabled,
		const bool bSwing2DriveEnabled)
	{
		FRotation3 R1Target = Rs[0] * JointSettings.AngularDrivePositionTarget;
		R1Target.EnforceShortestArcWith(Rs[1]);
		FRotation3 R1Error = R1Target.Inverse() * Rs[1];
		FVec3 R1TwistAxisError = R1Error * FJointConstants::TwistAxis();

		// Angle approximation Angle ~= Sin(Angle) for small angles, underestimates for large angles
		const FReal DTwistAngle = 2.0f * R1Error.X;
		const FReal DSwing1Angle = R1TwistAxisError.Y;
		const FReal DSwing2Angle = -R1TwistAxisError.Z;

		const FReal AngularTwistDriveStiffness = FPBDJointUtilities::GetAngularTwistDriveStiffness(SolverSettings, JointSettings);
		const FReal AngularTwistDriveDamping = FPBDJointUtilities::GetAngularTwistDriveDamping(SolverSettings, JointSettings);
		const FReal AngularSwingDriveStiffness = FPBDJointUtilities::GetAngularSwingDriveStiffness(SolverSettings, JointSettings);
		const FReal AngularSwingDriveDamping = FPBDJointUtilities::GetAngularSwingDriveDamping(SolverSettings, JointSettings);
		const bool bAccelerationMode = FPBDJointUtilities::GetDriveAccelerationMode(SolverSettings, JointSettings);

		const bool bUseTwistDrive = bTwistDriveEnabled && (((FMath::Abs(DTwistAngle) > AngleTolerance) && (AngularTwistDriveStiffness > 0.0f)) || (AngularTwistDriveDamping > 0.0f));
		if (bUseTwistDrive)
		{
			const FVec3 TwistAxis = Rs[1] * FJointConstants::TwistAxis();
			ApplyRotationConstraintSoft(Dt, AngularTwistDriveStiffness, AngularTwistDriveDamping, bAccelerationMode, TwistAxis, DTwistAngle, JointSettings.AngularDriveVelocityTarget[(int32)EJointAngularConstraintIndex::Twist], RotationDriveLambdas[(int32)EJointAngularConstraintIndex::Twist]);
		}

		const bool bUseSwing1Drive = bSwing1DriveEnabled && (((FMath::Abs(DSwing1Angle) > AngleTolerance) && (AngularSwingDriveStiffness > 0.0f)) || (AngularSwingDriveDamping > 0.0f));
		if (bUseSwing1Drive)
		{
			const FVec3 Swing1Axis = Rs[1] * FJointConstants::Swing1Axis();
			ApplyRotationConstraintSoft(Dt, AngularSwingDriveStiffness, AngularSwingDriveDamping, bAccelerationMode, Swing1Axis, DSwing1Angle, JointSettings.AngularDriveVelocityTarget[(int32)EJointAngularConstraintIndex::Swing1], RotationDriveLambdas[(int32)EJointAngularConstraintIndex::Swing1]);
		}

		const bool bUseSwing2Drive = bSwing2DriveEnabled && (((FMath::Abs(DSwing2Angle) > AngleTolerance) && (AngularSwingDriveStiffness > 0.0f)) || (AngularSwingDriveDamping > 0.0f));
		if (bUseSwing2Drive)
		{
			const FVec3 Swing2Axis = Rs[1] * FJointConstants::Swing2Axis();
			ApplyRotationConstraintSoft(Dt, AngularSwingDriveStiffness, AngularSwingDriveDamping, bAccelerationMode, Swing2Axis, DSwing2Angle, JointSettings.AngularDriveVelocityTarget[(int32)EJointAngularConstraintIndex::Swing2], RotationDriveLambdas[(int32)EJointAngularConstraintIndex::Swing2]);
		}
	}


	void FJointSolverGaussSeidel::ApplySLerpDrive(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		const FReal AngularDriveStiffness = FPBDJointUtilities::GetAngularSLerpDriveStiffness(SolverSettings, JointSettings);
		const FReal AngularDriveDamping = FPBDJointUtilities::GetAngularSLerpDriveDamping(SolverSettings, JointSettings);
		const bool bAccelerationMode = FPBDJointUtilities::GetDriveAccelerationMode(SolverSettings, JointSettings);

		// If damping is enabled, we need to apply the drive about all 3 axes, but without damping we can just drive along the axis of error
		if (AngularDriveDamping > 0.0f)
		{
			// NOTE: Slerp target velocity only works properly if we have a stiffness of zero.
			FVec3 Axes[3] = { FVec3(1, 0, 0), FVec3(0, 1, 0), FVec3(0, 0, 1) };
			if (AngularDriveStiffness > 0.0f)
			{
				FPBDJointUtilities::GetLockedRotationAxes(Rs[0], Rs[1], Axes[0], Axes[1], Axes[2]);
				Utilities::NormalizeSafe(Axes[0], KINDA_SMALL_NUMBER);
				Utilities::NormalizeSafe(Axes[1], KINDA_SMALL_NUMBER);
				Utilities::NormalizeSafe(Axes[2], KINDA_SMALL_NUMBER);
			}

			const FRotation3 R01 = Rs[0].Inverse() * Rs[1];
			FRotation3 TargetAngPos = JointSettings.AngularDrivePositionTarget;
			TargetAngPos.EnforceShortestArcWith(R01);
			const FRotation3 R1Error = TargetAngPos.Inverse() * R01;
			FReal AxisAngles[3] = 
			{ 
				2.0f * FMath::Asin(R1Error.X), 
				2.0f * FMath::Asin(R1Error.Y), 
				2.0f * FMath::Asin(R1Error.Z) 
			};

			const FVec3 TargetAngVel = Rs[0] * JointSettings.AngularDriveVelocityTarget;

			for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
			{
				const FReal AxisAngVel = FVec3::DotProduct(TargetAngVel, Axes[AxisIndex]);
				ApplyRotationConstraintSoft(Dt, AngularDriveStiffness, AngularDriveDamping, bAccelerationMode, Axes[AxisIndex], AxisAngles[AxisIndex], AxisAngVel, RotationDriveLambdas[AxisIndex]);
			}
		}
		else
		{
			const FRotation3 TargetR1 = Rs[0] * JointSettings.AngularDrivePositionTarget;
			const FRotation3 DR = TargetR1 * Rs[1].Inverse();

			FVec3 SLerpAxis;
			FReal SLerpAngle;
			if (DR.ToAxisAndAngleSafe(SLerpAxis, SLerpAngle, FVec3(1, 0, 0)))
			{
				if (SLerpAngle > (FReal)PI)
				{
					SLerpAngle = SLerpAngle - (FReal)2 * PI;
				}

				UE_LOG(LogChaosJoint, VeryVerbose, TEXT("      SLerpDrive Pos: %f Axis: %f %f %f"), -SLerpAngle, SLerpAxis.X, SLerpAxis.Y, SLerpAxis.Z);

				if (FMath::Abs(SLerpAngle) > AngleTolerance)
				{
					FReal AngVelTarget = (JointSettings.AngularDriveDamping > 0.0f) ? FVec3::DotProduct(SLerpAxis, Rs[0] * JointSettings.AngularDriveVelocityTarget) : 0.0f;
					ApplyRotationConstraintSoft(Dt, AngularDriveStiffness, AngularDriveDamping, bAccelerationMode, SLerpAxis, -SLerpAngle, AngVelTarget, RotationDriveLambdas[(int32)EJointAngularConstraintIndex::Swing1]);
				}
			}
		}
	}


	// Kinematic-Dynamic bodies
	void FJointSolverGaussSeidel::ApplyPointPositionConstraintKD(
		const int32 KIndex,
		const int32 DIndex,
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		check(InvMs[DIndex] > 0);

		FReal Stiffness = SolverStiffness * FPBDJointUtilities::GetLinearStiffness(SolverSettings, JointSettings);
		const FVec3 CX = Xs[DIndex] - Xs[KIndex];

		UE_LOG(LogChaosJoint, VeryVerbose, TEXT("    PointKD Delta %f [Limit %f]"), CX.Size(), PositionTolerance);

		if (CX.SizeSquared() > PositionTolerance * PositionTolerance)
		{
			if (bRealTypeCompatibleWithISPC && bChaos_Joint_ISPC_Enabled)
			{
#if INTEL_ISPC
				ispc::ApplyPointPositionConstraintKD((ispc::FJointSolverGaussSeidel*)this, KIndex, DIndex, (ispc::FVector&)CX, Stiffness);
#endif
			}
			else
			{
				// Calculate constraint correction
				FMatrix33 M = Utilities::ComputeJointFactorMatrix(Xs[DIndex] - Ps[DIndex], InvIs[DIndex], InvMs[DIndex]);
				FMatrix33 MI = M.Inverse();
				const FVec3 DX = Stiffness * Utilities::Multiply(MI, CX);

				// Apply constraint correction
				const FVec3 DP1 = -InvMs[DIndex] * DX;
				const FVec3 DR1 = Utilities::Multiply(InvIs[DIndex], FVec3::CrossProduct(Xs[DIndex] - Ps[DIndex], -DX));

				ApplyDelta(DIndex, DP1, DR1);

				NetLinearImpulse += (KIndex == 0) ? DX : -DX;
			}

			++NumActiveConstraints;
		}
	}


	// Dynamic-Dynamic bodies
	void FJointSolverGaussSeidel::ApplyPointPositionConstraintDD(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		check(InvMs[0] > 0);
		check(InvMs[1] > 0);

		FReal Stiffness = SolverStiffness * FPBDJointUtilities::GetLinearStiffness(SolverSettings, JointSettings);
		const FVec3 CX = Xs[1] - Xs[0];

		UE_LOG(LogChaosJoint, VeryVerbose, TEXT("    PointDD Delta %f [Limit %f]"), CX.Size(), PositionTolerance);

		if (CX.SizeSquared() > PositionTolerance * PositionTolerance)
		{
			if (bRealTypeCompatibleWithISPC && bChaos_Joint_ISPC_Enabled)
			{
#if INTEL_ISPC
				ispc::ApplyPointPositionConstraintDD((ispc::FJointSolverGaussSeidel*)this, (ispc::FVector&)CX, Stiffness);
#endif
			}
			else
			{
				// Calculate constraint correction
				FMatrix33 M0 = Utilities::ComputeJointFactorMatrix(Xs[0] - Ps[0], InvIs[0], InvMs[0]);
				FMatrix33 M1 = Utilities::ComputeJointFactorMatrix(Xs[1] - Ps[1], InvIs[1], InvMs[1]);
				FMatrix33 MI = (M0 + M1).Inverse();
				const FVec3 DX = Stiffness * Utilities::Multiply(MI, CX);

				// Apply constraint correction
				const FVec3 DP0 = InvMs[0] * DX;
				const FVec3 DP1 = -InvMs[1] * DX;
				const FVec3 DR0 = Utilities::Multiply(InvIs[0], FVec3::CrossProduct(Xs[0] - Ps[0], DX));
				const FVec3 DR1 = Utilities::Multiply(InvIs[1], FVec3::CrossProduct(Xs[1] - Ps[1], -DX));

				ApplyPositionDelta(DP0, DP1);
				ApplyRotationDelta(DR0, DR1);

				NetLinearImpulse += DX;
			}
			
			++NumActiveConstraints;
		}
	}


	void FJointSolverGaussSeidel::ApplySphericalPositionConstraint(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		FVec3 Axis;
		FReal Delta;
		FPBDJointUtilities::GetSphericalAxisDelta(Xs[0], Xs[1], Axis, Delta);

		const FReal LimitPadding = GetLinearConstraintPadding(0);
		const FReal Limit = FMath::Max(JointSettings.LinearLimit - LimitPadding, 0.0f);

		FReal Error = Delta - Limit;
		if (Error > PositionTolerance)
		{
			if (!FPBDJointUtilities::GetSoftLinearLimitEnabled(SolverSettings, JointSettings))
			{
				if (JointSettings.LinearRestitution > 0.0f)
				{
					CalculateLinearConstraintPadding(Dt, SolverSettings, JointSettings, JointSettings.LinearRestitution, 0, Axis, Error);
				}

				const FReal JointStiffness = FPBDJointUtilities::GetLinearStiffness(SolverSettings, JointSettings);
				ApplyPositionConstraint(JointStiffness, Axis, Error);
			}
			else
			{
				const FReal JointStiffness = FPBDJointUtilities::GetSoftLinearStiffness(SolverSettings, JointSettings);
				const FReal JointDamping = FPBDJointUtilities::GetSoftLinearDamping(SolverSettings, JointSettings);
				const bool bAccelerationMode = FPBDJointUtilities::GetLinearSoftAccelerationMode(SolverSettings, JointSettings);
				ApplyPositionConstraintSoft(Dt, JointStiffness, JointDamping, bAccelerationMode, Axis, Error, 0.0f, LinearSoftLambda);
			}
		}
	}


	void FJointSolverGaussSeidel::ApplyCylindricalPositionConstraint(
		const FReal Dt,
		const int32 AxisIndex,
		const EJointMotionType AxialMotion,
		const EJointMotionType RadialMotion,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		check(AxialMotion != RadialMotion);

		FVec3 Axis, RadialAxis;
		FReal AxialDelta, RadialDelta;
		FPBDJointUtilities::GetCylindricalAxesDeltas(Rs[0], Xs[0], Xs[1], AxisIndex, Axis, AxialDelta, RadialAxis, RadialDelta);

		if (AxialDelta < 0.0f)
		{
			AxialDelta = -AxialDelta;
			Axis = -Axis;
		}
		
		const FReal AxialLimitPadding = GetLinearConstraintPadding(0);
		const FReal AxialLimit = (AxialMotion == EJointMotionType::Locked) ? 0.0f : FMath::Max(JointSettings.LinearLimit - AxialLimitPadding, 0.0f);
		FReal AxialError = AxialDelta - AxialLimit;

		if (AxialError > PositionTolerance)
		{
			if ((AxialMotion == EJointMotionType::Limited) && FPBDJointUtilities::GetSoftLinearLimitEnabled(SolverSettings, JointSettings))
			{
				// Soft Axial constraint
				const FReal JointStiffness = FPBDJointUtilities::GetSoftLinearStiffness(SolverSettings, JointSettings);
				const FReal JointDamping = FPBDJointUtilities::GetSoftLinearDamping(SolverSettings, JointSettings);
				const bool bAccelerationMode = FPBDJointUtilities::GetLinearSoftAccelerationMode(SolverSettings, JointSettings);
				ApplyPositionConstraintSoft(Dt, JointStiffness, JointDamping, bAccelerationMode, Axis, AxialError, 0.0f, LinearSoftLambda);
			}
			else if (AxialMotion != EJointMotionType::Free)
			{
			// Hard Axial constraint
				if (JointSettings.LinearRestitution > 0.0f)
				{
					CalculateLinearConstraintPadding(Dt, SolverSettings, JointSettings, JointSettings.LinearRestitution, 0, Axis, AxialError);
				}

				const FReal JointStiffness = FPBDJointUtilities::GetLinearStiffness(SolverSettings, JointSettings);
				ApplyPositionConstraint(JointStiffness, Axis, AxialError);
			}
		}

		const FReal RadialLimitPadding = GetLinearConstraintPadding(1);
		const FReal RadialLimit = (RadialMotion == EJointMotionType::Locked) ? 0.0f : FMath::Max(JointSettings.LinearLimit - AxialLimitPadding, 0.0f);
		FReal RadialError = RadialDelta - RadialLimit;

		if (RadialError > PositionTolerance)
		{
			if ((RadialMotion == EJointMotionType::Limited) && FPBDJointUtilities::GetSoftLinearLimitEnabled(SolverSettings, JointSettings))
			{
				// Soft Radial constraint
				const FReal JointStiffness = FPBDJointUtilities::GetSoftLinearStiffness(SolverSettings, JointSettings);
				const FReal JointDamping = FPBDJointUtilities::GetSoftLinearDamping(SolverSettings, JointSettings);
				const bool bAccelerationMode = FPBDJointUtilities::GetLinearSoftAccelerationMode(SolverSettings, JointSettings);
				ApplyPositionConstraintSoft(Dt, JointStiffness, JointDamping, bAccelerationMode, RadialAxis, RadialError, 0.0f, LinearSoftLambda);
			}
			else if (RadialMotion != EJointMotionType::Free)
			{
				// Hard Radial constraint
				if (JointSettings.LinearRestitution > 0.0f)
				{
					CalculateLinearConstraintPadding(Dt, SolverSettings, JointSettings, JointSettings.LinearRestitution, 1, RadialAxis, RadialError);
				}

				const FReal JointStiffness = FPBDJointUtilities::GetLinearStiffness(SolverSettings, JointSettings);
				ApplyPositionConstraint(JointStiffness, RadialAxis, RadialError);
			}
		}
	}


	void FJointSolverGaussSeidel::ApplyPlanarPositionConstraint(
		const FReal Dt,
		const int32 AxisIndex,
		const EJointMotionType AxialMotion,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		FVec3 Axis;
		FReal Delta;
		FPBDJointUtilities::GetPlanarAxisDelta(Rs[0], Xs[0], Xs[1], AxisIndex, Axis, Delta);

		if (Delta < 0.0f)
		{
			Delta = -Delta;
			Axis = -Axis;
		}

		const FReal LimitPadding = GetLinearConstraintPadding(0);
		const FReal Limit = (AxialMotion == EJointMotionType::Locked) ? 0 : FMath::Max(JointSettings.LinearLimit - LimitPadding, 0.0f);
		FReal Error = Delta - Limit;
		if (Error > PositionTolerance)
		{
			if ((AxialMotion == EJointMotionType::Limited) && FPBDJointUtilities::GetSoftLinearLimitEnabled(SolverSettings, JointSettings))
			{
				const FReal JointStiffness = FPBDJointUtilities::GetSoftLinearStiffness(SolverSettings, JointSettings);
				const FReal JointDamping = FPBDJointUtilities::GetSoftLinearDamping(SolverSettings, JointSettings);
				const bool bAccelerationMode = FPBDJointUtilities::GetLinearSoftAccelerationMode(SolverSettings, JointSettings);
				ApplyPositionConstraintSoft(Dt, JointStiffness, JointDamping, bAccelerationMode, Axis, Error, 0.0f, LinearSoftLambda);
			}
			else
			{
				if (JointSettings.LinearRestitution > 0.0f)
				{
					CalculateLinearConstraintPadding(Dt, SolverSettings, JointSettings, JointSettings.LinearRestitution, 0, Axis, Error);
				}

				const FReal JointStiffness = FPBDJointUtilities::GetLinearStiffness(SolverSettings, JointSettings);
				ApplyPositionConstraint(JointStiffness, Axis, Error);
			}
		}
	}


	void FJointSolverGaussSeidel::ApplyPositionDrive(
		const FReal Dt,
		const int32 AxisIndex,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FVec3& Axis,
		const FReal DeltaPos,
		const FReal DeltaVel)
	{
		const FReal JointStiffness = FPBDJointUtilities::GetLinearDriveStiffness(SolverSettings, JointSettings, AxisIndex);
		const FReal JointDamping = FPBDJointUtilities::GetLinearDriveDamping(SolverSettings, JointSettings, AxisIndex);
		const bool bAccelerationMode = FPBDJointUtilities::GetDriveAccelerationMode(SolverSettings, JointSettings);

		if ((FMath::Abs(DeltaPos) > PositionTolerance) || (JointDamping > 0.0f))
		{
			ApplyPositionConstraintSoft(Dt, JointStiffness, JointDamping, bAccelerationMode, Axis, DeltaPos, DeltaVel, LinearDriveLambdas[AxisIndex]);
		}
	}


	void FJointSolverGaussSeidel::ApplyPointProjection(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FReal Alpha,
		FVec3& NetDP1,
		FVec3& NetDR1)
	{
		const FReal ProjectionPositionTolerance = 0.0f;//PositionTolerance;

		const FVec3 CX = Xs[1] - Xs[0];
		if (CX.Size() > ProjectionPositionTolerance)
		{
			FMatrix33 J = Utilities::ComputeJointFactorMatrix(Xs[1] - Ps[1], InvIs[1], InvMs[1]);
			const FMatrix33 IJ = J.Inverse();
			const FVec3 DX = Utilities::Multiply(IJ, CX);

			const FVec3 DP1 = -Alpha * InvMs[1] * DX;
			const FVec3 DR1 = -Alpha * Utilities::Multiply(InvIs[1], FVec3::CrossProduct(Xs[1] - Ps[1], DX));
			ApplyDelta(1, DP1, DR1);

			NetDP1 += DP1;
			NetDR1 += DR1;
		}
	}

	void FJointSolverGaussSeidel::ApplySphereProjection(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FReal Alpha,
		FVec3& NetDP1,
		FVec3& NetDR1)
	{
		const FReal ProjectionPositionTolerance = 0.0f;//PositionTolerance;

		FVec3 Axis;
		FReal Delta;
		FPBDJointUtilities::GetSphericalAxisDelta(Xs[0], Xs[1], Axis, Delta);
		const FReal Error = FMath::Max((FReal)0, Delta - JointSettings.LinearLimit);
		if (FMath::Abs(Error) > ProjectionPositionTolerance)
		{
			const FVec3 AngularAxis1 = FVec3::CrossProduct(Xs[1] - Ps[1], Axis);
			const FVec3 IA1 = Utilities::Multiply(InvIs[1], AngularAxis1);
			const FReal II1 = FVec3::DotProduct(AngularAxis1, IA1);
			const FReal IM = InvMs[1] + II1;
			const FVec3 DX = Axis * Error / IM;

			const FVec3 DP1 = -Alpha * InvMs[1] * DX;
			const FVec3 DR1 = -Alpha * Utilities::Multiply(InvIs[1], FVec3::CrossProduct(Xs[1] - Ps[1], DX));
			ApplyDelta(1, DP1, DR1);

			NetDP1 += DP1;
			NetDR1 += DR1;
		}
	}

	void FJointSolverGaussSeidel::ApplyTranslateProjection(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FReal Alpha,
		FVec3& NetDP1,
		FVec3& NetDR1)
	{
		const FReal ProjectionPositionTolerance = 0.0f;//PositionTolerance;

		FVec3 Axis;
		FReal Delta;
		FPBDJointUtilities::GetSphericalAxisDelta(Xs[0], Xs[1], Axis, Delta);
		const FReal Error = FMath::Max((FReal)0, Delta - JointSettings.LinearLimit);
		if (Error > ProjectionPositionTolerance)
		{
			const FVec3 DP1 = -Alpha * Error * Axis;
			ApplyPositionDelta(1, DP1);
			
			NetDP1 += DP1;
		}
	}

	void FJointSolverGaussSeidel::ApplyConeProjection(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FReal Alpha,
		const bool bPositionLocked,
		FVec3& NetDP1,
		FVec3& NetDR1)
	{
		const FReal ProjectionAngleTolerance = 0.0f;//AngleTolerance;

		FVec3 SwingAxisLocal;
		FReal DSwingAngle = 0.0f;
		const FReal Swing1Limit = JointSettings.AngularLimits[(int32)EJointAngularConstraintIndex::Swing1];
		const FReal Swing2Limit = JointSettings.AngularLimits[(int32)EJointAngularConstraintIndex::Swing2];
		FPBDJointUtilities::GetEllipticalConeAxisErrorLocal(Rs[0], Rs[1], Swing2Limit, Swing1Limit, SwingAxisLocal, DSwingAngle);
		FVec3 SwingAxis = Rs[0] * SwingAxisLocal;
		if (DSwingAngle > ProjectionAngleTolerance)
		{
			const FVec3 DR1 = -Alpha * DSwingAngle * SwingAxis;
			FVec3 DP1 = FVec3(0);
			if (bPositionLocked)
			{
				DP1 = -Alpha * FVec3::CrossProduct(DR1, Xs[1] - Ps[1]);
			}
			ApplyDelta(1, DP1, DR1);

			NetDP1 += DP1;
			NetDR1 += DR1;
		}
	}

	void FJointSolverGaussSeidel::ApplySwingProjection(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const EJointAngularConstraintIndex SwingConstraintIndex,
		const FReal Alpha,
		const bool bPositionLocked,
		FVec3& NetDP1,
		FVec3& NetDR1)
	{
		const FReal ProjectionAngleTolerance = 0.0f;//AngleTolerance;

		FVec3 SwingAxis;
		FReal SwingAngle;
		FPBDJointUtilities::GetSwingAxisAngle(Rs[0], Rs[1], SolverSettings.SwingTwistAngleTolerance, SwingConstraintIndex, SwingAxis, SwingAngle);

		// Calculate swing error we need to correct
		FReal DSwingAngle = 0;
		const FReal SwingAngleMax = JointSettings.AngularLimits[(int32)SwingConstraintIndex];
		if (SwingAngle > SwingAngleMax)
		{
			DSwingAngle = SwingAngle - SwingAngleMax;
		}
		else if (SwingAngle < -SwingAngleMax)
		{
			DSwingAngle = SwingAngle + SwingAngleMax;
		}

		UE_LOG(LogChaosJoint, VeryVerbose, TEXT("    Swing%d Angle %f [Limit %f]"), (SwingConstraintIndex == EJointAngularConstraintIndex::Swing1) ? 1 : 2, FMath::RadiansToDegrees(SwingAngle), FMath::RadiansToDegrees(SwingAngleMax));

		// Apply swing correction
		if (FMath::Abs(DSwingAngle) > ProjectionAngleTolerance)
		{
			const FVec3 DR1 = -Alpha * DSwingAngle * SwingAxis;
			FVec3 DP1 = FVec3(0);
			if (bPositionLocked)
			{
				DP1 = -Alpha * FVec3::CrossProduct(DR1, Xs[1] - Ps[1]);
			}
			ApplyDelta(1, DP1, DR1);

			NetDP1 += DP1;
			NetDR1 += DR1;
		}
	}

	void FJointSolverGaussSeidel::ApplySingleLockedSwingProjection(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const EJointAngularConstraintIndex SwingConstraintIndex,
		const FReal Alpha,
		const bool bPositionLocked,
		FVec3& NetDP1,
		FVec3& NetDR1)
	{
		ApplySwingProjection(Dt, SolverSettings, JointSettings, SwingConstraintIndex, Alpha, bPositionLocked, NetDP1, NetDR1);
	}

	void FJointSolverGaussSeidel::ApplyDoubleLockedSwingProjection(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FReal Alpha,
		const bool bPositionLocked,
		FVec3& NetDP1,
		FVec3& NetDR1)
	{
		const FReal ProjectionAngleTolerance = 0.0f;//AngleTolerance;

		FVec3 SwingAxisLocal;
		FReal DSwingAngle = 0.0f;
		FPBDJointUtilities::GetCircularConeAxisErrorLocal(Rs[0], Rs[1], 0.0f, SwingAxisLocal, DSwingAngle);
		FVec3 SwingAxis = Rs[0] * SwingAxisLocal;
		if (DSwingAngle > ProjectionAngleTolerance)
		{
			const FVec3 DR1 = -Alpha * DSwingAngle * SwingAxis;
			FVec3 DP1 = FVec3(0);
			if (bPositionLocked)
			{
				DP1 = -Alpha * FVec3::CrossProduct(DR1, Xs[1] - Ps[1]);
			}
			ApplyDelta(1, DP1, DR1);

			NetDP1 += DP1;
			NetDR1 += DR1;
		}
	}

	void FJointSolverGaussSeidel::ApplyDualConeSwingProjection(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const EJointAngularConstraintIndex SwingConstraintIndex,
		const FReal Alpha,
		const bool bPositionLocked,
		FVec3& NetDP1,
		FVec3& NetDR1)
	{
		const FReal ProjectionAngleTolerance = 0.0f;//AngleTolerance;

		FVec3 SwingAxis;
		FReal SwingAngle;
		FPBDJointUtilities::GetDualConeSwingAxisAngle(Rs[0], Rs[1], SwingConstraintIndex, SwingAxis, SwingAngle);

		// Calculate swing error we need to correct
		FReal DSwingAngle = 0;
		const FReal SwingAngleMax = JointSettings.AngularLimits[(int32)SwingConstraintIndex];
		if (SwingAngle > SwingAngleMax)
		{
			DSwingAngle = SwingAngle - SwingAngleMax;
		}
		else if (SwingAngle < -SwingAngleMax)
		{
			DSwingAngle = SwingAngle + SwingAngleMax;
		}

		// Apply swing correction
		if (FMath::Abs(DSwingAngle) > ProjectionAngleTolerance)
		{
			const FVec3 DR1 = -Alpha * DSwingAngle * SwingAxis;
			FVec3 DP1 = FVec3(0);
			if (bPositionLocked)
			{
				DP1 = -Alpha * FVec3::CrossProduct(DR1, Xs[1] - Ps[1]);
			}
			ApplyDelta(1, DP1, DR1);

			NetDP1 += DP1;
			NetDR1 += DR1;
		}
	}

	void FJointSolverGaussSeidel::ApplyTwistProjection(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FReal Alpha,
		const bool bPositionLocked,
		FVec3& NetDP1,
		FVec3& NetDR1)
	{
		const FReal ProjectionAngleTolerance = 0.0f;//AngleTolerance;

		FVec3 TwistAxis;
		FReal TwistAngle;
		FPBDJointUtilities::GetTwistAxisAngle(Rs[0], Rs[1], TwistAxis, TwistAngle);
		FReal DTwistAngle = 0;
		const FReal TwistLimit = JointSettings.AngularLimits[(int32)EJointAngularConstraintIndex::Twist];
		if (TwistAngle > TwistLimit)
		{
			DTwistAngle = TwistAngle - TwistLimit;
		}
		else if (TwistAngle < -TwistLimit)
		{
			DTwistAngle = TwistAngle + TwistLimit;
		}

		if (FMath::Abs(DTwistAngle) > ProjectionAngleTolerance)
		{
			const FVec3 DR1 = -Alpha * DTwistAngle * TwistAxis;
			FVec3 DP1 = FVec3(0);
			if (bPositionLocked)
			{
				DP1 = -Alpha * FVec3::CrossProduct(DR1, Xs[1] - Ps[1]);
			}
			ApplyDelta(1, DP1, DR1);

			NetDP1 += DP1;
			NetDR1 += DR1;
		}
	}

	void FJointSolverGaussSeidel::ApplyVelocityProjection(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FReal Alpha,
		const FVec3& DP1,
		const FVec3& DR1)
	{
		const FVec3 DV1 = Alpha * DP1 / Dt;
		const FVec3 DW1 = Alpha * DR1 / Dt;
		ApplyVelocityDelta(1, DV1, DW1);
	}

	//
	//
	// WIP matrix based solver for this joint.
	// 	   [ NOT CURRENTLY USED]
	//
	//

	int32 FJointSolverGaussSeidel::AddLinear(
		const FReal Dt,
		const FVec3& Axis,
		const FVec3& ConnectorOffset0,
		const FVec3& ConnectorOffset1,
		const FReal Error,
		const FReal VelTarget,
		const FReal Stiffness,
		const FReal Damping,
		const bool bSoft,
		const bool bAccelerationMode,
		FDenseMatrix66& J0,
		FDenseMatrix66& J1,
		FDenseMatrix61& C,
		FDenseMatrix61& V,
		FDenseMatrix61& S,
		FDenseMatrix61& D)
	{
		if (bSoft && (Stiffness < SMALL_NUMBER) && (Damping < SMALL_NUMBER))
		{
			return INDEX_NONE;
		}

		const int32 RowIndex = C.NumRows();
		J0.AddRows(1);
		J1.AddRows(1);
		C.AddRows(1);
		V.AddRows(1);
		S.AddRows(1);
		D.AddRows(1);

		if (InvMs[0] > SMALL_NUMBER)
		{
			J0.SetRowAt(RowIndex, 0, Axis);
			J0.SetRowAt(RowIndex, 3, FVec3::CrossProduct(ConnectorOffset0, Axis));
		}
		if (InvMs[1] > SMALL_NUMBER)
		{
			J1.SetRowAt(RowIndex, 0, Axis);
			J1.SetRowAt(RowIndex, 3, FVec3::CrossProduct(ConnectorOffset1, Axis));
		}

		C.SetAt(RowIndex + 0, 0, Error);
		S.SetAt(RowIndex + 0, 0, 0.0f);
		D.SetAt(RowIndex + 0, 0, 0.0f);
		V.SetAt(RowIndex + 0, 0, 0.0f);

		if (bSoft)
		{
			FReal VelDt = 0;
			if (Damping > KINDA_SMALL_NUMBER)
			{
				const FVec3 V0Dt = FVec3::CalculateVelocity(InitXs[0], Xs[0], 1.0f);
				const FVec3 V1Dt = FVec3::CalculateVelocity(InitXs[1], Xs[1], 1.0f);
				VelDt = VelTarget * Dt + FVec3::DotProduct(V0Dt - V1Dt, Axis);
			}
			const FReal MassScale = bAccelerationMode ? 1.0f / (InvMs[0] + InvMs[1]) : 0.0f;
			V.SetAt(RowIndex, 0, VelDt);
			S.SetAt(RowIndex, 0, MassScale * Stiffness * Dt * Dt);
			D.SetAt(RowIndex, 0, MassScale * Damping * Dt);
		}

		return RowIndex;
	}


	int32 FJointSolverGaussSeidel::AddAngular(
		const FReal Dt,
		const FVec3& Axis,
		const FReal Error,
		const FReal VelTarget,
		const FReal Stiffness,
		const FReal Damping,
		const bool bSoft,
		const bool bAccelerationMode,
		FDenseMatrix66& J0,
		FDenseMatrix66& J1,
		FDenseMatrix61& C,
		FDenseMatrix61& V,
		FDenseMatrix61& S,
		FDenseMatrix61& D)
	{
		if (bSoft && (Stiffness < SMALL_NUMBER) && (Damping < SMALL_NUMBER))
		{
			return INDEX_NONE;
		}

		const int32 RowIndex = C.NumRows();
		J0.AddRows(1);
		J1.AddRows(1);
		C.AddRows(1);
		V.AddRows(1);
		S.AddRows(1);
		D.AddRows(1);

		if (InvMs[0] > SMALL_NUMBER)
		{
			J0.SetRowAt(RowIndex, 0, FVec3(0));
			J0.SetRowAt(RowIndex, 3, Axis);
		}
		if (InvMs[1] > SMALL_NUMBER)
		{
			J1.SetRowAt(RowIndex, 0, FVec3(0));
			J1.SetRowAt(RowIndex, 3, Axis);
		}

		C.SetAt(RowIndex, 0, Error);
		V.SetAt(RowIndex, 0, 0.0f);
		S.SetAt(RowIndex, 0, 0.0f);
		D.SetAt(RowIndex, 0, 0.0f);
		if (bSoft)
		{
			FReal VelDt = 0;
			if (Damping > KINDA_SMALL_NUMBER)
			{
				const FReal AngVelTarget = 0.0f;
				const FVec3 W0Dt = FRotation3::CalculateAngularVelocity(InitRs[0], Rs[0], 1.0f);
				const FVec3 W1Dt = FRotation3::CalculateAngularVelocity(InitRs[1], Rs[1], 1.0f);
				VelDt = AngVelTarget * Dt + FVec3::DotProduct(Axis, W0Dt - W1Dt);
			}
			FReal MassScale = 1.0f;
			if (bAccelerationMode)
			{
				// TODO: We should be able to precalculate something that would do for the mass scale
				FReal II0 = 0.0f;
				FReal II1 = 0.0f;
				if (InvMs[0] > SMALL_NUMBER)
				{
					II0 = FVec3::DotProduct(Axis, Utilities::Multiply(InvIs[0], Axis));
				}
				if (InvMs[1] > SMALL_NUMBER)
				{
					II1 = FVec3::DotProduct(Axis, Utilities::Multiply(InvIs[1], Axis));
				}
				MassScale = 1.0f / (II0 + II1);
			}
			S.SetAt(RowIndex, 0, MassScale * Stiffness * Dt * Dt);
			D.SetAt(RowIndex, 0, MassScale * Damping * Dt);
			V.SetAt(RowIndex, 0, VelDt);
		}

		return RowIndex;
	}

	void FJointSolverGaussSeidel::ApplyConstraintsMatrix(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		// @todo(chaos): this path isn't currently used, but it needs to use SolverStiffness if we ever use it

		const FVec3 PX0 = Xs[0] - Ps[0];	// Constraint connector offset for particle 0
		const FVec3 PX1 = Xs[1] - Ps[1];	// Constraint connector offset for particle 1

		FDenseMatrix66 J0(0, 6);			// Jacobian of particle 0
		FDenseMatrix66 J1(0, 6);			// Jacobian of particle 1
		FDenseMatrix61 C(0, 1);				// Constraint-space errors
		FDenseMatrix61 V(0, 1);				// Constraint-space velocities for XPBD
		FDenseMatrix61 S(0, 1);				// Timestep-scaled stiffness for XPBD (stiffness*dt*dt)
		FDenseMatrix61 D(0, 1);				// Timestep-scaled damping for XPBD (damping*dt)
		FReal* LambdaPtr[6] = { 0, };		// Assumulated constraint-space correction for XPBD

		const bool bLinearLocked[3] =
		{
			(JointSettings.LinearMotionTypes[0] == EJointMotionType::Locked),
			(JointSettings.LinearMotionTypes[1] == EJointMotionType::Locked),
			(JointSettings.LinearMotionTypes[2] == EJointMotionType::Locked),
		};
		const bool bLinearLimted[3] =
		{
			(JointSettings.LinearMotionTypes[0] == EJointMotionType::Limited),
			(JointSettings.LinearMotionTypes[1] == EJointMotionType::Limited),
			(JointSettings.LinearMotionTypes[2] == EJointMotionType::Limited),
		};
		const bool bLinearSoft = FPBDJointUtilities::GetSoftLinearLimitEnabled(SolverSettings, JointSettings);
		const bool bLinearAccMode = FPBDJointUtilities::GetLinearSoftAccelerationMode(SolverSettings, JointSettings);

		if (bLinearLocked[0] && bLinearLocked[1] && bLinearLocked[2])
		{
			//
			// Position locked constraints. 
			// Add a constraint for each axis
			//
			const FVec3 Error = Xs[1] - Xs[0];
			AddLinear(Dt, FVec3(1, 0, 0), PX0, PX1, Error.X, 0.0f, 0.0f, 0.0f, false, false, J0, J1, C, V, S, D);
			AddLinear(Dt, FVec3(0, 1, 0), PX0, PX1, Error.Y, 0.0f, 0.0f, 0.0f, false, false, J0, J1, C, V, S, D);
			AddLinear(Dt, FVec3(0, 0, 1), PX0, PX1, Error.Z, 0.0f, 0.0f, 0.0f, false, false, J0, J1, C, V, S, D);
		}
		else if (bLinearLimted[0] && bLinearLimted[1] && bLinearLimted[2])
		{
			//
			// Spherical limited position constraint. 
			// Add a constraint to keep the connectors within specified distance of each other
			//
			const FReal LimitPadding = GetLinearConstraintPadding(0);
			const FReal LimitContactDistance = JointSettings.LinearContactDistance;
			const FReal Limit = FMath::Max(JointSettings.LinearLimit - LimitPadding - LimitContactDistance, 0.0f);

			FVec3 Axis;
			FReal Delta;
			FPBDJointUtilities::GetSphericalAxisDelta(Xs[0], Xs[1], Axis, Delta);

			FReal Error = Delta - Limit;
			if (Error > PositionTolerance)
			{
				if (JointSettings.LinearRestitution > 0.0f)
				{
					CalculateLinearConstraintPadding(Dt, SolverSettings, JointSettings, JointSettings.LinearRestitution, 0, Axis, Error);
				}

				Error = FMath::Max(Error - LimitContactDistance, 0.0f);

				const FReal Stiffness = FPBDJointUtilities::GetSoftLinearStiffness(SolverSettings, JointSettings);
				const FReal Damping = FPBDJointUtilities::GetSoftLinearDamping(SolverSettings, JointSettings);

				int32 RowIndex = AddLinear(Dt, Axis, PX0, PX1, Error, 0.0f, Stiffness, Damping, bLinearSoft, bLinearAccMode, J0, J1, C, V, S, D);
				if (bLinearSoft && (RowIndex != INDEX_NONE))
				{
					LambdaPtr[RowIndex] = &LinearSoftLambda;
				}
			}
		}

		const EJointMotionType TwistMotion = JointSettings.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Twist];
		const EJointMotionType Swing1Motion = JointSettings.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing1];
		const EJointMotionType Swing2Motion = JointSettings.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing2];
		const bool bTwistSoft = FPBDJointUtilities::GetSoftTwistLimitEnabled(SolverSettings, JointSettings);
		const bool bSwingSoft = FPBDJointUtilities::GetSoftSwingLimitEnabled(SolverSettings, JointSettings);
		const bool bAngularAccMode = FPBDJointUtilities::GetAngularSoftAccelerationMode(SolverSettings, JointSettings);

		if (TwistMotion == EJointMotionType::Limited)
		{
			//
			// Twist limit constraint
			//
			const FReal LimitPadding = GetAngularConstraintPadding(EJointAngularConstraintIndex::Twist);
			const FReal LimitContactDistance = JointSettings.TwistContactDistance;
			FReal Limit = FMath::Max(JointSettings.AngularLimits[(int32)EJointAngularConstraintIndex::Twist] - LimitPadding - LimitContactDistance, 0.0f);

			FVec3 TwistAxis;
			FReal TwistAngle;
			FPBDJointUtilities::GetTwistAxisAngle(Rs[0], Rs[1], TwistAxis, TwistAngle);
			FReal DTwistAngle = 0;
			if (TwistAngle > Limit)
			{
				DTwistAngle = TwistAngle - Limit;
			}
			else if (TwistAngle < -Limit)
			{
				// Keep Twist error positive
				DTwistAngle = -TwistAngle - Limit;
				TwistAxis = -TwistAxis;
			}

			if (DTwistAngle > AngleTolerance)
			{
				DTwistAngle = FMath::Max(DTwistAngle - LimitContactDistance, 0.0f);

				if ((DTwistAngle > 0.0f) && (JointSettings.TwistRestitution > 0.0f))
				{
					CalculateAngularConstraintPadding(Dt, SolverSettings, JointSettings, JointSettings.TwistRestitution, EJointAngularConstraintIndex::Twist, TwistAxis, DTwistAngle);
				}

				const FReal Stiffness = FPBDJointUtilities::GetSoftTwistStiffness(SolverSettings, JointSettings);
				const FReal Damping = FPBDJointUtilities::GetSoftTwistDamping(SolverSettings, JointSettings);

				int32 RowIndex = AddAngular(Dt, TwistAxis, DTwistAngle, 0.0f, Stiffness, Damping, bTwistSoft, bAngularAccMode, J0, J1, C, V, S, D);
				if (bTwistSoft && (RowIndex != INDEX_NONE))
				{
					LambdaPtr[RowIndex] = &TwistSoftLambda;
				}
			}
		}

		if ((Swing1Motion == EJointMotionType::Limited) && (Swing2Motion == EJointMotionType::Limited))
		{
			//
			// Cone limit constraint
			//
			const FReal LimitPadding = GetAngularConstraintPadding(EJointAngularConstraintIndex::Swing1);
			const FReal LimitContactDistance = JointSettings.SwingContactDistance;
			const FReal Limit1 = FMath::Max(JointSettings.AngularLimits[(int32)EJointAngularConstraintIndex::Swing1] - LimitPadding - LimitContactDistance, 0.0f);
			const FReal Limit2 = FMath::Max(JointSettings.AngularLimits[(int32)EJointAngularConstraintIndex::Swing2] - LimitPadding - LimitContactDistance, 0.0f);

			FVec3 SwingAxisLocal;
			FReal DSwingAngle = 0.0f;
			FPBDJointUtilities::GetEllipticalConeAxisErrorLocal(Rs[0], Rs[1], Limit2, Limit1, SwingAxisLocal, DSwingAngle);

			if (DSwingAngle > AngleTolerance)
			{
				DSwingAngle = FMath::Max(DSwingAngle - LimitContactDistance, 0.0f);
				const FVec3 SwingAxis = Rs[0] * SwingAxisLocal;

				if ((DSwingAngle > 0.0f) && (JointSettings.SwingRestitution > 0.0f))
				{
					CalculateAngularConstraintPadding(Dt, SolverSettings, JointSettings, JointSettings.SwingRestitution, EJointAngularConstraintIndex::Swing1, SwingAxis, DSwingAngle);
				}

				const FReal Stiffness = FPBDJointUtilities::GetSoftSwingStiffness(SolverSettings, JointSettings);
				const FReal Damping = FPBDJointUtilities::GetSoftSwingDamping(SolverSettings, JointSettings);

				int32 RowIndex = AddAngular(Dt, SwingAxis, DSwingAngle, 0.0f, Stiffness, Damping, bSwingSoft, bAngularAccMode, J0, J1, C, V, S, D);
				if (bSwingSoft && (RowIndex != INDEX_NONE))
				{
					LambdaPtr[RowIndex] = &SwingSoftLambda;
				}
			}
		}


		const int32 NumRows = C.NumRows();
		if (NumRows == 0)
		{
			return;
		}

		// InvM(6x6) = inverse mass matrix
		// IJt(6xN) = I(6x6).Jt(6xN)
		// Joint-space mass: F(NxN) = J(Nx6).I(6x6).Jt(6xN) = J(Nx6).IJt(6xN)
		// NOTE: Result is symmetric
		FDenseMatrix66 F = FDenseMatrix66::Make(NumRows, NumRows, 0.0f);
		FDenseMatrix66 IJt0, IJt1;
		if (InvMs[0] > SMALL_NUMBER)
		{
			const FMassMatrix InvM0 = FMassMatrix::Make(InvMs[0], InvIs[0]);
			IJt0 = FDenseMatrix66::MultiplyABt(InvM0, J0);
			F = FDenseMatrix66::MultiplyBCAddA_Symmetric(F, J0, IJt0);
		}
		if (InvMs[1] > SMALL_NUMBER)
		{
			const FMassMatrix InvM1 = FMassMatrix::Make(InvMs[1], InvIs[1]);
			IJt1 = FDenseMatrix66::MultiplyABt(InvM1, J1);
			F = FDenseMatrix66::MultiplyBCAddA_Symmetric(F, J1, IJt1);
		}

		// Apply stiffness and damping multipliers for XPBD
		for (int32 RowIndex0 = 0; RowIndex0 < NumRows; ++RowIndex0)
		{
			if (LambdaPtr[RowIndex0] != nullptr)
			{
				const FReal S0 = S.At(RowIndex0, 0);
				const FReal D0 = D.At(RowIndex0, 0);
				const FReal C0 = C.At(RowIndex0, 0);
				const FReal V0 = V.At(RowIndex0, 0);
				const FReal Lambda = *(LambdaPtr[RowIndex0]);
				if (S0 + D0 > KINDA_SMALL_NUMBER)
				{
					const FReal SDInv = 1.0f / (S0 + D0);

					const FReal FDiag = F.At(RowIndex0, RowIndex0) + SDInv;
					F.SetAt(RowIndex0, RowIndex0, FDiag);

					const FReal R = SDInv * (S0 * C0 - D0 * V0 - Lambda);
					C.SetAt(RowIndex0, 0, R);
				}
			}
		}

		// Joint-space correction: F(NxN).DL(Nx1) = C(Nx1)
		// DL = [1/F].C
		FDenseMatrix61 DL;
		if (FDenseMatrixSolver::SolvePositiveDefinite(F, C, DL))
		{
			// Accumulator for XPBD
			for (int32 RowIndex = 0; RowIndex < NumRows; ++RowIndex)
			{
				if (LambdaPtr[RowIndex] != nullptr)
				{
					*(LambdaPtr[RowIndex]) += DL.At(RowIndex, 0);
				}
			}

			// Calculate and apply world-space correction: 
			// D(6x1) = I(6x6).Jt(6xN).L(Nx1) = IJt(6xN).L(Nx1)
			// TODO: stiffness
			if (InvMs[0] > SMALL_NUMBER)
			{
				const FDenseMatrix61 D0 = FDenseMatrix61::MultiplyAB(IJt0, DL);
				const FVec3 DP0 = FVec3(D0.At(0, 0), D0.At(1, 0), D0.At(2, 0));
				const FVec3 DR0 = FVec3(D0.At(3, 0), D0.At(4, 0), D0.At(5, 0));
				ApplyDelta(0, DP0, DR0);
			}

			if (InvMs[1] > SMALL_NUMBER)
			{
				const FDenseMatrix61 D1 = FDenseMatrix61::MultiplyAB(IJt1, DL);
				const FVec3 DP1 = FVec3(-D1.At(0, 0), -D1.At(1, 0), -D1.At(2, 0));
				const FVec3 DR1 = FVec3(-D1.At(3, 0), -D1.At(4, 0), -D1.At(5, 0));
				ApplyDelta(1, DP1, DR1);
			}
		}
	}

}
