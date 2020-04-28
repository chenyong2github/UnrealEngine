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
		// Really we should need to do this for Kinematics since Dynamics are updated each iteration
		Xs[0] = PrevPs[0] + PrevQs[0] * XLs[0].GetTranslation();
		Rs[0] = PrevQs[0] * XLs[0].GetRotation();
		InvIs[0] = (InvMs[0] > 0.0f) ? Utilities::ComputeWorldSpaceInertia(PrevQs[0], InvILs[0]) : FMatrix33(0, 0, 0);

		Xs[1] = PrevPs[1] + PrevQs[1] * XLs[1].GetTranslation();
		Rs[1] = PrevQs[1] * XLs[1].GetRotation();
		InvIs[1] = (InvMs[1] > 0.0f) ? Utilities::ComputeWorldSpaceInertia(PrevQs[1], InvILs[1]) : FMatrix33(0, 0, 0);

		Rs[1].EnforceShortestArcWith(Rs[0]);
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

		PrevPs[0] = PrevP0;
		PrevPs[1] = PrevP1;
		PrevQs[0] = PrevQ0;
		PrevQs[1] = PrevQ1;
		PrevXs[0] = PrevP0 + PrevQ0 * XL0.GetTranslation();
		PrevXs[1] = PrevP1 + PrevQ1 * XL1.GetTranslation();

		LinearSoftLambda = (FReal)0;
		LinearDriveLambda = (FReal)0;
		TwistSoftLambda = (FReal)0;
		SwingSoftLambda = (FReal)0;
		TwistDriveLambda = (FReal)0;
		Swing1DriveLambda = (FReal)0;
		Swing2DriveLambda = (FReal)0;

		PositionTolerance = SolverSettings.PositionTolerance;
		AngleTolerance = SolverSettings.AngleTolerance;
		ProjectionInvMassScale = SolverSettings.ProjectionInvMassScale;
		VelProjectionInvMassScale = SolverSettings.VelProjectionInvMassScale;

		InitDerivedState();
	}


	void FJointSolverGaussSeidel::Update(
		const FReal Dt,
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

		UpdateDerivedState();
	}


	int32 FJointSolverGaussSeidel::ApplyConstraints(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		int32 NumActive = 0;

		bool bHasPositionConstraints =
			(JointSettings.LinearMotionTypes[0] != EJointMotionType::Free)
			|| (JointSettings.LinearMotionTypes[1] != EJointMotionType::Free)
			|| (JointSettings.LinearMotionTypes[2] != EJointMotionType::Free);

		bool bHasRotationConstraints =
			(JointSettings.AngularMotionTypes[0] != EJointMotionType::Free)
			|| (JointSettings.AngularMotionTypes[1] != EJointMotionType::Free)
			|| (JointSettings.AngularMotionTypes[2] != EJointMotionType::Free);

		if (bHasPositionConstraints)
		{
			NumActive += ApplyPositionConstraints(Dt, SolverSettings, JointSettings);
		}

		if (bHasRotationConstraints)
		{
			NumActive += ApplyRotationConstraints(Dt, SolverSettings, JointSettings);
		}

		return NumActive;
	}


	int32 FJointSolverGaussSeidel::ApplyDrives(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		int32 NumActive = 0;

		bool bHasPositionDrives = 
			JointSettings.bLinearPositionDriveEnabled[0]
			|| JointSettings.bLinearPositionDriveEnabled[1]
			|| JointSettings.bLinearPositionDriveEnabled[2]
			|| JointSettings.bLinearVelocityDriveEnabled[0]
			|| JointSettings.bLinearVelocityDriveEnabled[1]
			|| JointSettings.bLinearVelocityDriveEnabled[2];

		bool bHasRotationDrives = 
			JointSettings.bAngularTwistPositionDriveEnabled
			|| JointSettings.bAngularTwistVelocityDriveEnabled
			|| JointSettings.bAngularSwingPositionDriveEnabled
			|| JointSettings.bAngularSwingVelocityDriveEnabled
			|| JointSettings.bAngularSLerpPositionDriveEnabled
			|| JointSettings.bAngularSLerpVelocityDriveEnabled;

		if (bHasPositionDrives)
		{
			NumActive += ApplyPositionDrives(Dt, SolverSettings, JointSettings);
		}

		if (bHasRotationDrives)
		{
			NumActive += ApplyRotationDrives(Dt, SolverSettings, JointSettings);
		}

		return NumActive;
	}


	int32 FJointSolverGaussSeidel::ApplyProjections(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		int32 NumActive = 0;
		const FReal LinearProjection = FPBDJointUtilities::GetLinearProjection(SolverSettings, JointSettings);
		const FReal AngularProjection = FPBDJointUtilities::GetAngularProjection(SolverSettings, JointSettings);
		if ((LinearProjection == 0.0f) && (AngularProjection == 0.0f))
		{
			return NumActive;
		}

		const TVector<EJointMotionType, 3>& LinearMotion = JointSettings.LinearMotionTypes;
		const EJointMotionType TwistMotion = JointSettings.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Twist];
		const EJointMotionType Swing1Motion = JointSettings.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing1];
		const EJointMotionType Swing2Motion = JointSettings.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing2];
		const bool bTwistSoft = JointSettings.bSoftTwistLimitsEnabled;
		const bool bSwingSoft = JointSettings.bSoftSwingLimitsEnabled;

		const bool bPointProject = 
			(LinearProjection > 0.0f) 
			&& (LinearMotion[0] == EJointMotionType::Locked)
			&& (LinearMotion[1] == EJointMotionType::Locked)
			&& (LinearMotion[2] == EJointMotionType::Locked);
		bool bConeProject = 
			SolverSettings.bEnableSwingLimits 
			&& (AngularProjection > 0.0f) 
			&& !bSwingSoft 
			&& (Swing1Motion == EJointMotionType::Limited) 
			&& (Swing2Motion == EJointMotionType::Limited);

		if (bPointProject && bConeProject)
		{
			// Fixed point constraint with cone limits
			NumActive += ApplyPointConeProjection(Dt, SolverSettings, JointSettings);
		}
		else if (bPointProject)
		{
			// Fixed point constraint with no angular limits
			NumActive += ApplyPointProjection(Dt, SolverSettings, JointSettings);
		}

		return NumActive;
	}


	int32 FJointSolverGaussSeidel::ApplyRotationConstraints(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		// Locked axes always use hard constraints. Limited axes use hard or soft depending on settings
		int32 NumActive = 0;

		EJointMotionType TwistMotion = JointSettings.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Twist];
		EJointMotionType Swing1Motion = JointSettings.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing1];
		EJointMotionType Swing2Motion = JointSettings.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing2];
		bool bTwistSoft = JointSettings.bSoftTwistLimitsEnabled;
		bool bSwingSoft = JointSettings.bSoftSwingLimitsEnabled;

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
				NumActive += ApplyTwistConstraint(Dt, SolverSettings, JointSettings, bTwistSoft);
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
				NumActive += ApplyConeConstraint(Dt, SolverSettings, JointSettings, bSwingSoft);
			}
			else if ((Swing1Motion == EJointMotionType::Limited) && (Swing2Motion == EJointMotionType::Locked))
			{
				NumActive += ApplySingleLockedSwingConstraint(Dt, SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing2, false);
				if (!bDegenerate)
				{
					NumActive += ApplySwingConstraint(Dt, SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing1, bSwingSoft);
				}
			}
			else if ((Swing1Motion == EJointMotionType::Limited) && (Swing2Motion == EJointMotionType::Free))
			{
				if (!bDegenerate)
				{
					NumActive += ApplyDualConeSwingConstraint(Dt, SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing1, bSwingSoft);
				}
			}
			else if ((Swing1Motion == EJointMotionType::Locked) && (Swing2Motion == EJointMotionType::Limited))
			{
				NumActive += ApplySingleLockedSwingConstraint(Dt, SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing1, false);
				if (!bDegenerate)
				{
					NumActive += ApplySwingConstraint(Dt, SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing2, bSwingSoft);
				}
			}
			else if ((Swing1Motion == EJointMotionType::Locked) && (Swing2Motion == EJointMotionType::Locked))
			{
				// Covered below
			}
			else if ((Swing1Motion == EJointMotionType::Locked) && (Swing2Motion == EJointMotionType::Free))
			{
				NumActive += ApplySingleLockedSwingConstraint(Dt, SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing1, false);
			}
			else if ((Swing1Motion == EJointMotionType::Free) && (Swing2Motion == EJointMotionType::Limited))
			{
				if (!bDegenerate)
				{
					NumActive += ApplyDualConeSwingConstraint(Dt, SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing2, bSwingSoft);
				}
			}
			else if ((Swing1Motion == EJointMotionType::Free) && (Swing2Motion == EJointMotionType::Locked))
			{
				NumActive += ApplySingleLockedSwingConstraint(Dt, SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing2, false);
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
			NumActive += ApplyLockedRotationConstraints(Dt, SolverSettings, JointSettings, bLockedTwist, bLockedSwing);
		}

		return NumActive;
	}


	int32 FJointSolverGaussSeidel::ApplyRotationDrives(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		int32 NumActive = 0;

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
				NumActive += ApplySLerpDrive(Dt, SolverSettings, JointSettings);
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

		return NumActive;
	}


	int32 FJointSolverGaussSeidel::ApplyPositionConstraints(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		// @todo(ccaulfield): the branch logic is all constant for a joint - move it to initialization and turn it into a table or something (also for Hard/Soft login in apply functions)
		int32 NumActive = 0;

		const TVector<EJointMotionType, 3>& LinearMotion = JointSettings.LinearMotionTypes;
		const TVector<bool, 3> bLinearLocked =
		{
			(LinearMotion[0] == EJointMotionType::Locked),
			(LinearMotion[1] == EJointMotionType::Locked),
			(LinearMotion[2] == EJointMotionType::Locked),
		};
		const TVector<bool, 3> bLinearLimted =
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
				NumActive += ApplyPointPositionConstraintKD(0, 1, Dt, SolverSettings, JointSettings);
			}
			else if (InvMs[1] == 0)
			{
				NumActive += ApplyPointPositionConstraintKD(1, 0, Dt, SolverSettings, JointSettings);
			}
			else
			{
				NumActive += ApplyPointPositionConstraintDD(Dt, SolverSettings, JointSettings);
			}
		}
		else if (bLinearLimted[0] && bLinearLimted[1] && bLinearLimted[2])
		{
			// Spherical constraint
			NumActive += ApplySphericalPositionConstraint(Dt, SolverSettings, JointSettings);
		}
		else if (bLinearLocked[1] && bLinearLocked[2] && !bLinearLocked[0])
		{
			// Line constraint along X axis
			NumActive += ApplyCylindricalPositionConstraint(Dt, 0, LinearMotion[0], EJointMotionType::Locked, SolverSettings, JointSettings);
		}
		else if (bLinearLocked[0] && bLinearLocked[2] && !bLinearLocked[1])
		{
			// Line constraint along Y axis
			NumActive += ApplyCylindricalPositionConstraint(Dt, 1, LinearMotion[1], EJointMotionType::Locked, SolverSettings, JointSettings);
		}
		else if (bLinearLocked[0] && bLinearLocked[1] && !bLinearLocked[2])
		{
			// Line constraint along Z axis
			NumActive += ApplyCylindricalPositionConstraint(Dt, 2, LinearMotion[2], EJointMotionType::Locked, SolverSettings, JointSettings);
		}
		else if (bLinearLimted[1] && bLinearLimted[2] && !bLinearLimted[0])
		{
			// Cylindrical constraint along X axis
			NumActive += ApplyCylindricalPositionConstraint(Dt, 0, LinearMotion[0], EJointMotionType::Limited, SolverSettings, JointSettings);
		}
		else if (bLinearLimted[0] && bLinearLimted[2] && !bLinearLimted[1])
		{
			// Cylindrical constraint along Y axis
			NumActive += ApplyCylindricalPositionConstraint(Dt, 1, LinearMotion[1], EJointMotionType::Limited, SolverSettings, JointSettings);
		}
		else if (bLinearLimted[0] && bLinearLimted[1] && !bLinearLimted[2])
		{
			// Cylindrical constraint along Z axis
			NumActive += ApplyCylindricalPositionConstraint(Dt, 2, LinearMotion[2], EJointMotionType::Limited, SolverSettings, JointSettings);
		}
		else if (bLinearLocked[0] || bLinearLimted[0])
		{
			// Planar constraint along X axis
			NumActive += ApplyPlanarPositionConstraint(Dt, 0, LinearMotion[0], SolverSettings, JointSettings);
		}
		else if (bLinearLocked[1] || bLinearLimted[1])
		{
			// Planar constraint along Y axis
			NumActive += ApplyPlanarPositionConstraint(Dt, 1, LinearMotion[1], SolverSettings, JointSettings);
		}
		else if (bLinearLocked[2] || bLinearLimted[2])
		{
			// Planar constraint along Z axis
			NumActive += ApplyPlanarPositionConstraint(Dt, 2, LinearMotion[2], SolverSettings, JointSettings);
		}

		return NumActive;
	}


	int32 FJointSolverGaussSeidel::ApplyPositionDrives(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		// @todo(ccaulfield): This logic is broken if different axes have drives position/velocity enabled - fix it. 
		// E.g., if X/Y have position drives and Z is a velocity drive, it should be a circular position-drive and axial velocity-drive rather than a spherical one.
		// E.g., if X/Y/Z have position drives and only 1 or 2 axes have velocity drives, we need to apply 2 drives. Etc etc.
		// Basically we need to split the axes by those that have the same drive settings.
		int32 NumActive = 0;

		if (SolverSettings.bEnableDrives)
		{
			TVector<bool, 3> bDriven =
			{
				(JointSettings.bLinearPositionDriveEnabled[0] || JointSettings.bLinearVelocityDriveEnabled[0]) && (JointSettings.LinearMotionTypes[0] != EJointMotionType::Locked),
				(JointSettings.bLinearPositionDriveEnabled[1] || JointSettings.bLinearVelocityDriveEnabled[1]) && (JointSettings.LinearMotionTypes[1] != EJointMotionType::Locked),
				(JointSettings.bLinearPositionDriveEnabled[2] || JointSettings.bLinearVelocityDriveEnabled[2]) && (JointSettings.LinearMotionTypes[2] != EJointMotionType::Locked),
			};

			if (bDriven[0] && bDriven[1] && bDriven[2])
			{
				NumActive += ApplySphericalPositionDrive(Dt, SolverSettings, JointSettings);
			}
			else if (bDriven[1] && bDriven[2])
			{
				NumActive += ApplyCircularPositionDrive(Dt, 0, SolverSettings, JointSettings);
			}
			else if (bDriven[0] && bDriven[2])
			{
				NumActive += ApplyCircularPositionDrive(Dt, 1, SolverSettings, JointSettings);
			}
			else if (bDriven[0] && bDriven[1])
			{
				NumActive += ApplyCircularPositionDrive(Dt, 2, SolverSettings, JointSettings);
			}
			else if (bDriven[0])
			{
				NumActive += ApplyAxialPositionDrive(Dt, 0, SolverSettings, JointSettings);
			}
			else if (bDriven[1])
			{
				NumActive += ApplyAxialPositionDrive(Dt, 1, SolverSettings, JointSettings);
			}
			else if (bDriven[2])
			{
				NumActive += ApplyAxialPositionDrive(Dt, 2, SolverSettings, JointSettings);
			}
		}

		return NumActive;
	}


	//
	//
	//////////////////////////////////////////////////////////////////////////
	//
	//


	void FJointSolverGaussSeidel::ApplyPositionDelta(
		const int32 BodyIndex,
		const FReal Stiffness,
		const FVec3& DP)
	{
		//UE_LOG(LogChaosJoint, VeryVerbose, TEXT("      Apply DP%d %f %f %f"), BodyIndex, DP.X, DP.Y, DP.Z);

		Ps[BodyIndex] += Stiffness * DP;

		Xs[BodyIndex] += Stiffness * DP;
	}


	void FJointSolverGaussSeidel::ApplyPositionDelta(
		const FReal Stiffness,
		const FVec3& DP0,
		const FVec3& DP1)
	{
		//UE_LOG(LogChaosJoint, VeryVerbose, TEXT("      Apply DP%d %f %f %f"), 0, DP0.X, DP0.Y, DP0.Z);
		//UE_LOG(LogChaosJoint, VeryVerbose, TEXT("      Apply DP%d %f %f %f"), 1, DP1.X, DP1.Y, DP1.Z);

		Ps[0] += Stiffness * DP0;
		Ps[1] += Stiffness * DP1;

		Xs[0] += Stiffness * DP0;
		Xs[1] += Stiffness * DP1;
	}


	void FJointSolverGaussSeidel::ApplyRotationDelta(
		const int32 BodyIndex,
		const FReal Stiffness,
		const FVec3& DR)
	{
		//UE_LOG(LogChaosJoint, VeryVerbose, TEXT("      Apply DR%d %f %f %f"), BodyIndex, DR.X, DR.Y, DR.Z);

		const FRotation3 DQ = (FRotation3::FromElements(Stiffness * DR, 0) * Qs[BodyIndex]) * (FReal)0.5;
		Qs[BodyIndex] = (Qs[BodyIndex] + DQ).GetNormalized();
		Qs[1].EnforceShortestArcWith(Qs[0]);

		UpdateDerivedState(BodyIndex);
	}


	void FJointSolverGaussSeidel::ApplyRotationDelta(
		const FReal Stiffness,
		const FVec3& DR0,
		const FVec3& DR1)
	{
		//UE_LOG(LogChaosJoint, VeryVerbose, TEXT("      Apply DR%d %f %f %f"), 0, DR0.X, DR0.Y, DR0.Z);
		//UE_LOG(LogChaosJoint, VeryVerbose, TEXT("      Apply DR%d %f %f %f"), 1, DR1.X, DR1.Y, DR1.Z);

		if (bChaos_Joint_ISPC_Enabled)
		{
#if INTEL_ISPC
			ispc::ApplyRotationDelta2((ispc::FJointSolverGaussSeidel*)this, Stiffness, (ispc::FVector&)DR0, (ispc::FVector&)DR1);
#endif
		}
		else
		{
			if (InvMs[0] > 0.0f)
			{
				const FRotation3 DQ0 = (FRotation3::FromElements(Stiffness * DR0, 0) * Qs[0]) * (FReal)0.5;
				Qs[0] = (Qs[0] + DQ0).GetNormalized();
			}
			if (InvMs[1] > 0.0f)
			{
				const FRotation3 DQ1 = (FRotation3::FromElements(Stiffness * DR1, 0) * Qs[1]) * (FReal)0.5;
				Qs[1] = (Qs[1] + DQ1).GetNormalized();
			}
			Qs[1].EnforceShortestArcWith(Qs[0]);

			UpdateDerivedState();
		}
	}

	void FJointSolverGaussSeidel::ApplyDelta(
		const int32 BodyIndex,
		const FReal Stiffness,
		const FVec3& DP,
		const FVec3& DR)
	{
		//UE_LOG(LogChaosJoint, VeryVerbose, TEXT("      Apply DP%d %f %f %f"), BodyIndex, DP.X, DP.Y, DP.Z);
		//UE_LOG(LogChaosJoint, VeryVerbose, TEXT("      Apply DR%d %f %f %f"), BodyIndex, DR.X, DR.Y, DR.Z);

		Ps[BodyIndex] += Stiffness * DP;
		const FRotation3 DQ = (FRotation3::FromElements(Stiffness * DR, 0) * Qs[BodyIndex]) * (FReal)0.5;
		Qs[BodyIndex] = (Qs[BodyIndex] + DQ).GetNormalized();
		Qs[1].EnforceShortestArcWith(Qs[0]);

		UpdateDerivedState(BodyIndex);
	}


	void FJointSolverGaussSeidel::ApplyVelocityDelta(
		const int32 BodyIndex,
		const FReal Stiffness,
		const FVec3& DV,
		const FVec3& DW)
	{
		Vs[BodyIndex] = Vs[BodyIndex] + Stiffness * DV;
		Ws[BodyIndex] = Ws[BodyIndex] + Stiffness * DW;
	}


	void FJointSolverGaussSeidel::ApplyVelocityDelta(
		const FReal Stiffness,
		const FVec3& DV0,
		const FVec3& DW0,
		const FVec3& DV1,
		const FVec3& DW1)
	{
		Vs[0] += Stiffness * DV0;
		Vs[1] += Stiffness * DV1;
		Ws[0] += Stiffness * DW0;
		Ws[1] += Stiffness * DW1;
	}


	void FJointSolverGaussSeidel::ApplyPositionConstraint(
		const FReal Stiffness,
		const FVec3& Axis,
		const FReal Delta)
	{
		const FVec3 AngularAxis0 = FVec3::CrossProduct(Xs[0] - Ps[0], Axis);
		const FVec3 AngularAxis1 = FVec3::CrossProduct(Xs[1] - Ps[1], Axis);
		const FVec3 IA0 = Utilities::Multiply(InvIs[0], AngularAxis0);
		const FVec3 IA1 = Utilities::Multiply(InvIs[1], AngularAxis1);

		// Joint-space inverse mass
		const FReal II0 = FVec3::DotProduct(AngularAxis0, IA0);
		const FReal II1 = FVec3::DotProduct(AngularAxis1, IA1);
		const FReal IM = InvMs[0] + II0 + InvMs[1] + II1;

		const FVec3 DX = Axis * Delta / IM;

		// Apply constraint correction
		const FVec3 DP0 = InvMs[0] * DX;
		const FVec3 DP1 = -InvMs[1] * DX;
		const FVec3 DR0 = Utilities::Multiply(InvIs[0], FVec3::CrossProduct(Xs[0] - Ps[0], DX));
		const FVec3 DR1 = Utilities::Multiply(InvIs[1], FVec3::CrossProduct(Xs[1] - Ps[1], -DX));

		ApplyPositionDelta(Stiffness, DP0, DP1);
		ApplyRotationDelta(Stiffness, DR0, DR1);
	}


	void FJointSolverGaussSeidel::ApplyPositionConstraintSoft(
		const FReal Dt,
		const FReal Stiffness,
		const FReal Damping,
		const bool bAccelerationMode,
		const FVec3& Axis,
		const FReal Delta,
		FReal& Lambda)
	{
		if (bChaos_Joint_ISPC_Enabled)
		{
#if INTEL_ISPC
			ispc::ApplyPositionConstraintSoft((ispc::FJointSolverGaussSeidel*)this, Dt, Stiffness, Damping, bAccelerationMode, (ispc::FVector&)Axis, Delta, Lambda);
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
			if (Damping > KINDA_SMALL_NUMBER)
			{
				const FVec3 V0 = FVec3::CalculateVelocity(PrevXs[0], Xs[0], 1.0f);
				const FVec3 V1 = FVec3::CalculateVelocity(PrevXs[1], Xs[1], 1.0f);
				VelDt = FVec3::DotProduct(V0 - V1, Axis);
			}
	
			const FReal SpringMassScale = (bAccelerationMode) ? 1.0f / (InvMs[0] + InvMs[1]) : 1.0f;
			const FReal S = SpringMassScale * Stiffness * Dt * Dt;
			const FReal D = SpringMassScale * Damping * Dt;
			const FReal Multiplier = (FReal)1 / ((S + D) * II + (FReal)1);
			const FReal DLambda = Multiplier * (S * Delta - D * VelDt - Lambda);
	
			const FVec3 DP0 = (InvMs[0] * DLambda) * Axis;
			const FVec3 DP1 = (-InvMs[1] * DLambda) * Axis;
			const FVec3 DR0 = DLambda * Utilities::Multiply(InvIs[0], AngularAxis0);
			const FVec3 DR1 = -DLambda * Utilities::Multiply(InvIs[1], AngularAxis1);
	
			Lambda += DLambda;
			ApplyPositionDelta((FReal)1, DP0, DP1);
			ApplyRotationDelta((FReal)1, DR0, DR1);
		}
	}
	

	void FJointSolverGaussSeidel::ApplyRotationConstraintKD(
		const int32 KIndex,
		const int32 DIndex,
		const FReal Stiffness,
		const FVec3& Axis,
		const FReal Angle)
	{
		// NOTE: May be called with non-normalized axis (and similarly scaled angle), hence the divide by 
		// length squared which is already handled by the joint mass calc in ApplyRotationConstraintDD
		const FVec3 DR1 = (-Angle / Axis.SizeSquared()) * Axis;
		ApplyRotationDelta(DIndex, Stiffness, DR1);
	}


	void FJointSolverGaussSeidel::ApplyRotationConstraintDD(
		const FReal Stiffness,
		const FVec3& Axis,
		const FReal Angle)
	{
		// Joint-space inverse mass
		const FVec3 IA0 = Utilities::Multiply(InvIs[0], Axis);
		const FVec3 IA1 = Utilities::Multiply(InvIs[1], Axis);
		const FReal II0 = FVec3::DotProduct(Axis, IA0);
		const FReal II1 = FVec3::DotProduct(Axis, IA1);

		const FVec3 DR0 = IA0 * (Angle / (II0 + II1));
		const FVec3 DR1 = IA1 * -(Angle / (II0 + II1));
		//const FVec3 DR0 = Axis * (Angle * II0 / (II0 + II1));
		//const FVec3 DR1 = Axis * -(Angle * II1 / (II0 + II1));

		ApplyRotationDelta(Stiffness, DR0, DR1);
	}


	void FJointSolverGaussSeidel::ApplyRotationConstraint(
		const FReal Stiffness,
		const FVec3& Axis,
		const FReal Angle)
	{
		if (InvMs[0] == 0)
		{
			ApplyRotationConstraintKD(0, 1, Stiffness, Axis, Angle);
		}
		else if (InvMs[1] == 0)
		{
			ApplyRotationConstraintKD(1, 0, Stiffness, Axis, -Angle);
		}
		else
		{
			ApplyRotationConstraintDD(Stiffness, Axis, Angle);
		}
	}


	// See "XPBD: Position-Based Simulation of Compliant Constrained Dynamics"
	void FJointSolverGaussSeidel::ApplyRotationConstraintSoftKD(
		const int32 KIndex,
		const int32 DIndex,
		const FReal Dt,
		const FReal Stiffness,
		const FReal Damping,
		const bool bAccelerationMode,
		const FVec3& Axis,
		const FReal Angle,
		FReal& Lambda)
	{
		check(InvMs[DIndex] > 0);

		if (bChaos_Joint_ISPC_Enabled)
		{
#if INTEL_ISPC
			ispc::ApplyRotationConstraintSoftKD((ispc::FJointSolverGaussSeidel*)this, KIndex, DIndex, Dt, Stiffness, Damping, bAccelerationMode, (ispc::FVector&) Axis, Angle, Lambda);
#endif
		}
		else
		{
			// World-space inverse mass
			const FVec3 IA1 = Utilities::Multiply(InvIs[1], Axis);

			// Joint-space inverse mass
			FReal II1 = FVec3::DotProduct(Axis, IA1);
			const FReal II = II1;

			// Damping angular velocity
			FReal AngVelDt = 0;
			if (Damping > KINDA_SMALL_NUMBER)
			{
				const FVec3 W1 = FRotation3::CalculateAngularVelocity(PrevQs[DIndex], Qs[DIndex], 1.0f);
				AngVelDt = -FVec3::DotProduct(Axis, W1);
			}

			const FReal SpringMassScale = (bAccelerationMode) ? 1.0f / II : 1.0f;
			const FReal S = SpringMassScale * Stiffness * Dt * Dt;
			const FReal D = SpringMassScale * Damping * Dt;
			const FReal Multiplier = (FReal)1 / ((S + D) * II + (FReal)1);
			const FReal DLambda = Multiplier * (S * Angle - D * AngVelDt - Lambda);

			//const FVec3 DR1 = IA1 * -DLambda;
			const FVec3 DR1 = Axis * -(DLambda * II1);

			Lambda += DLambda;
			ApplyRotationDelta(DIndex, 1.0f, DR1);
		}
	}

	// See "XPBD: Position-Based Simulation of Compliant Constrained Dynamics"
	void FJointSolverGaussSeidel::ApplyRotationConstraintSoftDD(
		const FReal Dt,
		const FReal Stiffness,
		const FReal Damping,
		const bool bAccelerationMode,
		const FVec3& Axis,
		const FReal Angle,
		FReal& Lambda)
	{
		check(InvMs[0] > 0);
		check(InvMs[1] > 0);

		if (bChaos_Joint_ISPC_Enabled)
		{
#if INTEL_ISPC
			ispc::ApplyRotationConstraintSoftDD((ispc::FJointSolverGaussSeidel*)this, Dt, Stiffness, Damping, bAccelerationMode, (ispc::FVector&) Axis, Angle, Lambda);
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
			if (Damping > KINDA_SMALL_NUMBER)
			{
				const FVec3 W0 = FRotation3::CalculateAngularVelocity(PrevQs[0], Qs[0], 1.0f);
				const FVec3 W1 = FRotation3::CalculateAngularVelocity(PrevQs[1], Qs[1], 1.0f);
				AngVelDt = FVec3::DotProduct(Axis, W0) - FVec3::DotProduct(Axis, W1);
			}

			const FReal SpringMassScale = (bAccelerationMode) ? 1.0f / II : 1.0f;
			const FReal S = SpringMassScale * Stiffness * Dt * Dt;
			const FReal D = SpringMassScale * Damping * Dt;
			const FReal Multiplier = (FReal)1 / ((S + D) * II + (FReal)1);
			const FReal DLambda = Multiplier * (S * Angle - D * AngVelDt - Lambda);

			//const FVec3 DR0 = IA0 * DLambda;
			//const FVec3 DR1 = IA1 * -DLambda;
			const FVec3 DR0 = Axis * (DLambda * II0);
			const FVec3 DR1 = Axis * -(DLambda * II1);

			Lambda += DLambda;
			ApplyRotationDelta(1.0f, DR0, DR1);
		}
	}

	void FJointSolverGaussSeidel::ApplyRotationConstraintSoft(
		const FReal Dt,
		const FReal Stiffness,
		const FReal Damping,
		const bool bAccelerationMode,
		const FVec3& Axis,
		const FReal Angle,
		FReal& Lambda)
	{
		if (InvMs[0] == 0)
		{
			ApplyRotationConstraintSoftKD(0, 1, Dt, Stiffness, Damping, bAccelerationMode, Axis, Angle, Lambda);
		}
		else if (InvMs[1] == 0)
		{
			ApplyRotationConstraintSoftKD(1, 0, Dt, Stiffness, Damping, bAccelerationMode, Axis, -Angle, Lambda);
		}
		else
		{
			ApplyRotationConstraintSoftDD(Dt, Stiffness, Damping, bAccelerationMode, Axis, Angle, Lambda);
		}
	}

	//
	//
	//////////////////////////////////////////////////////////////////////////
	//
	//

	int32 FJointSolverGaussSeidel::ApplyLockedRotationConstraints(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const bool bApplyTwist,
		const bool bApplySwing)
	{
		FVec3 Axis0, Axis1, Axis2;
		FPBDJointUtilities::GetLockedRotationAxes(Rs[0], Rs[1], Axis0, Axis1, Axis2);

		const FRotation3 R01 = Rs[0].Inverse() * Rs[1];

		int32 NumActive = 0;
		if (bApplyTwist)
		{
			FReal TwistStiffness = FPBDJointUtilities::GetTwistStiffness(SolverSettings, JointSettings);
			ApplyRotationConstraint(TwistStiffness, Axis0, R01.X);
			++NumActive;
		}

		if (bApplySwing)
		{
			FReal SwingStiffness = FPBDJointUtilities::GetSwingStiffness(SolverSettings, JointSettings);
			ApplyRotationConstraint(SwingStiffness, Axis1, R01.Y);
			ApplyRotationConstraint(SwingStiffness, Axis2, R01.Z);
			++NumActive;
		}

		return NumActive;
	}

	int32 FJointSolverGaussSeidel::ApplyTwistConstraint(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const bool bUseSoftLimit)
	{
		FVec3 TwistAxis;
		FReal TwistAngle;
		FPBDJointUtilities::GetTwistAxisAngle(Rs[0], Rs[1], TwistAxis, TwistAngle);

		// Calculate the twist correction to apply to each body
		FReal DTwistAngle = 0;
		FReal TwistAngleMax = JointSettings.AngularLimits[(int32)EJointAngularConstraintIndex::Twist] + AngleTolerance;
		if (TwistAngle > TwistAngleMax)
		{
			DTwistAngle = TwistAngle - TwistAngleMax;
		}
		else if (TwistAngle < -TwistAngleMax)
		{
			DTwistAngle = TwistAngle + TwistAngleMax;
		}

		UE_LOG(LogChaosJoint, VeryVerbose, TEXT("    Twist Angle %f [Limit %f]"), FMath::RadiansToDegrees(TwistAngle), FMath::RadiansToDegrees(TwistAngleMax));

		// Apply twist correction
		if (FMath::Abs(DTwistAngle) > 0)
		{
			if (bUseSoftLimit)
			{
				const FReal TwistStiffness = FPBDJointUtilities::GetSoftTwistStiffness(SolverSettings, JointSettings);
				const FReal TwistDamping = FPBDJointUtilities::GetSoftTwistDamping(SolverSettings, JointSettings);
				const bool bAccelerationMode = FPBDJointUtilities::GetAngularSoftAccelerationMode(SolverSettings, JointSettings);
				ApplyRotationConstraintSoft(Dt, TwistStiffness, TwistDamping, bAccelerationMode, TwistAxis, DTwistAngle, TwistSoftLambda);
			}
			else
			{
				FReal TwistStiffness = FPBDJointUtilities::GetTwistStiffness(SolverSettings, JointSettings);
				ApplyRotationConstraint(TwistStiffness, TwistAxis, DTwistAngle);
			}
			return 1;
		}
		return 0;
	}


	int32 FJointSolverGaussSeidel::ApplyTwistProjection(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		// @todo(ccaulfield): implement twist projection
		return 0;
	}


	int32 FJointSolverGaussSeidel::ApplyConeConstraint(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const bool bUseSoftLimit)
	{
		FVec3 SwingAxisLocal;
		FReal DSwingAngle = 0.0f;

		if (!bChaos_Joint_EllipticalFix)
		{
			// Calculate swing angle and axis
			FReal SwingAngle;
			FPBDJointUtilities::GetConeAxisAngleLocal(Rs[0], Rs[1], SolverSettings.SwingTwistAngleTolerance, SwingAxisLocal, SwingAngle);

			// Calculate swing angle error
			FReal SwingAngleMax = FPBDJointUtilities::GetConeAngleLimit(JointSettings, SwingAxisLocal, SwingAngle) + AngleTolerance;
			if (SwingAngle > SwingAngleMax)
			{
				DSwingAngle = SwingAngle - SwingAngleMax;
			}
			else if (SwingAngle < -SwingAngleMax)
			{
				DSwingAngle = SwingAngle + SwingAngleMax;
			}

			UE_LOG(LogChaosJoint, VeryVerbose, TEXT("    Cone Angle %f [Limit %f]"), FMath::RadiansToDegrees(SwingAngle), FMath::RadiansToDegrees(SwingAngleMax));
		}
		else
		{
			const FReal Swing1Limit = JointSettings.AngularLimits[(int32)EJointAngularConstraintIndex::Swing1];
			const FReal Swing2Limit = JointSettings.AngularLimits[(int32)EJointAngularConstraintIndex::Swing2];
			FPBDJointUtilities::GetEllipticalConeAxisErrorLocal(Rs[0], Rs[1], Swing2Limit, Swing1Limit, SwingAxisLocal, DSwingAngle);

			UE_LOG(LogChaosJoint, VeryVerbose, TEXT("    Cone Error %f [Limits %f %f]"), FMath::RadiansToDegrees(DSwingAngle), FMath::RadiansToDegrees(Swing2Limit), FMath::RadiansToDegrees(Swing1Limit));
		}

		const FVec3 SwingAxis = Rs[0] * SwingAxisLocal;

		// Apply swing correction to each body
		if (FMath::Abs(DSwingAngle) > 0)
		{
			if (bUseSoftLimit)
			{
				const FReal SoftSwingStiffness = FPBDJointUtilities::GetSoftSwingStiffness(SolverSettings, JointSettings);
				const FReal SoftSwingDamping = FPBDJointUtilities::GetSoftSwingDamping(SolverSettings, JointSettings);
				const bool bAccelerationMode = FPBDJointUtilities::GetAngularSoftAccelerationMode(SolverSettings, JointSettings);
				ApplyRotationConstraintSoft(Dt, SoftSwingStiffness, SoftSwingDamping, bAccelerationMode, SwingAxis, DSwingAngle, SwingSoftLambda);
			}
			else
			{
				FReal SwingStiffness = FPBDJointUtilities::GetSwingStiffness(SolverSettings, JointSettings);
				ApplyRotationConstraint(SwingStiffness, SwingAxis, DSwingAngle);
			}
			return 1;
		}
		return 0;
	}


	int32 FJointSolverGaussSeidel::ApplyConeProjection(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		const FReal Stiffness = FPBDJointUtilities::GetAngularProjection(SolverSettings, JointSettings);
		if (Stiffness == 0.0f)
		{
			return 0;
		}

		const FReal Swing1Limit = JointSettings.AngularLimits[(int32)EJointAngularConstraintIndex::Swing1];
		const FReal Swing2Limit = JointSettings.AngularLimits[(int32)EJointAngularConstraintIndex::Swing2];

		// Fix the rotation - treat parent as infinite mass
		{
			// Calculate the swing error and axis
			FVec3 SwingAxisLocal;
			FReal DSwingAngle = 0.0f;
			FPBDJointUtilities::GetEllipticalConeAxisErrorLocal(Rs[0], Rs[1], Swing2Limit, Swing1Limit, SwingAxisLocal, DSwingAngle);
			const FVec3 SwingAxis = Rs[0] * SwingAxisLocal;

			if (DSwingAngle < AngleTolerance)
			{
				return 0;
			}

			ApplyRotationConstraintKD(0, 1, Stiffness, SwingAxis, DSwingAngle);
		}

		// Fix the relative velocity - this time use the actual inertias rather than treating the parent as infinite mass
		{
			// Calculate the swing error and axis
			FVec3 SwingAxisLocal;
			FReal DSwingAngle = 0.0f;
			FPBDJointUtilities::GetEllipticalConeAxisErrorLocal(Rs[0], Rs[1], Swing2Limit, Swing1Limit, SwingAxisLocal, DSwingAngle);
			const FVec3 SwingAxis = Rs[0] * SwingAxisLocal;

			const FReal W0 = FVec3::DotProduct(Ws[0], SwingAxis);
			const FReal W1 = FVec3::DotProduct(Ws[1], SwingAxis);
			const FVec3 DW = (W1 - W0) * SwingAxis;

			FReal II0 = 0.0f;
			FReal II1 = 0.0f;
			if (InvMs[0] > 0.0f)
			{
				II0 = FVec3::DotProduct(SwingAxis, Utilities::Multiply(InvIs[0], SwingAxis));
			}
			if (InvMs[1] > 0.0f)
			{
				II1 = FVec3::DotProduct(SwingAxis, Utilities::Multiply(InvIs[1], SwingAxis));
			}

			if (InvMs[0] > 0.0f)
			{
				const FVec3 DW0 = (II0 / (II0 + II1)) * DW;
				ApplyVelocityDelta(0, Stiffness, FVec3(0), DW0);
			}
			if (InvMs[1] > 0.0f)
			{
				const FVec3 DW1 = -(II1 / (II0 + II1)) * DW;
				ApplyVelocityDelta(1, Stiffness, FVec3(0), DW1);
			}
		}

		return 1;
	}


	int32 FJointSolverGaussSeidel::ApplySingleLockedSwingConstraint(
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
				ApplyRotationConstraintSoft(Dt, SoftSwingStiffness, SoftSwingDamping, bAccelerationMode, SwingAxis, DSwingAngle, SwingSoftLambda);
			}
			else
			{
				const FReal SwingStiffness = FPBDJointUtilities::GetSwingStiffness(SolverSettings, JointSettings);
				ApplyRotationConstraint(SwingStiffness, SwingAxis, DSwingAngle);
			}
			return 1;
		}
		return 0;
	}


	int32 FJointSolverGaussSeidel::ApplyDualConeSwingConstraint(
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
		FReal DSwingAngle = 0;
		const FReal SwingAngleMax = JointSettings.AngularLimits[(int32)SwingConstraintIndex] + AngleTolerance;
		if (SwingAngle > SwingAngleMax)
		{
			DSwingAngle = SwingAngle - SwingAngleMax;
		}
		else if (SwingAngle < -SwingAngleMax)
		{
			DSwingAngle = SwingAngle + SwingAngleMax;
		}

		UE_LOG(LogChaosJoint, VeryVerbose, TEXT("    DualConeSwing%d Angle %f [Limit %f]"), (SwingConstraintIndex == EJointAngularConstraintIndex::Swing1) ? 1 : 2, FMath::RadiansToDegrees(SwingAngle), FMath::RadiansToDegrees(SwingAngleMax));

		// Apply swing correction
		if (FMath::Abs(DSwingAngle) > 0)
		{
			if (bUseSoftLimit)
			{
				const FReal SoftSwingStiffness = FPBDJointUtilities::GetSoftSwingStiffness(SolverSettings, JointSettings);
				const FReal SoftSwingDamping = FPBDJointUtilities::GetSoftSwingDamping(SolverSettings, JointSettings);
				const bool bAccelerationMode = FPBDJointUtilities::GetAngularSoftAccelerationMode(SolverSettings, JointSettings);
				ApplyRotationConstraintSoft(Dt, SoftSwingStiffness, SoftSwingDamping, bAccelerationMode, SwingAxis, DSwingAngle, SwingSoftLambda);
			}
			else
			{
				const FReal SwingStiffness = FPBDJointUtilities::GetSwingStiffness(SolverSettings, JointSettings);
				ApplyRotationConstraint(SwingStiffness, SwingAxis, DSwingAngle);
			}
			return 1;
		}
		return 0;
	}


	int32 FJointSolverGaussSeidel::ApplySwingConstraint(
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
		FReal DSwingAngle = 0;
		const FReal SwingAngleMax = JointSettings.AngularLimits[(int32)SwingConstraintIndex] + AngleTolerance;
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
		if (FMath::Abs(DSwingAngle) > 0)
		{
			if (bUseSoftLimit)
			{
				const FReal SoftSwingStiffness = FPBDJointUtilities::GetSoftSwingStiffness(SolverSettings, JointSettings);
				const FReal SoftSwingDamping = FPBDJointUtilities::GetSoftSwingDamping(SolverSettings, JointSettings);
				const bool bAccelerationMode = FPBDJointUtilities::GetAngularSoftAccelerationMode(SolverSettings, JointSettings);
				ApplyRotationConstraintSoft(Dt, SoftSwingStiffness, SoftSwingDamping, bAccelerationMode, SwingAxis, DSwingAngle, SwingSoftLambda);
			}
			else
			{
				const FReal SwingStiffness = FPBDJointUtilities::GetSwingStiffness(SolverSettings, JointSettings);
				ApplyRotationConstraint(SwingStiffness, SwingAxis, DSwingAngle);
			}
			return 1;
		}
		return 0;
	}


	int32 FJointSolverGaussSeidel::ApplySwingProjection(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const EJointAngularConstraintIndex SwingConstraintIndex)
	{
		// @todo(ccaulfield): implement swing projection
		return 0;
	}


	int32 FJointSolverGaussSeidel::ApplySwingTwistDrives(
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
		int32 NumActive = 0;

		if (bTwistDriveEnabled && (FMath::Abs(DTwistAngle) > AngleTolerance))
		{
			const FVec3 TwistAxis = Rs[1] * FJointConstants::TwistAxis();
			ApplyRotationConstraintSoft(Dt, AngularTwistDriveStiffness, AngularTwistDriveDamping, bAccelerationMode, TwistAxis, DTwistAngle, TwistDriveLambda);
			++NumActive;
		}

		if (bSwing1DriveEnabled && (FMath::Abs(DSwing1Angle) > AngleTolerance))
		{
			const FVec3 Swing1Axis = Rs[1] * FJointConstants::Swing1Axis();
			ApplyRotationConstraintSoft(Dt, AngularSwingDriveStiffness, AngularSwingDriveDamping, bAccelerationMode, Swing1Axis, DSwing1Angle, Swing1DriveLambda);
			++NumActive;
		}

		if (bSwing2DriveEnabled && (FMath::Abs(DSwing2Angle) > AngleTolerance))
		{
			const FVec3 Swing2Axis = Rs[1] * FJointConstants::Swing2Axis();
			ApplyRotationConstraintSoft(Dt, AngularSwingDriveStiffness, AngularSwingDriveDamping, bAccelerationMode, Swing2Axis, DSwing2Angle, Swing2DriveLambda);
			++NumActive;
		}

		return NumActive;
	}


	int32 FJointSolverGaussSeidel::ApplySLerpDrive(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
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
				const FReal AngularDriveStiffness = FPBDJointUtilities::GetAngularSLerpDriveStiffness(SolverSettings, JointSettings);
				const FReal AngularDriveDamping = FPBDJointUtilities::GetAngularSLerpDriveDamping(SolverSettings, JointSettings);
				const bool bAccelerationMode = FPBDJointUtilities::GetDriveAccelerationMode(SolverSettings, JointSettings);
				ApplyRotationConstraintSoft(Dt, AngularDriveStiffness, AngularDriveDamping, bAccelerationMode, SLerpAxis, -SLerpAngle, Swing1DriveLambda);
				return 1;
			}
		}
		return 0;
	}


	// Kinematic-Dynamic bodies
	int32 FJointSolverGaussSeidel::ApplyPointPositionConstraintKD(
		const int32 KIndex,
		const int32 DIndex,
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		check(InvMs[DIndex] > 0);

		FReal LinearStiffness = FPBDJointUtilities::GetLinearStiffness(SolverSettings, JointSettings);
		const FVec3 CX = Xs[DIndex] - Xs[KIndex];

		UE_LOG(LogChaosJoint, VeryVerbose, TEXT("    PointKD Delta %f [Limit %f]"), CX.Size(), PositionTolerance);

		if (CX.SizeSquared() > PositionTolerance * PositionTolerance)
		{
			if (bChaos_Joint_ISPC_Enabled)
			{
#if INTEL_ISPC
				ispc::ApplyPointPositionConstraintKD((ispc::FJointSolverGaussSeidel*)this, KIndex, DIndex, (ispc::FVector&)CX, LinearStiffness);
#endif
			}
			else
			{
				// Calculate constraint correction
				FMatrix33 M = Utilities::ComputeJointFactorMatrix(Xs[DIndex] - Ps[DIndex], InvIs[DIndex], InvMs[DIndex]);
				FMatrix33 MI = M.Inverse();
				const FVec3 DX = Utilities::Multiply(MI, CX);

				// Apply constraint correction
				const FVec3 DP1 = -InvMs[DIndex] * DX;
				const FVec3 DR1 = Utilities::Multiply(InvIs[DIndex], FVec3::CrossProduct(Xs[DIndex] - Ps[DIndex], -DX));

				ApplyDelta(DIndex, LinearStiffness, DP1, DR1);
			}
			return 1;
		}
		return 0;
	}


	// Dynamic-Dynamic bodies
	int32 FJointSolverGaussSeidel::ApplyPointPositionConstraintDD(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		check(InvMs[0] > 0);
		check(InvMs[1] > 0);

		FReal LinearStiffness = FPBDJointUtilities::GetLinearStiffness(SolverSettings, JointSettings);
		const FVec3 CX = Xs[1] - Xs[0];

		UE_LOG(LogChaosJoint, VeryVerbose, TEXT("    PointDD Delta %f [Limit %f]"), CX.Size(), PositionTolerance);

		if (CX.SizeSquared() > PositionTolerance * PositionTolerance)
		{
			if (bChaos_Joint_ISPC_Enabled)
			{
#if INTEL_ISPC
				ispc::ApplyPointPositionConstraintDD((ispc::FJointSolverGaussSeidel*)this, (ispc::FVector&)CX, LinearStiffness);
#endif
			}
			else
			{
				// Calculate constraint correction
				FMatrix33 M0 = Utilities::ComputeJointFactorMatrix(Xs[0] - Ps[0], InvIs[0], InvMs[0]);
				FMatrix33 M1 = Utilities::ComputeJointFactorMatrix(Xs[1] - Ps[1], InvIs[1], InvMs[1]);
				FMatrix33 MI = (M0 + M1).Inverse();
				const FVec3 DX = Utilities::Multiply(MI, CX);

				// Apply constraint correction
				const FVec3 DP0 = InvMs[0] * DX;
				const FVec3 DP1 = -InvMs[1] * DX;
				const FVec3 DR0 = Utilities::Multiply(InvIs[0], FVec3::CrossProduct(Xs[0] - Ps[0], DX));
				const FVec3 DR1 = Utilities::Multiply(InvIs[1], FVec3::CrossProduct(Xs[1] - Ps[1], -DX));

				ApplyPositionDelta(LinearStiffness, DP0, DP1);
				ApplyRotationDelta(LinearStiffness, DR0, DR1);
			}
			return 1;
		}
		return 0;
	}


	int32 FJointSolverGaussSeidel::ApplySphericalPositionConstraint(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		FVec3 Axis;
		FReal Delta;
		FPBDJointUtilities::GetSphericalAxisDelta(Xs[0], Xs[1], Axis, Delta);

		const FReal Error = FMath::Max((FReal)0, Delta - JointSettings.LinearLimit);
		if (FMath::Abs(Error) > PositionTolerance)
		{
			if (!JointSettings.bSoftLinearLimitsEnabled)
			{
				const FReal Stiffness = FPBDJointUtilities::GetLinearStiffness(SolverSettings, JointSettings);
				ApplyPositionConstraint(Stiffness, Axis, Error);
			}
			else
			{
				const FReal Stiffness = FPBDJointUtilities::GetSoftLinearStiffness(SolverSettings, JointSettings);
				const FReal Damping = FPBDJointUtilities::GetSoftLinearDamping(SolverSettings, JointSettings);
				const bool bAccelerationMode = FPBDJointUtilities::GetLinearSoftAccelerationMode(SolverSettings, JointSettings);
				ApplyPositionConstraintSoft(Dt, Stiffness, Damping, bAccelerationMode, Axis, Error, LinearSoftLambda);
			}
			return 1;
		}
		return 0;
	}


	int32 FJointSolverGaussSeidel::ApplySphericalPositionDrive(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		const FVec3 XTarget = Xs[0] + Rs[0] * JointSettings.LinearDriveTarget;
		FVec3 Axis;
		FReal Delta;
		FPBDJointUtilities::GetSphericalAxisDelta(XTarget, Xs[1], Axis, Delta);
		if (FMath::Abs(Delta) > PositionTolerance)
		{
			const FReal Stiffness = FPBDJointUtilities::GetLinearDriveStiffness(SolverSettings, JointSettings);
			const FReal Damping = FPBDJointUtilities::GetLinearDriveDamping(SolverSettings, JointSettings);
			const bool bAccelerationMode = FPBDJointUtilities::GetDriveAccelerationMode(SolverSettings, JointSettings);
			ApplyPositionConstraintSoft(Dt, Stiffness, Damping, bAccelerationMode, Axis, Delta, LinearDriveLambda);
			return 1;
		}
		return 0;
	}


	int32 FJointSolverGaussSeidel::ApplyCylindricalPositionConstraint(
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
		
		int32 NumActive = 0;

		if ((AxialMotion == EJointMotionType::Limited) && JointSettings.bSoftLinearLimitsEnabled)
		{
			// Soft Axial constraint
			const FReal AxialLimit = JointSettings.LinearLimit;
			if (FMath::Abs(AxialDelta) > AxialLimit + PositionTolerance)
			{
				const FReal AxialError = (AxialDelta > 0) ? AxialDelta - AxialLimit : AxialDelta + AxialLimit;
				const FReal Stiffness = FPBDJointUtilities::GetSoftLinearStiffness(SolverSettings, JointSettings);
				const FReal Damping = FPBDJointUtilities::GetSoftLinearDamping(SolverSettings, JointSettings);
				const bool bAccelerationMode = FPBDJointUtilities::GetLinearSoftAccelerationMode(SolverSettings, JointSettings);
				ApplyPositionConstraintSoft(Dt, Stiffness, Damping, bAccelerationMode, Axis, AxialError, LinearSoftLambda);
				++NumActive;
			}
		}
		else if (AxialMotion != EJointMotionType::Free)
		{
			// Hard Axial constraint
			const FReal AxialLimit = (AxialMotion == EJointMotionType::Locked) ? 0 : JointSettings.LinearLimit;
			if (FMath::Abs(AxialDelta) > AxialLimit + PositionTolerance)
			{
				const FReal AxialError = (AxialDelta > 0) ? AxialDelta - AxialLimit : AxialDelta + AxialLimit;
				const FReal Stiffness = FPBDJointUtilities::GetLinearStiffness(SolverSettings, JointSettings);
				ApplyPositionConstraint(Stiffness, Axis, AxialError);
				++NumActive;
			}
		}

		if ((RadialMotion == EJointMotionType::Limited) && JointSettings.bSoftLinearLimitsEnabled)
		{
			// Soft Radial constraint
			const FReal RadialLimit = JointSettings.LinearLimit;
			if (RadialDelta > RadialLimit + PositionTolerance)
			{
				const FReal RadialError = FMath::Max((FReal)0, RadialDelta - RadialLimit);
				const FReal Stiffness = FPBDJointUtilities::GetSoftLinearStiffness(SolverSettings, JointSettings);
				const FReal Damping = FPBDJointUtilities::GetSoftLinearDamping(SolverSettings, JointSettings);
				const bool bAccelerationMode = FPBDJointUtilities::GetLinearSoftAccelerationMode(SolverSettings, JointSettings);
				ApplyPositionConstraintSoft(Dt, Stiffness, Damping, bAccelerationMode, RadialAxis, RadialError, LinearSoftLambda);
				++NumActive;
			}
		}
		else if (RadialMotion != EJointMotionType::Free)
		{
			// Hard Radial constraint
			const FReal RadialLimit = (RadialMotion == EJointMotionType::Locked) ? 0 : JointSettings.LinearLimit;
			if (RadialDelta > RadialLimit + PositionTolerance)
			{
				const FReal RadialError = FMath::Max((FReal)0, RadialDelta - RadialLimit);
				const FReal Stiffness = FPBDJointUtilities::GetLinearStiffness(SolverSettings, JointSettings);
				ApplyPositionConstraint(Stiffness, RadialAxis, RadialError);
				++NumActive;
			}
		}

		return NumActive;
	}


	int32 FJointSolverGaussSeidel::ApplyCircularPositionDrive(
		const FReal Dt,
		const int32 AxisIndex,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		const FVec3 XTarget = Xs[0] + Rs[0] * JointSettings.LinearDriveTarget;
		FVec3 Axis, RadialAxis;
		FReal AxialDelta, RadialDelta;
		FPBDJointUtilities::GetCylindricalAxesDeltas(Rs[0], XTarget, Xs[1], AxisIndex, Axis, AxialDelta, RadialAxis, RadialDelta);
		if (RadialDelta > PositionTolerance)
		{
			const FReal Stiffness = FPBDJointUtilities::GetLinearDriveStiffness(SolverSettings, JointSettings);
			const FReal Damping = FPBDJointUtilities::GetLinearDriveDamping(SolverSettings, JointSettings);
			const bool bAccelerationMode = FPBDJointUtilities::GetDriveAccelerationMode(SolverSettings, JointSettings);
			ApplyPositionConstraintSoft(Dt, Stiffness, Damping, bAccelerationMode, RadialAxis, RadialDelta, LinearDriveLambda);

			return 1;
		}
		return 0;
	}


	int32 FJointSolverGaussSeidel::ApplyPlanarPositionConstraint(
		const FReal Dt,
		const int32 AxisIndex,
		const EJointMotionType AxialMotion,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		FVec3 Axis;
		FReal Delta;
		FPBDJointUtilities::GetPlanarAxisDelta(Rs[0], Xs[0], Xs[1], AxisIndex, Axis, Delta);

		const FReal Limit = (AxialMotion == EJointMotionType::Locked) ? 0 : JointSettings.LinearLimit;
		if (FMath::Abs(Delta) > Limit + PositionTolerance)
		{
			const FReal Error = (Delta > 0) ? Delta - Limit : Delta + Limit;
			if ((AxialMotion == EJointMotionType::Limited) && JointSettings.bSoftLinearLimitsEnabled)
			{
				const FReal Stiffness = FPBDJointUtilities::GetSoftLinearStiffness(SolverSettings, JointSettings);
				const FReal Damping = FPBDJointUtilities::GetSoftLinearDamping(SolverSettings, JointSettings);
				const bool bAccelerationMode = FPBDJointUtilities::GetLinearSoftAccelerationMode(SolverSettings, JointSettings);
				ApplyPositionConstraintSoft(Dt, Stiffness, Damping, bAccelerationMode, Axis, Error, LinearSoftLambda);
			}
			else
			{
				const FReal Stiffness = FPBDJointUtilities::GetLinearStiffness(SolverSettings, JointSettings);
				ApplyPositionConstraint(Stiffness, Axis, Error);
			}
			return 1;
		}
		return 0;
	}


	int32 FJointSolverGaussSeidel::ApplyAxialPositionDrive(
		const FReal Dt,
		const int32 AxisIndex,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		const FVec3 XTarget = Xs[0] + Rs[0] * JointSettings.LinearDriveTarget;
		FVec3 Axis;
		FReal Delta;
		FPBDJointUtilities::GetPlanarAxisDelta(Rs[0], XTarget, Xs[1], AxisIndex, Axis, Delta);
		if (FMath::Abs(Delta) > PositionTolerance)
		{
			const FReal Stiffness = FPBDJointUtilities::GetLinearDriveStiffness(SolverSettings, JointSettings);
			const FReal Damping = FPBDJointUtilities::GetLinearDriveDamping(SolverSettings, JointSettings);
			const bool bAccelerationMode = FPBDJointUtilities::GetDriveAccelerationMode(SolverSettings, JointSettings);
			ApplyPositionConstraintSoft(Dt, Stiffness, Damping, bAccelerationMode, Axis, Delta, LinearDriveLambda);

			return 1;
		}
		return 0;
	}


	// Projection for a fixed point constraint and no rotation constraint
	int32 FJointSolverGaussSeidel::ApplyPointProjection(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		FReal LinearProjection = FPBDJointUtilities::GetLinearProjection(SolverSettings, JointSettings);
		const FVec3 CX = Xs[1] - Xs[0];
		bool bApplyProjection = (CX.Size() > KINDA_SMALL_NUMBER); // + PositionTolerance);
		if (bApplyProjection)
		{
			if (bChaos_Joint_ISPC_Enabled)
			{
#if INTEL_ISPC
				ispc::ApplyPointPositionProjection((ispc::FJointSolverGaussSeidel*)this, (ispc::FVector&)CX, LinearProjection);
#endif
			}
			else
			{
				// Apply a position correction
				{
					const FReal InvM0 = ProjectionInvMassScale * InvMs[0];
					const FMatrix33 InvI0 = ProjectionInvMassScale * InvIs[0];

					FMatrix33 J = FMatrix33(0, 0, 0);
					if (InvM0 > SMALL_NUMBER)
					{
						J += Utilities::ComputeJointFactorMatrix(Xs[0] - Ps[0], InvI0, InvM0);
					}
					if (InvMs[1] > SMALL_NUMBER)
					{
						J += Utilities::ComputeJointFactorMatrix(Xs[1] - Ps[1], InvIs[1], InvMs[1]);
					}
					const FMatrix33 IJ = J.Inverse();
					const FVec3 DX = Utilities::Multiply(IJ, CX);

					if (InvM0 > SMALL_NUMBER)
					{
						const FVec3 DP0 = InvM0 * DX;
						const FVec3 DR0 = Utilities::Multiply(InvI0, FVec3::CrossProduct(Xs[0] - Ps[0], DX));
						ApplyDelta(0, LinearProjection, DP0, DR0);
					}

					if (InvMs[1] > SMALL_NUMBER)
					{
						const FVec3 DP1 = -InvMs[1] * DX;
						const FVec3 DR1 = Utilities::Multiply(InvIs[1], FVec3::CrossProduct(Xs[1] - Ps[1], -DX));
						ApplyDelta(1, LinearProjection, DP1, DR1);
					}
				}

				// Apply a velocity correction to set the relative velocity at the joint to zero
				{
					const FReal InvM0 = VelProjectionInvMassScale * InvMs[0];
					const FMatrix33 InvI0 = VelProjectionInvMassScale * InvIs[0];

					// Calculate joint-space mass
					FMatrix33 J = FMatrix33(0, 0, 0);
					if (InvM0 > SMALL_NUMBER)
					{
						J = J + Utilities::ComputeJointFactorMatrix(Xs[0] - Ps[0], InvI0, InvM0);
					}
					if (InvMs[1] > SMALL_NUMBER)
					{
						J = J + Utilities::ComputeJointFactorMatrix(Xs[1] - Ps[1], InvIs[1], InvMs[1]);
					}
					const FMatrix33 IJ = J.Inverse();

					// Velocity correction
					const FVec3 V0 = Vs[0] + FVec3::CrossProduct(Ws[0], Xs[0] - Ps[0]);
					const FVec3 V1 = Vs[1] + FVec3::CrossProduct(Ws[1], Xs[1] - Ps[1]);
					const FVec3 DV = Utilities::Multiply(IJ, V1 - V0);

					if (InvM0 > SMALL_NUMBER)
					{
						const FVec3 DV0 = InvM0 * DV;
						const FVec3 DW0 = Utilities::Multiply(InvI0, FVec3::CrossProduct(Xs[0] - Ps[0], DV));
						ApplyVelocityDelta(0, LinearProjection, DV0, DW0);
					}
					if (InvMs[1] > SMALL_NUMBER)
					{
						const FVec3 DV1 = -InvMs[1] * DV;
						const FVec3 DW1 = Utilities::Multiply(InvIs[1], FVec3::CrossProduct(Xs[1] - Ps[1], -DV));
						ApplyVelocityDelta(1, LinearProjection, DV1, DW1);
					}
				}
			}
			return 1;
		}
		return 0;
	}

	// Projection for fixed point constraint and cone limited angular constraint
	int32 FJointSolverGaussSeidel::ApplyPointConeProjection(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		FReal LinearProjection = FPBDJointUtilities::GetLinearProjection(SolverSettings, JointSettings);
		const FReal AngularProjection = FPBDJointUtilities::GetAngularProjection(SolverSettings, JointSettings);

		int32 NumApplied = 0;
		bool bConeActive = false;

		const FReal ProjectionPositionTolerance = 0.0f;//PositionTolerance;
		const FReal ProjectionAngleTolerance = 0.0f;//AngleTolerance;

		// Correct the position error
		const FVec3 CX = Xs[1] - Xs[0];
		if (CX.Size() > ProjectionPositionTolerance)
		{
			const FReal InvM0 = ProjectionInvMassScale * InvMs[0];
			const FMatrix33 InvI0 = ProjectionInvMassScale * InvIs[0];

			FMatrix33 J = FMatrix33(0, 0, 0);
			if (InvM0 > SMALL_NUMBER)
			{
				J += Utilities::ComputeJointFactorMatrix(Xs[0] - Ps[0], InvI0, InvM0);
			}
			if (InvMs[1] > SMALL_NUMBER)
			{
				J += Utilities::ComputeJointFactorMatrix(Xs[1] - Ps[1], InvIs[1], InvMs[1]);
			}
			const FMatrix33 IJ = J.Inverse();
			const FVec3 DX = Utilities::Multiply(IJ, CX);

			if (InvM0 > SMALL_NUMBER)
			{
				const FVec3 DP0 = InvM0 * DX;
				const FVec3 DR0 = Utilities::Multiply(InvI0, FVec3::CrossProduct(Xs[0] - Ps[0], DX));
				ApplyDelta(0, LinearProjection, DP0, DR0);
			}
			if (InvMs[1] > SMALL_NUMBER)
			{
				const FVec3 DP1 = -InvMs[1] * DX;
				const FVec3 DR1 = Utilities::Multiply(InvIs[1], FVec3::CrossProduct(Xs[1] - Ps[1], -DX));
				ApplyDelta(1, LinearProjection, DP1, DR1);
			}

			++NumApplied;
		}

		// Correct the cone error while maintaining position constraint by rotating about the swing axis through the joint position
		const FReal Swing1Limit = JointSettings.AngularLimits[(int32)EJointAngularConstraintIndex::Swing1];
		const FReal Swing2Limit = JointSettings.AngularLimits[(int32)EJointAngularConstraintIndex::Swing2];
		FVec3 SwingAxis = FVec3(0);
		{
			const FReal InvM0 = ProjectionInvMassScale * InvMs[0];
			const FMatrix33 InvI0 = ProjectionInvMassScale * InvIs[0];
			FVec3 SwingAxisLocal;
			FReal DSwingAngle = 0.0f;
			FPBDJointUtilities::GetEllipticalConeAxisErrorLocal(Rs[0], Rs[1], Swing2Limit, Swing1Limit, SwingAxisLocal, DSwingAngle);
			SwingAxis = Rs[0] * SwingAxisLocal;
			if (DSwingAngle > ProjectionAngleTolerance)
			{
				if (InvM0 > SMALL_NUMBER)
				{
					const FVec3 DR0 = DSwingAngle * SwingAxis;
					const FVec3 DP0 = FVec3::CrossProduct(DR0, Xs[0] - Ps[0]);
					ApplyDelta(0, AngularProjection, DP0, DR0);
				}
				if (InvMs[1] > SMALL_NUMBER)
				{
					const FVec3 DR1 = -DSwingAngle * SwingAxis;
					const FVec3 DP1 = -FVec3::CrossProduct(DR1, Xs[1] - Ps[1]);
					ApplyDelta(1, AngularProjection, DP1, DR1);
				}

				++NumApplied;
				bConeActive = true;
			}
		}

		// Final position correction. Following this, both the position constraint and swing
		// constraint should be exactly resolved.
		const FVec3 CX2 = Xs[1] - Xs[0];
		if (CX2.Size() > ProjectionPositionTolerance)
		{
			ApplyPositionDelta(1, LinearProjection, -CX2);
		}

		// Velocity correction
		{
			// Recalculate swing axis. THis should not be necessary if we did not rotate the parent body.
			// @todo(ccaulfield): see if we really need this.
			//if (InvM0 > SMALL_NUMBER)
			{
				FVec3 SwingAxisLocal;
				FReal DSwingAngle = 0.0f;
				FPBDJointUtilities::GetEllipticalConeAxisErrorLocal(Rs[0], Rs[1], Swing2Limit, Swing1Limit, SwingAxisLocal, DSwingAngle);
				SwingAxis = Rs[0] * SwingAxisLocal;
			}

			// We may iterate here if the swing constraint is active but the angular velocity does not required
			// fixing (because it is not moving against the constraint), if when fixing the position constraint
			// we introduce an angular velocity that would violate the swing constraint.
			for (int32 ProjIts = 0; ProjIts < 2; ++ProjIts)
			{
				const FReal InvM0 = VelProjectionInvMassScale * InvMs[0];
				const FMatrix33 InvI0 = VelProjectionInvMassScale * InvIs[0];
				
				// Calculate velocity and angular velocity error at joint
				const FVec3 V0 = Vs[0] + FVec3::CrossProduct(Ws[0], Xs[0] - Ps[0]);
				const FVec3 V1 = Vs[1] + FVec3::CrossProduct(Ws[1], Xs[1] - Ps[1]);
				const FReal W0 = FVec3::DotProduct(Ws[0], SwingAxis);
				const FReal W1 = FVec3::DotProduct(Ws[1], SwingAxis);
				const FVec3 CV = V1 - V0;
				const FReal CW = FMath::Max(0.0f, W1 - W0);

				// Calculate the joint-space velocity and angular velocity correction
				// If the swing constraint is active, and we need to fix angular velocity about the axis,
				// we have 4 simultaneous equations to solve (velocity at joint, and angular velocity about axis).
				// If the swing constraint is inactive, or the angular velocity about the axis is negative,
				// we have only 3 equations to solve (velocity at joint).
				FVec3 DV = FVec3(0);
				FReal DW = 0;
				if (bConeActive && (CW > 0.0f))
				{
					// Calculate joint-space mass from the Jacobian
					FMatrix33 J00 = FMatrix33(0, 0, 0);
					FVec3 J01 = FVec3(0);
					FReal J11 = 0;
					if (InvM0 > SMALL_NUMBER)
					{
						J00 += Utilities::ComputeJointFactorMatrix(Xs[0] - Ps[0], InvI0, InvM0);
						J01 += FVec3::CrossProduct(Xs[0] - Ps[0], Utilities::Multiply(InvI0, SwingAxis));
						J11 += FVec3::DotProduct(SwingAxis, Utilities::Multiply(InvI0, SwingAxis));
					}
					if (InvMs[1] > SMALL_NUMBER)
					{
						J00 += Utilities::ComputeJointFactorMatrix(Xs[1] - Ps[1], InvIs[1], InvMs[1]);
						J01 += FVec3::CrossProduct(Xs[1] - Ps[1], Utilities::Multiply(InvIs[1], SwingAxis));
						J11 += FVec3::DotProduct(SwingAxis, Utilities::Multiply(InvIs[1], SwingAxis));
					}
					PMatrix<FReal, 4, 4>& J = reinterpret_cast<PMatrix<FReal, 4, 4>&>(J00);
					J.M[3][0] = -J01.X;
					J.M[3][1] = -J01.Y;
					J.M[3][2] = -J01.Z;
					J.M[0][3] = -J01.X;
					J.M[1][3] = -J01.Y;
					J.M[2][3] = -J01.Z;
					J.M[3][3] = J11;
					PMatrix<FReal, 4, 4> IJ = J.Inverse();

					const TVector<FReal, 4> C = TVector<FReal, 4>(CV.X, CV.Y, CV.Z, CW);
					const TVector<FReal, 4> D = IJ.TransformFVector4(C);
					DV = FVec3(D.X, D.Y, D.Z);
					DW = D.W;
				}
				else
				{
					// Calculate joint-space mass from the Jacobian
					FMatrix33 J00 = FMatrix33(0, 0, 0);
					if (InvM0 > SMALL_NUMBER)
					{
						J00 += Utilities::ComputeJointFactorMatrix(Xs[0] - Ps[0], InvI0, InvM0);
					}
					if (InvMs[1] > SMALL_NUMBER)
					{
						J00 += Utilities::ComputeJointFactorMatrix(Xs[1] - Ps[1], InvIs[1], InvMs[1]);
					}
					FMatrix33 IJ = J00.Inverse();

					DV = Utilities::Multiply(IJ, CV);
				}

				// Apply the world-space velocity and angular velocity correction
				FVec3 DV0 = InvM0 * DV;
				FVec3 DW0 = Utilities::Multiply(InvI0, FVec3::CrossProduct(Xs[0] - Ps[0], DV)) + Utilities::Multiply(InvI0, SwingAxis) * DW;
				if (InvM0 > SMALL_NUMBER)
				{
					ApplyVelocityDelta(0, 1.0f, DV0, DW0);
				}
				FVec3 DV1 = -InvMs[1] * DV;
				FVec3 DWV1 = Utilities::Multiply(InvIs[1], FVec3::CrossProduct(Xs[1] - Ps[1], DV));
				FVec3 DWW1 = Utilities::Multiply(InvIs[1], SwingAxis) * DW;
				FVec3 DW1 = -(DWV1 + DWW1);
				if (InvMs[1] > SMALL_NUMBER)
				{
					ApplyVelocityDelta(1, 1.0f, DV1, DW1);
				}

				const FVec3 V02 = Vs[0] + FVec3::CrossProduct(Ws[0], Xs[0] - Ps[0]);
				const FVec3 V12 = Vs[1] + FVec3::CrossProduct(Ws[1], Xs[1] - Ps[1]);
				const FReal W02 = FVec3::DotProduct(Ws[0], SwingAxis);
				const FReal W12 = FVec3::DotProduct(Ws[1], SwingAxis);
				const FVec3 CV2 = V12 - V02;
				const FReal CW2 = FMath::Max(0.0f, W12 - W02);
				const FVec3 DWOffAxis0 = DW0 - FVec3::DotProduct(DW0, SwingAxis) * SwingAxis;
				const FVec3 DWOffAxis1 = DW1 - FVec3::DotProduct(DW1, SwingAxis) * SwingAxis;
				const FVec3 DWOffAxisW1 = DWW1 - FVec3::DotProduct(DWW1, SwingAxis) * SwingAxis;
				const FVec3 DWOffAxisV1 = DWV1 - FVec3::DotProduct(DWV1, SwingAxis) * SwingAxis;

				// If we resolved the angvel constraint or it was ignored and not reactivated (by the vel constraint), we are done
				if ((bConeActive && (CW > 0.0f)) || (CW2 <= 0.0f))
				{
					break;
				}
			}
		}

		return NumApplied;
	}
}
