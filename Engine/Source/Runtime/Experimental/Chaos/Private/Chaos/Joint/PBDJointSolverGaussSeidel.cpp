// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Joint/PBDJointSolverGaussSeidel.h"
#include "Chaos/Joint/ChaosJointLog.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDJointConstraintUtilities.h"
#include "Chaos/Utilities.h"
#include "ChaosStats.h"
#if INTEL_ISPC
#include "PBDJointSolverGaussSeidel.ispc.generated.h"
#endif

//PRAGMA_DISABLE_OPTIMIZATION

#if !INTEL_ISPC
const bool bChaos_Joint_ISPC_Enabled = false;
#elif UE_BUILD_SHIPPING
const bool bChaos_Joint_ISPC_Enabled = true;
#else
bool bChaos_Joint_ISPC_Enabled = true;
FAutoConsoleVariableRef CVarChaosJointISPCEnabled(TEXT("p.Chaos.Joint.ISPC"), bChaos_Joint_ISPC_Enabled, TEXT("Whether to use ISPC optimizations in the Joint Solver"));
#endif

float Chaos_Joint_DegenerateRotationLimit = -0.998f;	// Cos(176deg)
FAutoConsoleVariableRef CVarChaosJointDegenerateRotationLimit(TEXT("p.Chaos.Joint.DegenerateRotationLimit"), Chaos_Joint_DegenerateRotationLimit, TEXT("Cosine of the swing angle that is considered degerenerate (default Cos(176deg))"));

namespace Chaos
{
	void GetSphericalAxisDelta(
		const FVec3& X0,
		const FVec3& X1,
		FVec3& Axis,
		FReal& Delta)
	{
		Axis = FVec3(0);
		Delta = 0;

		const FVec3 DX = X1 - X0;
		const FReal DXLen = DX.Size();
		if (DXLen > KINDA_SMALL_NUMBER)
		{
			Axis = DX / DXLen;
			Delta = DXLen;
		}
	}


	void GetCylindricalAxesDeltas(
		const FRotation3& R0,
		const FVec3& X0,
		const FVec3& X1,
		const int32 CylinderAxisIndex,
		FVec3& CylinderAxis,
		FReal& CylinderDelta,
		FVec3& RadialAxis,
		FReal& RadialDelta)
	{
		CylinderAxis = FVec3(0);
		CylinderDelta = 0;
		RadialAxis = FVec3(0);
		RadialDelta = 0;

		FVec3 DX = X1 - X0;

		const FMatrix33 R0M = R0.ToMatrix();
		CylinderAxis = R0M.GetAxis(CylinderAxisIndex);
		CylinderDelta = FVec3::DotProduct(DX, CylinderAxis);

		DX = DX - CylinderDelta * CylinderAxis;
		const FReal DXLen = DX.Size();
		if (DXLen > KINDA_SMALL_NUMBER)
		{
			RadialAxis = DX / DXLen;
			RadialDelta = DXLen;
		}
	}


	void GetPlanarAxisDelta(
		const FRotation3& R0,
		const FVec3& X0,
		const FVec3& X1,
		const int32 PlaneAxisIndex,
		FVec3& Axis,
		FReal& Delta)
	{
		const FMatrix33 R0M = R0.ToMatrix();
		Axis = R0M.GetAxis(PlaneAxisIndex);
		Delta = FVec3::DotProduct(X1 - X0, Axis);
	}


	FReal GetConeAngleLimit(
		const FPBDJointSettings& JointSettings,
		const FVec3& SwingAxisLocal,
		const FReal SwingAngle)
	{
		// Calculate swing limit for the current swing axis
		const FReal Swing1Limit = JointSettings.AngularLimits[(int32)EJointAngularConstraintIndex::Swing1];
		const FReal Swing2Limit = JointSettings.AngularLimits[(int32)EJointAngularConstraintIndex::Swing2];

		// Circular swing limit
		FReal SwingAngleMax = Swing1Limit;

		// Elliptical swing limit
		// @todo(ccaulfield): do elliptical constraints properly (axis is still for circular limit)
		if (!FMath::IsNearlyEqual(Swing1Limit, Swing2Limit, KINDA_SMALL_NUMBER))
		{
			// Map swing axis to ellipse and calculate limit for this swing axis
			const FReal DotSwing1 = FMath::Abs(FVec3::DotProduct(SwingAxisLocal, FJointConstants::Swing1Axis()));
			const FReal DotSwing2 = FMath::Abs(FVec3::DotProduct(SwingAxisLocal, FJointConstants::Swing2Axis()));
			SwingAngleMax = FMath::Sqrt(Swing1Limit * DotSwing1 * Swing1Limit * DotSwing1 + Swing2Limit * DotSwing2 * Swing2Limit * DotSwing2);
		}

		return SwingAngleMax;
	}


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


	void FJointSolverGaussSeidel::UpdateDerivedState()
	{
		Xs[0] = Ps[0] + Qs[0] * XLs[0].GetTranslation();
		Xs[1] = Ps[1] + Qs[1] * XLs[1].GetTranslation();
		Rs[0] = Qs[0] * XLs[0].GetRotation();
		Rs[1] = Qs[1] * XLs[1].GetRotation();
		Rs[1].EnforceShortestArcWith(Rs[0]);
	}

	void FJointSolverGaussSeidel::UpdateDerivedState(const int32 BodyIndex)
	{
		Xs[BodyIndex] = Ps[BodyIndex] + Qs[BodyIndex] * XLs[BodyIndex].GetTranslation();
		Rs[BodyIndex] = Qs[BodyIndex] * XLs[BodyIndex].GetRotation();
		Rs[1].EnforceShortestArcWith(Rs[0]);
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
		SwingDriveLambda = (FReal)0;

		AngularPositionCorrection = FPBDJointUtilities::GetAngularPositionCorrection(SolverSettings, JointSettings);
		PositionTolerance = SolverSettings.PositionTolerance;
		AngleTolerance = SolverSettings.AngleTolerance;
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


	FJointSolverResult FJointSolverGaussSeidel::ApplyConstraints(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		FJointSolverResult NetResult;

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
			NetResult += ApplyPositionConstraints(Dt, SolverSettings, JointSettings);
		}

		if (bHasRotationConstraints)
		{
			NetResult += ApplyRotationConstraints(Dt, SolverSettings, JointSettings);
		}

		return NetResult;
	}


	FJointSolverResult FJointSolverGaussSeidel::ApplyDrives(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		FJointSolverResult NetResult;

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
			NetResult += ApplyPositionDrives(Dt, SolverSettings, JointSettings);
		}

		if (bHasRotationDrives)
		{
			NetResult += ApplyRotationDrives(Dt, SolverSettings, JointSettings);
		}

		return NetResult;
	}


	FJointSolverResult FJointSolverGaussSeidel::ApplyProjections(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		FJointSolverResult NetResult;

		//NetResult += ApplyRotationProjection(Dt, SolverSettings, JointSettings);

		NetResult += ApplyPositionProjection(Dt, SolverSettings, JointSettings);

		return NetResult;
	}


	FJointSolverResult FJointSolverGaussSeidel::ApplyRotationConstraints(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		// Locked axes always use hard constraints. Limited axes use hard or soft depending on settings
		FJointSolverResult NetResult;

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
				NetResult += ApplyTwistConstraint(Dt, SolverSettings, JointSettings, bTwistSoft);
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
				NetResult += ApplyConeConstraint(Dt, SolverSettings, JointSettings, bSwingSoft);
			}
			else if ((Swing1Motion == EJointMotionType::Limited) && (Swing2Motion == EJointMotionType::Locked))
			{
				NetResult += ApplySingleLockedSwingConstraint(Dt, SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing2, false);
				if (!bDegenerate)
				{
					NetResult += ApplySwingConstraint(Dt, SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing1, bSwingSoft);
				}
			}
			else if ((Swing1Motion == EJointMotionType::Limited) && (Swing2Motion == EJointMotionType::Free))
			{
				if (!bDegenerate)
				{
					NetResult += ApplyDualConeSwingConstraint(Dt, SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing1, bSwingSoft);
				}
			}
			else if ((Swing1Motion == EJointMotionType::Locked) && (Swing2Motion == EJointMotionType::Limited))
			{
				NetResult += ApplySingleLockedSwingConstraint(Dt, SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing1, false);
				if (!bDegenerate)
				{
					NetResult += ApplySwingConstraint(Dt, SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing2, bSwingSoft);
				}
			}
			else if ((Swing1Motion == EJointMotionType::Locked) && (Swing2Motion == EJointMotionType::Locked))
			{
				// Covered below
			}
			else if ((Swing1Motion == EJointMotionType::Locked) && (Swing2Motion == EJointMotionType::Free))
			{
				NetResult += ApplySingleLockedSwingConstraint(Dt, SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing1, false);
			}
			else if ((Swing1Motion == EJointMotionType::Free) && (Swing2Motion == EJointMotionType::Limited))
			{
				if (!bDegenerate)
				{
					NetResult += ApplyDualConeSwingConstraint(Dt, SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing2, bSwingSoft);
				}
			}
			else if ((Swing1Motion == EJointMotionType::Free) && (Swing2Motion == EJointMotionType::Locked))
			{
				NetResult += ApplySingleLockedSwingConstraint(Dt, SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing2, false);
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
			NetResult += ApplyLockedRotationConstraints(Dt, SolverSettings, JointSettings, bLockedTwist, bLockedSwing);
		}

		return NetResult;
	}


	FJointSolverResult FJointSolverGaussSeidel::ApplyRotationDrives(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		FJointSolverResult NetResult;

		EJointMotionType TwistMotion = JointSettings.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Twist];
		EJointMotionType Swing1Motion = JointSettings.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing1];
		EJointMotionType Swing2Motion = JointSettings.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing2];

		if (SolverSettings.bEnableDrives)
		{
			bool bTwistLocked = TwistMotion == EJointMotionType::Locked;
			bool bSwing1Locked = Swing1Motion == EJointMotionType::Locked;
			bool bSwing2Locked = Swing2Motion == EJointMotionType::Locked;

			// No SLerp drive if we have a locked rotation (it will be grayed out in the editor in this case, but could still have been set before the rotation was locked)
			if ((JointSettings.bAngularSLerpPositionDriveEnabled || JointSettings.bAngularSLerpVelocityDriveEnabled) && !bTwistLocked && !bSwing1Locked && !bSwing2Locked)
			{
				NetResult += ApplySLerpDrive(Dt, SolverSettings, JointSettings);
			}
			else
			{
				if ((JointSettings.bAngularTwistPositionDriveEnabled || JointSettings.bAngularTwistVelocityDriveEnabled) && !bTwistLocked)
				{
					NetResult += ApplyTwistDrive(Dt, SolverSettings, JointSettings);
				}

				const bool bSwingDriveEnabled = (JointSettings.bAngularSwingPositionDriveEnabled || JointSettings.bAngularSwingVelocityDriveEnabled);
				if (bSwingDriveEnabled && !bSwing1Locked && !bSwing2Locked)
				{
					NetResult += ApplyConeDrive(Dt, SolverSettings, JointSettings);
				}
				else if (bSwingDriveEnabled && !bSwing1Locked)
				{
					NetResult += ApplySwingDrive(Dt, SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing1);
				}
				else if (bSwingDriveEnabled && !bSwing2Locked)
				{
					NetResult += ApplySwingDrive(Dt, SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing2);
				}
			}
		}

		return NetResult;
	}


	FJointSolverResult FJointSolverGaussSeidel::ApplyRotationProjection(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		FJointSolverResult NetResult;

		EJointMotionType TwistMotion = JointSettings.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Twist];
		EJointMotionType Swing1Motion = JointSettings.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing1];
		EJointMotionType Swing2Motion = JointSettings.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing2];
		bool bTwistSoft = JointSettings.bSoftTwistLimitsEnabled;
		bool bSwingSoft = JointSettings.bSoftSwingLimitsEnabled;

		if (SolverSettings.bEnableTwistLimits)
		{
			if ((TwistMotion == EJointMotionType::Locked) || ((TwistMotion == EJointMotionType::Limited) && !bTwistSoft))
			{
				NetResult += ApplyTwistProjection(Dt, SolverSettings, JointSettings);
			}
		}

		if (SolverSettings.bEnableSwingLimits)
		{
			if ((Swing1Motion == EJointMotionType::Limited) && (Swing2Motion == EJointMotionType::Limited) && !bSwingSoft)
			{
				NetResult += ApplyConeProjection(Dt, SolverSettings, JointSettings);
			}
			else
			{
				if ((Swing1Motion == EJointMotionType::Locked) || ((Swing1Motion == EJointMotionType::Limited) && !bSwingSoft))
				{
					NetResult += ApplySwingProjection(Dt, SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing1);
				}
				if ((Swing2Motion == EJointMotionType::Locked) || ((Swing2Motion == EJointMotionType::Limited) && !bSwingSoft))
				{
					NetResult += ApplySwingProjection(Dt, SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing2);
				}
			}
		}

		return NetResult;
	}


	FJointSolverResult FJointSolverGaussSeidel::ApplyPositionConstraints(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		// @todo(ccaulfield): the branch logic is all constant for a joint - move it to initialization and turn it into a table or something (also for Hard/Soft login in apply functions)
		FJointSolverResult NetResult;

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
				NetResult += ApplyPointPositionConstraintKD(0, 1, Dt, SolverSettings, JointSettings);
			}
			else if (InvMs[1] == 0)
			{
				NetResult += ApplyPointPositionConstraintKD(1, 0, Dt, SolverSettings, JointSettings);
			}
			else
			{
				NetResult += ApplyPointPositionConstraintDD(Dt, SolverSettings, JointSettings);
			}
		}
		else if (bLinearLimted[0] && bLinearLimted[1] && bLinearLimted[2])
		{
			// Spherical constraint
			NetResult += ApplySphericalPositionConstraint(Dt, SolverSettings, JointSettings);
		}
		else if (bLinearLocked[1] && bLinearLocked[2] && !bLinearLocked[0])
		{
			// Line constraint along X axis
			NetResult += ApplyCylindricalPositionConstraint(Dt, 0, LinearMotion[0], EJointMotionType::Locked, SolverSettings, JointSettings);
		}
		else if (bLinearLocked[0] && bLinearLocked[2] && !bLinearLocked[1])
		{
			// Line constraint along Y axis
			NetResult += ApplyCylindricalPositionConstraint(Dt, 1, LinearMotion[1], EJointMotionType::Locked, SolverSettings, JointSettings);
		}
		else if (bLinearLocked[0] && bLinearLocked[1] && !bLinearLocked[2])
		{
			// Line constraint along Z axis
			NetResult += ApplyCylindricalPositionConstraint(Dt, 2, LinearMotion[2], EJointMotionType::Locked, SolverSettings, JointSettings);
		}
		else if (bLinearLimted[1] && bLinearLimted[2] && !bLinearLimted[0])
		{
			// Cylindrical constraint along X axis
			NetResult += ApplyCylindricalPositionConstraint(Dt, 0, LinearMotion[0], EJointMotionType::Limited, SolverSettings, JointSettings);
		}
		else if (bLinearLimted[0] && bLinearLimted[2] && !bLinearLimted[1])
		{
			// Cylindrical constraint along Y axis
			NetResult += ApplyCylindricalPositionConstraint(Dt, 1, LinearMotion[1], EJointMotionType::Limited, SolverSettings, JointSettings);
		}
		else if (bLinearLimted[0] && bLinearLimted[1] && !bLinearLimted[2])
		{
			// Cylindrical constraint along Z axis
			NetResult += ApplyCylindricalPositionConstraint(Dt, 2, LinearMotion[2], EJointMotionType::Limited, SolverSettings, JointSettings);
		}
		else if (bLinearLocked[0] || bLinearLimted[0])
		{
			// Planar constraint along X axis
			NetResult += ApplyPlanarPositionConstraint(Dt, 0, LinearMotion[0], SolverSettings, JointSettings);
		}
		else if (bLinearLocked[1] || bLinearLimted[1])
		{
			// Planar constraint along Y axis
			NetResult += ApplyPlanarPositionConstraint(Dt, 1, LinearMotion[1], SolverSettings, JointSettings);
		}
		else if (bLinearLocked[2] || bLinearLimted[2])
		{
			// Planar constraint along Z axis
			NetResult += ApplyPlanarPositionConstraint(Dt, 2, LinearMotion[2], SolverSettings, JointSettings);
		}

		return NetResult;
	}


	FJointSolverResult FJointSolverGaussSeidel::ApplyPositionDrives(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		// @todo(ccaulfield): This logic is broken if different axes have drives position/velocity enabled - fix it. 
		// E.g., if X/Y have position drives and Z is a velocity drive, it should be a circular position-drive and axial velocity-drive rather than a spherical one.
		// E.g., if X/Y/Z have position drives and only 1 or 2 axes have velocity drives, we need to apply 2 drives. Etc etc.
		// Basically we need to split the axes by those that have the same drive settings.
		FJointSolverResult NetResult;

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
				NetResult += ApplySphericalPositionDrive(Dt, SolverSettings, JointSettings);
			}
			else if (bDriven[1] && bDriven[2])
			{
				NetResult += ApplyCircularPositionDrive(Dt, 0, SolverSettings, JointSettings);
			}
			else if (bDriven[0] && bDriven[2])
			{
				NetResult += ApplyCircularPositionDrive(Dt, 1, SolverSettings, JointSettings);
			}
			else if (bDriven[0] && bDriven[1])
			{
				NetResult += ApplyCircularPositionDrive(Dt, 2, SolverSettings, JointSettings);
			}
			else if (bDriven[0])
			{
				NetResult += ApplyAxialPositionDrive(Dt, 0, SolverSettings, JointSettings);
			}
			else if (bDriven[1])
			{
				NetResult += ApplyAxialPositionDrive(Dt, 1, SolverSettings, JointSettings);
			}
			else if (bDriven[2])
			{
				NetResult += ApplyAxialPositionDrive(Dt, 2, SolverSettings, JointSettings);
			}
		}

		return NetResult;
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
	}


	void FJointSolverGaussSeidel::ApplyRotationDelta(
		const FReal Stiffness,
		const FVec3& DR0,
		const FVec3& DR1)
	{
		//UE_LOG(LogChaosJoint, VeryVerbose, TEXT("      Apply DR%d %f %f %f"), 0, DR0.X, DR0.Y, DR0.Z);
		//UE_LOG(LogChaosJoint, VeryVerbose, TEXT("      Apply DR%d %f %f %f"), 1, DR1.X, DR1.Y, DR1.Z);

		const FRotation3 DQ0 = (FRotation3::FromElements(Stiffness * DR0, 0) * Qs[0]) * (FReal)0.5;
		const FRotation3 DQ1 = (FRotation3::FromElements(Stiffness * DR1, 0) * Qs[1]) * (FReal)0.5;
		Qs[0] = (Qs[0] + DQ0).GetNormalized();
		Qs[1] = (Qs[1] + DQ1).GetNormalized();
		Qs[1].EnforceShortestArcWith(Qs[0]);
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
		const FMatrix33 InvI0 = Utilities::ComputeWorldSpaceInertia(Qs[0], InvILs[0]);
		const FMatrix33 InvI1 = Utilities::ComputeWorldSpaceInertia(Qs[1], InvILs[1]);
		const FVec3 IA0 = Utilities::Multiply(InvI0, AngularAxis0);
		const FVec3 IA1 = Utilities::Multiply(InvI1, AngularAxis1);

		// Joint-space inverse mass
		const FReal II0 = FVec3::DotProduct(AngularAxis0, IA0);
		const FReal II1 = FVec3::DotProduct(AngularAxis1, IA1);
		const FReal IM = InvMs[0] + II0 + InvMs[1] + II1;

		const FVec3 DX = Axis * Delta / IM;

		// Apply constraint correction
		const FVec3 DP0 = InvMs[0] * DX;
		const FVec3 DP1 = -InvMs[1] * DX;
		const FVec3 DR0 = Utilities::Multiply(InvI0, FVec3::CrossProduct(Xs[0] - Ps[0], DX));
		const FVec3 DR1 = Utilities::Multiply(InvI1, FVec3::CrossProduct(Xs[1] - Ps[1], -DX));

		ApplyPositionDelta(Stiffness, DP0, DP1);
		ApplyRotationDelta(Stiffness, DR0, DR1);
		UpdateDerivedState();
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
			// World-space inverse mass
			const FMatrix33 InvI0 = Utilities::ComputeWorldSpaceInertia(Qs[0], InvILs[0]);
			const FMatrix33 InvI1 = Utilities::ComputeWorldSpaceInertia(Qs[1], InvILs[1]);
	
			// Joint-space inverse mass
			const FVec3 AngularAxis0 = FVec3::CrossProduct(Xs[0] - Ps[0], Axis);
			const FVec3 AngularAxis1 = FVec3::CrossProduct(Xs[1] - Ps[1], Axis);
			const FVec3 IA0 = Utilities::Multiply(InvI0, AngularAxis0);
			const FVec3 IA1 = Utilities::Multiply(InvI1, AngularAxis1);
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
			const FVec3 DR0 = DLambda * Utilities::Multiply(InvI0, AngularAxis0);
			const FVec3 DR1 = -DLambda * Utilities::Multiply(InvI1, AngularAxis1);
	
			Lambda += DLambda;
			ApplyPositionDelta((FReal)1, DP0, DP1);
			ApplyRotationDelta((FReal)1, DR0, DR1);
			UpdateDerivedState();
		}
	}
	
	
	void FJointSolverGaussSeidel::ApplyRotationConstraint(
		const FReal Stiffness,
		const FVec3& Axis,
		const FReal Angle)
	{
		// World-space inverse mass
		const FMatrix33 InvI0 = Utilities::ComputeWorldSpaceInertia(Qs[0], InvILs[0]);
		const FMatrix33 InvI1 = Utilities::ComputeWorldSpaceInertia(Qs[1], InvILs[1]);
		const FVec3 IA0 = Utilities::Multiply(InvI0, Axis);
		const FVec3 IA1 = Utilities::Multiply(InvI1, Axis);

		// Joint-space inverse mass
		const FReal II0 = FVec3::DotProduct(Axis, IA0);
		const FReal II1 = FVec3::DotProduct(Axis, IA1);

		const FVec3 DR0 = IA0 * (Angle / (II0 + II1));
		const FVec3 DR1 = IA1 * -(Angle / (II0 + II1));
		//const FVec3 DR0 = Axis * (Angle * II0 / (II0 + II1));
		//const FVec3 DR1 = Axis * -(Angle * II1 / (II0 + II1));

		ApplyRotationDelta(Stiffness, DR0, DR1);
		UpdateDerivedState();
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
			const FMatrix33 InvI1 = Utilities::ComputeWorldSpaceInertia(Qs[DIndex], InvILs[DIndex]);
			const FVec3 IA1 = Utilities::Multiply(InvI1, Axis);

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
			UpdateDerivedState(DIndex);
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
			const FMatrix33 InvI0 = Utilities::ComputeWorldSpaceInertia(Qs[0], InvILs[0]);
			const FMatrix33 InvI1 = Utilities::ComputeWorldSpaceInertia(Qs[1], InvILs[1]);
			const FVec3 IA0 = Utilities::Multiply(InvI0, Axis);
			const FVec3 IA1 = Utilities::Multiply(InvI1, Axis);

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
			UpdateDerivedState();
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
			ApplyRotationConstraintSoftKD(1, 0, Dt, Stiffness, Damping, bAccelerationMode, Axis, Angle, Lambda);
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

	FJointSolverResult FJointSolverGaussSeidel::ApplyLockedRotationConstraints(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const bool bApplyTwist,
		const bool bApplySwing)
	{
		FVec3 Axis0, Axis1, Axis2;
		FPBDJointUtilities::GetLockedAxes(Rs[0], Rs[1], Axis0, Axis1, Axis2);

		const FRotation3 R01 = Rs[0].Inverse() * Rs[1];

		FJointSolverResult NetResult;
		if (bApplyTwist)
		{
			FReal TwistStiffness = FPBDJointUtilities::GetTwistStiffness(SolverSettings, JointSettings);
			ApplyRotationConstraint(TwistStiffness, Axis0, R01.X);
			NetResult += FJointSolverResult::MakeActive();
		}

		if (bApplySwing)
		{
			FReal SwingStiffness = FPBDJointUtilities::GetSwingStiffness(SolverSettings, JointSettings);
			ApplyRotationConstraint(SwingStiffness, Axis1, R01.Y);
			ApplyRotationConstraint(SwingStiffness, Axis2, R01.Z);
			NetResult += FJointSolverResult::MakeActive();
			NetResult += FJointSolverResult::MakeActive();
		}

		return NetResult;
	}

	FJointSolverResult FJointSolverGaussSeidel::ApplyTwistConstraint(
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

		UE_LOG(LogChaosJoint, VeryVerbose, TEXT("    SoftTwist Angle %f [Limit %f]"), FMath::RadiansToDegrees(TwistAngle), FMath::RadiansToDegrees(TwistAngleMax));

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
			return FJointSolverResult::MakeActive();
		}
		return FJointSolverResult::MakeSolved();
	}


	FJointSolverResult FJointSolverGaussSeidel::ApplyTwistDrive(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		FVec3 TwistAxis;
		FReal TwistAngle;
		FPBDJointUtilities::GetTwistAxisAngle(Rs[0], Rs[1], TwistAxis, TwistAngle);

		const FReal TwistAngleTarget = JointSettings.AngularDriveTargetAngles[(int32)EJointAngularConstraintIndex::Twist];
		const FReal DTwistAngle = TwistAngle - TwistAngleTarget;

		// Apply twist correction
		if (FMath::Abs(DTwistAngle) > AngleTolerance)
		{
			const FReal AngularDriveStiffness = FPBDJointUtilities::GetAngularTwistDriveStiffness(SolverSettings, JointSettings);
			const FReal AngularDriveDamping = FPBDJointUtilities::GetAngularTwistDriveDamping(SolverSettings, JointSettings);
			const bool bAccelerationMode = FPBDJointUtilities::GetDriveAccelerationMode(SolverSettings, JointSettings);
			ApplyRotationConstraintSoft(Dt, AngularDriveStiffness, AngularDriveDamping, bAccelerationMode, TwistAxis, DTwistAngle, TwistDriveLambda);
		}
		return FJointSolverResult::MakeSolved();
	}


	FJointSolverResult FJointSolverGaussSeidel::ApplyTwistProjection(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		// @todo(ccaulfield): implement twist projection
		return FJointSolverResult::MakeSolved();
	}


	FJointSolverResult FJointSolverGaussSeidel::ApplyConeConstraint(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const bool bUseSoftLimit)
	{
		// Calculate swing angle and axis
		FReal SwingAngle;
		FVec3 SwingAxisLocal;
		FPBDJointUtilities::GetConeAxisAngleLocal(Rs[0], Rs[1], SolverSettings.SwingTwistAngleTolerance, SwingAxisLocal, SwingAngle);
		const FVec3 SwingAxis = Rs[0] * SwingAxisLocal;

		// Calculate swing angle error
		FReal SwingAngleMax = GetConeAngleLimit(JointSettings, SwingAxisLocal, SwingAngle) + AngleTolerance;
		FReal DSwingAngle = 0.0f;
		if (SwingAngle > SwingAngleMax)
		{
			DSwingAngle = SwingAngle - SwingAngleMax;
		}
		else if (SwingAngle < -SwingAngleMax)
		{
			DSwingAngle = SwingAngle + SwingAngleMax;
		}

		UE_LOG(LogChaosJoint, VeryVerbose, TEXT("    Cone Angle %f [Limit %f]"), FMath::RadiansToDegrees(SwingAngle), FMath::RadiansToDegrees(SwingAngleMax));

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
			return FJointSolverResult::MakeActive();
		}
		return FJointSolverResult::MakeSolved();
	}


	FJointSolverResult FJointSolverGaussSeidel::ApplyConeDrive(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		// Calculate swing angle and axis
		FReal SwingAngle;
		FVec3 SwingAxisLocal;
		FPBDJointUtilities::GetConeAxisAngleLocal(Rs[0], Rs[1], SolverSettings.SwingTwistAngleTolerance, SwingAxisLocal, SwingAngle);
		const FVec3 SwingAxis = Rs[0] * SwingAxisLocal;

		// Circular swing target (max of Swing1, Swing2 targets)
		// @todo(ccaulfield): what should cone target really do?
		const FReal Swing1Target = JointSettings.AngularDriveTargetAngles[(int32)EJointAngularConstraintIndex::Swing1];
		const FReal Swing2Target = JointSettings.AngularDriveTargetAngles[(int32)EJointAngularConstraintIndex::Swing2];
		const FReal SwingAngleTarget = FMath::Max(Swing1Target, Swing2Target);
		const FReal DSwingAngle = SwingAngle - SwingAngleTarget;

		// Apply drive forces to each body
		if (FMath::Abs(DSwingAngle) > AngleTolerance)
		{
			const FReal AngularDriveStiffness = FPBDJointUtilities::GetAngularSwingDriveStiffness(SolverSettings, JointSettings);
			const FReal AngularDriveDamping = FPBDJointUtilities::GetAngularSwingDriveDamping(SolverSettings, JointSettings);
			const bool bAccelerationMode = FPBDJointUtilities::GetDriveAccelerationMode(SolverSettings, JointSettings);
			ApplyRotationConstraintSoft(Dt, AngularDriveStiffness, AngularDriveDamping, bAccelerationMode, SwingAxis, DSwingAngle, SwingDriveLambda);
		}
		return FJointSolverResult::MakeSolved();
	}


	FJointSolverResult FJointSolverGaussSeidel::ApplyConeProjection(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		return FJointSolverResult::MakeSolved();
	}


	FJointSolverResult FJointSolverGaussSeidel::ApplySingleLockedSwingConstraint(
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
			return FJointSolverResult::MakeActive();
		}
		return FJointSolverResult::MakeSolved();
	}


	FJointSolverResult FJointSolverGaussSeidel::ApplyDualConeSwingConstraint(
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
			return FJointSolverResult::MakeActive();
		}
		return FJointSolverResult::MakeSolved();
	}


	FJointSolverResult FJointSolverGaussSeidel::ApplySwingConstraint(
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
			return FJointSolverResult::MakeActive();
		}
		return FJointSolverResult::MakeSolved();
	}


	FJointSolverResult FJointSolverGaussSeidel::ApplySwingDrive(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const EJointAngularConstraintIndex SwingConstraintIndex)
	{
		FVec3 SwingAxis;
		FReal SwingAngle;
		FPBDJointUtilities::GetSwingAxisAngle(Rs[0], Rs[1], SolverSettings.SwingTwistAngleTolerance, SwingConstraintIndex, SwingAxis, SwingAngle);

		const FReal SwingAngleTarget = JointSettings.AngularDriveTargetAngles[(int32)SwingConstraintIndex];
		const FReal DSwingAngle = SwingAngle - SwingAngleTarget;

		// Apply drive forces to each body
		if (FMath::Abs(DSwingAngle) > AngleTolerance)
		{
			const FReal AngularDriveStiffness = FPBDJointUtilities::GetAngularSwingDriveStiffness(SolverSettings, JointSettings);
			const FReal AngularDriveDamping = FPBDJointUtilities::GetAngularSwingDriveDamping(SolverSettings, JointSettings);
			const bool bAccelerationMode = FPBDJointUtilities::GetDriveAccelerationMode(SolverSettings, JointSettings);
			ApplyRotationConstraintSoft(Dt, AngularDriveStiffness, AngularDriveDamping, bAccelerationMode, SwingAxis, DSwingAngle, SwingDriveLambda);
		}
		return FJointSolverResult::MakeSolved();
	}


	FJointSolverResult FJointSolverGaussSeidel::ApplySwingProjection(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const EJointAngularConstraintIndex SwingConstraintIndex)
	{
		// @todo(ccaulfield): implement swing projection
		return FJointSolverResult::MakeSolved();
	}


	FJointSolverResult FJointSolverGaussSeidel::ApplySLerpDrive(
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

			if (FMath::Abs(SLerpAngle) > AngleTolerance)
			{
				const FReal AngularDriveStiffness = FPBDJointUtilities::GetAngularSLerpDriveStiffness(SolverSettings, JointSettings);
				const FReal AngularDriveDamping = FPBDJointUtilities::GetAngularSLerpDriveDamping(SolverSettings, JointSettings);
				const bool bAccelerationMode = FPBDJointUtilities::GetDriveAccelerationMode(SolverSettings, JointSettings);
				ApplyRotationConstraintSoft(Dt, AngularDriveStiffness, AngularDriveDamping, bAccelerationMode, SLerpAxis, -SLerpAngle, SwingDriveLambda);
			}
		}
		return FJointSolverResult::MakeSolved();
	}


	// Kinematic-Dynamic bodies
	FJointSolverResult FJointSolverGaussSeidel::ApplyPointPositionConstraintKD(
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
				FMatrix33 InvI1 = Utilities::ComputeWorldSpaceInertia(Qs[DIndex], InvILs[DIndex]);
				FMatrix33 M = Utilities::ComputeJointFactorMatrix(Xs[DIndex] - Ps[DIndex], InvI1, InvMs[DIndex]);
				FMatrix33 MI = M.Inverse();
				const FVec3 DX = Utilities::Multiply(MI, CX);

				// Apply constraint correction
				const FVec3 DP1 = -InvMs[DIndex] * DX;
				const FVec3 DR1 = Utilities::Multiply(InvI1, FVec3::CrossProduct(Xs[DIndex] - Ps[DIndex], -DX));

				ApplyDelta(DIndex, LinearStiffness, DP1, DR1);
				UpdateDerivedState(DIndex);
			}
			return FJointSolverResult::MakeActive();
		}
		return FJointSolverResult::MakeSolved();
	}


	// Dynamic-Dynamic bodies
	FJointSolverResult FJointSolverGaussSeidel::ApplyPointPositionConstraintDD(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		check(InvMs[0] > 0);
		check(InvMs[1] > 0);

		FReal LinearStiffness = FPBDJointUtilities::GetLinearStiffness(SolverSettings, JointSettings);
		const FVec3 CX = Xs[1] - Xs[0];

		UE_LOG(LogChaosJoint, VeryVerbose, TEXT("    PointKD Delta %f [Limit %f]"), CX.Size(), PositionTolerance);

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
				FMatrix33 InvI0 = Utilities::ComputeWorldSpaceInertia(Qs[0], InvILs[0]);
				FMatrix33 InvI1 = Utilities::ComputeWorldSpaceInertia(Qs[1], InvILs[1]);
				FMatrix33 M0 = Utilities::ComputeJointFactorMatrix(Xs[0] - Ps[0], InvI0, InvMs[0]);
				FMatrix33 M1 = Utilities::ComputeJointFactorMatrix(Xs[1] - Ps[1], InvI1, InvMs[1]);
				FMatrix33 MI = (M0 + M1).Inverse();
				const FVec3 DX = Utilities::Multiply(MI, CX);

				// Apply constraint correction
				const FVec3 DP0 = InvMs[0] * DX;
				const FVec3 DP1 = -InvMs[1] * DX;
				const FVec3 DR0 = Utilities::Multiply(InvI0, FVec3::CrossProduct(Xs[0] - Ps[0], DX));
				const FVec3 DR1 = Utilities::Multiply(InvI1, FVec3::CrossProduct(Xs[1] - Ps[1], -DX));

				ApplyPositionDelta(LinearStiffness, DP0, DP1);
				ApplyRotationDelta(LinearStiffness, DR0, DR1);
				UpdateDerivedState();
			}
			return FJointSolverResult::MakeActive();
		}
		return FJointSolverResult::MakeSolved();
	}


	FJointSolverResult FJointSolverGaussSeidel::ApplySphericalPositionConstraint(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		FVec3 Axis;
		FReal Delta;
		GetSphericalAxisDelta(Xs[0], Xs[1], Axis, Delta);

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
			return FJointSolverResult::MakeActive();
		}
		return FJointSolverResult::MakeSolved();
	}


	FJointSolverResult FJointSolverGaussSeidel::ApplySphericalPositionDrive(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		const FVec3 XTarget = Xs[0] + Rs[0] * JointSettings.LinearDriveTarget;
		FVec3 Axis;
		FReal Delta;
		GetSphericalAxisDelta(XTarget, Xs[1], Axis, Delta);
		if (FMath::Abs(Delta) > PositionTolerance)
		{
			const FReal Stiffness = FPBDJointUtilities::GetLinearDriveStiffness(SolverSettings, JointSettings);
			const FReal Damping = FPBDJointUtilities::GetLinearDriveDamping(SolverSettings, JointSettings);
			const bool bAccelerationMode = FPBDJointUtilities::GetDriveAccelerationMode(SolverSettings, JointSettings);
			ApplyPositionConstraintSoft(Dt, Stiffness, Damping, bAccelerationMode, Axis, Delta, LinearDriveLambda);
			return FJointSolverResult::MakeActive();
		}
		return FJointSolverResult::MakeSolved();
	}


	FJointSolverResult FJointSolverGaussSeidel::ApplyCylindricalPositionConstraint(
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
		GetCylindricalAxesDeltas(Rs[0], Xs[0], Xs[1], AxisIndex, Axis, AxialDelta, RadialAxis, RadialDelta);
		
		FJointSolverResult NetResult;

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
				NetResult += FJointSolverResult::MakeActive();
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
				NetResult += FJointSolverResult::MakeActive();
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
				NetResult += FJointSolverResult::MakeActive();
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
				NetResult += FJointSolverResult::MakeActive();
			}
		}

		return NetResult;
	}


	FJointSolverResult FJointSolverGaussSeidel::ApplyCircularPositionDrive(
		const FReal Dt,
		const int32 AxisIndex,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		const FVec3 XTarget = Xs[0] + Rs[0] * JointSettings.LinearDriveTarget;
		FVec3 Axis, RadialAxis;
		FReal AxialDelta, RadialDelta;
		GetCylindricalAxesDeltas(Rs[0], XTarget, Xs[1], AxisIndex, Axis, AxialDelta, RadialAxis, RadialDelta);
		if (RadialDelta > PositionTolerance)
		{
			const FReal Stiffness = FPBDJointUtilities::GetLinearDriveStiffness(SolverSettings, JointSettings);
			const FReal Damping = FPBDJointUtilities::GetLinearDriveDamping(SolverSettings, JointSettings);
			const bool bAccelerationMode = FPBDJointUtilities::GetDriveAccelerationMode(SolverSettings, JointSettings);
			ApplyPositionConstraintSoft(Dt, Stiffness, Damping, bAccelerationMode, RadialAxis, RadialDelta, LinearDriveLambda);
		}
		return FJointSolverResult::MakeSolved();
	}


	FJointSolverResult FJointSolverGaussSeidel::ApplyPlanarPositionConstraint(
		const FReal Dt,
		const int32 AxisIndex,
		const EJointMotionType AxialMotion,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		FVec3 Axis;
		FReal Delta;
		GetPlanarAxisDelta(Rs[0], Xs[0], Xs[1], AxisIndex, Axis, Delta);

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
			return FJointSolverResult::MakeActive();
		}
		return FJointSolverResult::MakeSolved();
	}


	FJointSolverResult FJointSolverGaussSeidel::ApplyAxialPositionDrive(
		const FReal Dt,
		const int32 AxisIndex,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		const FVec3 XTarget = Xs[0] + Rs[0] * JointSettings.LinearDriveTarget;
		FVec3 Axis;
		FReal Delta;
		GetPlanarAxisDelta(Rs[0], XTarget, Xs[1], AxisIndex, Axis, Delta);
		if (FMath::Abs(Delta) > PositionTolerance)
		{
			const FReal Stiffness = FPBDJointUtilities::GetLinearDriveStiffness(SolverSettings, JointSettings);
			const FReal Damping = FPBDJointUtilities::GetLinearDriveDamping(SolverSettings, JointSettings);
			const bool bAccelerationMode = FPBDJointUtilities::GetDriveAccelerationMode(SolverSettings, JointSettings);
			ApplyPositionConstraintSoft(Dt, Stiffness, Damping, bAccelerationMode, Axis, Delta, LinearDriveLambda);
		}
		return FJointSolverResult::MakeSolved();
	}


	FJointSolverResult FJointSolverGaussSeidel::ApplyPositionProjection(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings)
	{
		FReal LinearProjection = FPBDJointUtilities::GetLinearProjection(SolverSettings, JointSettings);
		if ((LinearProjection > 0) && !JointSettings.bSoftLinearLimitsEnabled)
		{
			// Apply a position correction with the parent body set to infinite mass, then correct the velocity.
			FVec3 CX = FPBDJointUtilities::GetLimitedPositionError(JointSettings, Rs[0], Xs[1] - Xs[0]);
			const FReal CXLen = CX.Size();
			if (CXLen > KINDA_SMALL_NUMBER + PositionTolerance)
			{
				const FReal ParentMassScale = FMath::Max(0.0f, 1.0f - LinearProjection);
				if (bChaos_Joint_ISPC_Enabled)
				{
#if INTEL_ISPC
					const FReal Stiffness = FPBDJointUtilities::GetLinearStiffness(SolverSettings, JointSettings);
					ispc::ApplyPositionProjection((ispc::FJointSolverGaussSeidel*)this, (ispc::FVector&)CX, CXLen, ParentMassScale, Stiffness);
#endif
				}
				else
				{
					const FVec3 CXDir = CX / CXLen;
					const FVec3 V0 = Vs[0] + FVec3::CrossProduct(Ws[0], Xs[0] - Ps[0]);
					const FVec3 V1 = Vs[1] + FVec3::CrossProduct(Ws[1], Xs[1] - Ps[1]);
					FVec3 CV = FVec3::DotProduct(V1 - V0, CXDir) * CXDir;

					const FReal IM0 = ParentMassScale * InvMs[0];
					const FReal IM1 = InvMs[1];
					const FVec3 IIL0 = ParentMassScale * InvILs[0];
					const FVec3& IIL1 = InvILs[1];
					FMatrix33 II0 = Utilities::ComputeWorldSpaceInertia(Qs[0], IIL0);
					FMatrix33 II1 = Utilities::ComputeWorldSpaceInertia(Qs[1], IIL1);
					FMatrix33 J0 = (IM0 > 0) ? Utilities::ComputeJointFactorMatrix(Xs[0] - Ps[0], II0, IM0) : FMatrix33(0, 0, 0);
					FMatrix33 J1 = Utilities::ComputeJointFactorMatrix(Xs[1] - Ps[1], II1, IM1);
					FMatrix33 IJ = (J0 + J1).Inverse();

					const FVec3 DX = Utilities::Multiply(IJ, CX);
					const FVec3 DV = Utilities::Multiply(IJ, CV);

					const FVec3 DP0 = IM0 * DX;
					const FVec3 DP1 = -IM1 * DX;
					const FVec3 DR0 = Utilities::Multiply(II0, FVec3::CrossProduct(Xs[0] - Ps[0], DX));
					const FVec3 DR1 = Utilities::Multiply(II1, FVec3::CrossProduct(Xs[1] - Ps[1], -DX));

					const FVec3 DV0 = IM0 * DV;
					const FVec3 DV1 = -IM1 * DV;
					const FVec3 DW0 = Utilities::Multiply(II0, FVec3::CrossProduct(Xs[0] - Ps[0], DV));
					const FVec3 DW1 = Utilities::Multiply(II1, FVec3::CrossProduct(Xs[1] - Ps[1], -DV));

					const FReal Stiffness = FPBDJointUtilities::GetLinearStiffness(SolverSettings, JointSettings);
					ApplyPositionDelta(Stiffness, DP0, DP1);
					ApplyRotationDelta(Stiffness, DR0, DR1);
					ApplyVelocityDelta(Stiffness, DV0, DW0, DV1, DW1);
					UpdateDerivedState();
				}
				return FJointSolverResult::MakeActive();
			}
		}
		return FJointSolverResult::MakeSolved();
	}
}
