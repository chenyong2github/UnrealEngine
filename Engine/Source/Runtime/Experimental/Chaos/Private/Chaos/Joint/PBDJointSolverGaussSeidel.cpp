// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/Joint/PBDJointSolverGaussSeidel.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDJointConstraintUtilities.h"
#include "Chaos/Utilities.h"
#include "ChaosLog.h"
#include "ChaosStats.h"

//#pragma optimize("", off)

namespace Chaos
{
	FJointSolverGaussSeidel::FJointSolverGaussSeidel()
	{
	}


	void FJointSolverGaussSeidel::UpdateDerivedState()
	{
		Xs[0] = Ps[0] + Qs[0] * XLs[0].GetTranslation();
		Xs[1] = Ps[1] + Qs[1] * XLs[1].GetTranslation();
		Rs[0] = Qs[0] * XLs[0].GetRotation();
		Rs[1] = Qs[1] * XLs[1].GetRotation();
	}


	void FJointSolverGaussSeidel::Init(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FRotation3& PrevQ0,
		const FRotation3& PrevQ1,
		const FReal InvM0,
		const FMatrix33& InvIL0,
		const FReal InvM1,
		const FMatrix33& InvIL1,
		const FRigidTransform3& XL0,
		const FRigidTransform3& XL1)
	{
		XLs[0] = XL0;
		XLs[1] = XL1;
		InvILs[0] = InvIL0;
		InvILs[1] = InvIL1;
		InvMs[0] = InvM0;
		InvMs[1] = InvM1;
		// @todo(ccaulfield): mass conditioning

		PrevQs[0] = PrevQ0;
		PrevQs[1] = PrevQ1;

		TwistLambda = (FReal)0;
		SwingLambda = (FReal)0;

		LinearStiffness = FPBDJointUtilities::GetLinearStiffness(SolverSettings, JointSettings);
		TwistStiffness = FPBDJointUtilities::GetTwistStiffness(SolverSettings, JointSettings);
		SwingStiffness = FPBDJointUtilities::GetSwingStiffness(SolverSettings, JointSettings);
		AngularDriveStiffness = FPBDJointUtilities::GetAngularDriveStiffness(SolverSettings, JointSettings);
		AngularDriveDamping = FPBDJointUtilities::GetAngularDriveDamping(SolverSettings, JointSettings);
		LinearProjection = FPBDJointUtilities::GetLinearProjection(SolverSettings, JointSettings);
		AngularProjection = FPBDJointUtilities::GetAngularProjection(SolverSettings, JointSettings);
		SwingTwistAngleTolerance = SolverSettings.SwingTwistAngleTolerance;
		bEnableTwistLimits = SolverSettings.bEnableTwistLimits;
		bEnableSwingLimits = SolverSettings.bEnableSwingLimits;
		bEnableDrives = SolverSettings.bEnableDrives;
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


	void FJointSolverGaussSeidel::ApplyConstraints(
		const FReal Dt,
		const FPBDJointSettings& JointSettings)
	{
		ApplyRotationConstraints(Dt, JointSettings);
		ApplyPositionConstraints(Dt, JointSettings);
	}


	void FJointSolverGaussSeidel::ApplyDrives(
		const FReal Dt,
		const FPBDJointSettings& JointSettings)
	{
		// @todo(ccaulfield): linear and velocity drives
		ApplyRotationDrives(Dt, JointSettings);
	}

	void FJointSolverGaussSeidel::ApplyProjections(
		const FReal Dt,
		const FPBDJointSettings& JointSettings)
	{
		ApplyPositionProjection(Dt, JointSettings);
	}


	void FJointSolverGaussSeidel::ApplyRotationConstraints(
		const FReal Dt,
		const FPBDJointSettings& JointSettings)
	{
		EJointMotionType TwistMotion = JointSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Twist];
		EJointMotionType Swing1Motion = JointSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing1];
		EJointMotionType Swing2Motion = JointSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing2];

		// Apply twist constraint
		if (bEnableTwistLimits)
		{
			if (TwistMotion != EJointMotionType::Free)
			{
				ApplyTwistConstraint(Dt, JointSettings);
			}
		}

		// Apply swing constraints
		if (bEnableSwingLimits)
		{
			if ((Swing1Motion == EJointMotionType::Limited) && (Swing2Motion == EJointMotionType::Limited))
			{
				// Swing Cone
				ApplyConeConstraint(Dt, JointSettings);
			}
			else
			{
				if (Swing1Motion != EJointMotionType::Free)
				{
					// Swing Arc/Lock
					ApplySwingConstraint(Dt, JointSettings, EJointAngularConstraintIndex::Swing1, EJointAngularAxisIndex::Swing1);
				}
				if (Swing2Motion != EJointMotionType::Free)
				{
					// Swing Arc/Lock
					ApplySwingConstraint(Dt, JointSettings, EJointAngularConstraintIndex::Swing2, EJointAngularAxisIndex::Swing2);
				}
			}
		}
	}


	void FJointSolverGaussSeidel::ApplyRotationDrives(
		const FReal Dt,
		const FPBDJointSettings& JointSettings)
	{
		EJointMotionType TwistMotion = JointSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Twist];
		EJointMotionType Swing1Motion = JointSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing1];
		EJointMotionType Swing2Motion = JointSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing2];

		if (bEnableDrives)
		{
			bool bTwistLocked = TwistMotion == EJointMotionType::Locked;
			bool bSwing1Locked = Swing1Motion == EJointMotionType::Locked;
			bool bSwing2Locked = Swing2Motion == EJointMotionType::Locked;

			// No SLerp drive if we have a locked rotation (it will be grayed out in the editor in this case, but could still have been set before the rotation was locked)
			if (JointSettings.Motion.bAngularSLerpDriveEnabled && !bTwistLocked && !bSwing1Locked && !bSwing2Locked)
			{
				ApplySLerpDrive(Dt, JointSettings);
			}

			if (JointSettings.Motion.bAngularTwistDriveEnabled && !bTwistLocked)
			{
				ApplyTwistDrive(Dt, JointSettings);
			}

			if (JointSettings.Motion.bAngularSwingDriveEnabled && !bSwing1Locked && !bSwing2Locked)
			{
				ApplyConeDrive(Dt, JointSettings);
			}
			else if (JointSettings.Motion.bAngularSwingDriveEnabled && !bSwing1Locked)
			{
				ApplySwingDrive(Dt, JointSettings, EJointAngularConstraintIndex::Swing1, EJointAngularAxisIndex::Swing1);
			}
			else if (JointSettings.Motion.bAngularSwingDriveEnabled && !bSwing2Locked)
			{
				ApplySwingDrive(Dt, JointSettings, EJointAngularConstraintIndex::Swing2, EJointAngularAxisIndex::Swing2);
			}
		}
	}


	void FJointSolverGaussSeidel::ApplyPositionConstraints(
		const FReal Dt,
		const FPBDJointSettings& JointSettings)
	{
		const TVector<EJointMotionType, 3>& LinearMotion = JointSettings.Motion.LinearMotionTypes;
		if ((LinearMotion[0] != EJointMotionType::Free) || (LinearMotion[1] != EJointMotionType::Free) || (LinearMotion[2] != EJointMotionType::Free))
		{
			ApplyPositionConstraint(Dt, JointSettings);
		}
	}


	void FJointSolverGaussSeidel::ApplyPositionDelta(
		const FReal Stiffness,
		const FVec3& DP0,
		const FVec3& DP1)
	{
		Ps[0] = Ps[0] + Stiffness * DP0;
		Ps[1] = Ps[1] + Stiffness * DP1;
	}


	void FJointSolverGaussSeidel::ApplyRotationDelta(
		const FReal Stiffness,
		const FVec3& DR0,
		const FVec3& DR1)
	{
		const FRotation3 DQ0 = (FRotation3::FromElements(Stiffness * DR0, 0) * Qs[0]) * (FReal)0.5;
		const FRotation3 DQ1 = (FRotation3::FromElements(Stiffness * DR1, 0) * Qs[1]) * (FReal)0.5;
		Qs[0] = (Qs[0] + DQ0).GetNormalized();
		Qs[1] = (Qs[1] + DQ1).GetNormalized();
		Qs[1].EnforceShortestArcWith(Qs[0]);
	}


	void FJointSolverGaussSeidel::ApplyRotationDelta(
		const FReal Stiffness,
		const FVec3& Axis0,
		const FVec3& Axis1,
		const FReal Angle)
	{
		const FMatrix33 InvI0 = Utilities::ComputeWorldSpaceInertia(Qs[0], InvILs[0]);
		const FMatrix33 InvI1 = Utilities::ComputeWorldSpaceInertia(Qs[1], InvILs[1]);

		const FReal M0 = FVec3::DotProduct(Axis0, Utilities::Multiply(InvI0, Axis0));
		const FReal M1 = FVec3::DotProduct(Axis1, Utilities::Multiply(InvI1, Axis1));

		//const FVec3 DR0 = Utilities::Multiply(InvI0, Axis0) * (Angle / (M0 + M1));
		//const FVec3 DR1 = Utilities::Multiply(InvI1, Axis1) * -(Angle / (M0 + M1));
		const FVec3 DR0 = Axis0 * (Angle * M0 / (M0 + M1));
		const FVec3 DR1 = Axis1 * -(Angle * M1 / (M0 + M1));

		ApplyRotationDelta(Stiffness, DR0, DR1);
	}

	// See "XPBD: Position-Based Simulation of Compliant Constrained Dynamics"
	void FJointSolverGaussSeidel::ApplyDriveRotationDelta(
		const FReal Dt,
		const FReal Stiffness,
		const FReal Damping,
		const FVec3& Axis0,
		const FVec3& Axis1,
		const FReal Angle,
		FReal& Lambda)
	{
		const FMatrix33 InvI0 = Utilities::ComputeWorldSpaceInertia(Qs[0], InvILs[0]);
		const FMatrix33 InvI1 = Utilities::ComputeWorldSpaceInertia(Qs[1], InvILs[1]);

		const FReal IM0 = FVec3::DotProduct(Axis0, Utilities::Multiply(InvI0, Axis0));
		const FReal IM1 = FVec3::DotProduct(Axis1, Utilities::Multiply(InvI1, Axis1));
		const FReal IM = (IM0 + IM1);

		FReal JV = 0;
		if (Damping > KINDA_SMALL_NUMBER)
		{
			const FVec3 W0 = FRotation3::CalculateAngularVelocity(PrevQs[0], Qs[0], Dt);
			const FVec3 W1 = FRotation3::CalculateAngularVelocity(PrevQs[1], Qs[1], Dt);
			JV = FVec3::DotProduct(Axis0, W0) - FVec3::DotProduct(Axis1, W1);
		}

		//const FReal MassScale = (FReal)1;	// For force springs. Make this an option...
		const FReal MassScale = IM;			// For acceleration springs. Make this an option...
		FReal DLambda = 0;
		if (Stiffness > KINDA_SMALL_NUMBER)
		{
			const FReal Alpha = MassScale / (Stiffness * Dt * Dt);
			const FReal AlphaBeta = Damping / Stiffness;
			const FReal DLambdaNumerator = (Angle - Alpha * Lambda - AlphaBeta * JV);
			const FReal DLambdaDenominator = (((FReal)1 + AlphaBeta / Dt) * IM + Alpha);
			DLambda = DLambdaNumerator / DLambdaDenominator;
		}
		else
		{
			const FReal Beta = Damping / (MassScale * Dt * Dt);
			const FReal DLambdaNumerator = -Beta * JV;
			const FReal DLambdaDenominator = IM;
			DLambda = DLambdaNumerator / DLambdaDenominator;
		}

		//const FVec3 DR0 = Utilities::Multiply(InvI0, Axis0) * DLambda;
		//const FVec3 DR1 = Utilities::Multiply(InvI1, Axis1) * -DLambda;
		const FVec3 DR0 = Axis0 * (DLambda * IM0);
		const FVec3 DR1 = Axis1 * -(DLambda * IM1);

		const FRotation3 DQ0 = (FRotation3::FromElements(DR0, 0) * Qs[0]) * (FReal)0.5;
		const FRotation3 DQ1 = (FRotation3::FromElements(DR1, 0) * Qs[1]) * (FReal)0.5;
		Qs[0] = (Qs[0] + DQ0).GetNormalized();
		Qs[1] = (Qs[1] + DQ1).GetNormalized();
		Qs[1].EnforceShortestArcWith(Qs[0]);
		Lambda += DLambda;
	}

	void FJointSolverGaussSeidel::ApplyVelocityDelta(
		const FReal Stiffness,
		const FVec3& DV0,
		const FVec3& DW0,
		const FVec3& DV1,
		const FVec3& DW1)
	{
		Vs[0] = Vs[0] + Stiffness * DV0;
		Ws[0] = Ws[0] + Stiffness * DW0;
		Vs[1] = Vs[1] + Stiffness * DV1;
		Ws[1] = Ws[1] + Stiffness * DW1;
	}


	void FJointSolverGaussSeidel::ApplyTwistConstraint(
		const FReal Dt,
		const FPBDJointSettings& JointSettings)
	{
		// Decompose rotation of body 1 relative to body 0 into swing and twist rotations, assuming twist is X axis
		FRotation3 R01Twist, R01Swing;
		FPBDJointUtilities::DecomposeSwingTwistLocal(Rs[0], Rs[1], R01Swing, R01Twist);

		FVec3 TwistAxis01 = FJointConstants::TwistAxis();
		FReal TwistAngle = R01Twist.GetAngle();
		if (TwistAngle > PI)
		{
			TwistAngle = TwistAngle - (FReal)2 * PI;
		}
		if (R01Twist.X < 0)
		{
			TwistAngle = -TwistAngle;
		}

		const FVec3 TwistAxis0 = Rs[0] * TwistAxis01;
		const FVec3 TwistAxis1 = Rs[1] * TwistAxis01;
		FReal TwistAngleMax = FLT_MAX;
		if (JointSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Twist] == EJointMotionType::Limited)
		{
			TwistAngleMax = JointSettings.Motion.AngularLimits[(int32)EJointAngularConstraintIndex::Twist];
		}
		else if (JointSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Twist] == EJointMotionType::Locked)
		{
			TwistAngleMax = 0;
		}

		// Calculate the twist correction to apply to each body
		FReal DTwistAngle = 0;
		if (TwistAngle > TwistAngleMax)
		{
			DTwistAngle = TwistAngle - TwistAngleMax;
		}
		else if (TwistAngle < -TwistAngleMax)
		{
			DTwistAngle = TwistAngle + TwistAngleMax;
		}

		// Apply twist correction
		ApplyRotationDelta(TwistStiffness, TwistAxis0, TwistAxis1, DTwistAngle);
		UpdateDerivedState();
	}


	void FJointSolverGaussSeidel::ApplyTwistDrive(
		const FReal Dt,
		const FPBDJointSettings& JointSettings)
	{
		// Decompose rotation of body 1 relative to body 0 into swing and twist rotations, assuming twist is X axis
		FRotation3 R01Twist, R01Swing;
		FPBDJointUtilities::DecomposeSwingTwistLocal(Rs[0], Rs[1], R01Swing, R01Twist);

		FVec3 TwistAxis01 = FJointConstants::TwistAxis();
		FReal TwistAngle = R01Twist.GetAngle();
		if (TwistAngle > PI)
		{
			TwistAngle = TwistAngle - (FReal)2 * PI;
		}
		if (R01Twist.X < 0)
		{
			TwistAngle = -TwistAngle;
		}

		const FVec3 TwistAxis0 = Rs[0] * TwistAxis01;
		const FVec3 TwistAxis1 = Rs[1] * TwistAxis01;
		const FReal TwistAngleTarget = JointSettings.Motion.AngularDriveTargetAngles[(int32)EJointAngularConstraintIndex::Twist];
		const FReal DTwistAngle = TwistAngle - TwistAngleTarget;

		// Apply twist correction
		ApplyDriveRotationDelta(Dt, AngularDriveStiffness, AngularDriveDamping, TwistAxis0, TwistAxis1, DTwistAngle, TwistLambda);
		UpdateDerivedState();
	}


	void FJointSolverGaussSeidel::ApplyConeConstraint(
		const FReal Dt,
		const FPBDJointSettings& JointSettings)
	{
		// Decompose rotation of body 1 relative to body 0 into swing and twist rotations, assuming twist is X axis
		FRotation3 R01Twist, R01Swing;
		FPBDJointUtilities::DecomposeSwingTwistLocal(Rs[0], Rs[1], R01Swing, R01Twist);

		FVec3 SwingAxis01;
		FReal SwingAngle;
		R01Swing.ToAxisAndAngleSafe(SwingAxis01, SwingAngle, FJointConstants::Swing1Axis(), SwingTwistAngleTolerance);
		if (SwingAngle > PI)
		{
			SwingAngle = SwingAngle - (FReal)2 * PI;
		}

		const FVec3 SwingAxis = Rs[0] * SwingAxis01;

		// Calculate swing limit for the current swing axis
		const FReal Swing1Limit = JointSettings.Motion.AngularLimits[(int32)EJointAngularConstraintIndex::Swing1];
		const FReal Swing2Limit = JointSettings.Motion.AngularLimits[(int32)EJointAngularConstraintIndex::Swing2];

		// Circular swing limit
		FReal SwingAngleMax = Swing1Limit;

		// Elliptical swing limit
		// @todo(ccaulfield): do elliptical constraints properly (axis is still for circular limit)
		if (!FMath::IsNearlyEqual(Swing1Limit, Swing2Limit, KINDA_SMALL_NUMBER))
		{
			// Map swing axis to ellipse and calculate limit for this swing axis
			const FReal DotSwing1 = FMath::Abs(FVec3::DotProduct(SwingAxis01, FJointConstants::Swing1Axis()));
			const FReal DotSwing2 = FMath::Abs(FVec3::DotProduct(SwingAxis01, FJointConstants::Swing2Axis()));
			SwingAngleMax = FMath::Sqrt(Swing1Limit * DotSwing1 * Swing1Limit * DotSwing1 + Swing2Limit * DotSwing2 * Swing2Limit * DotSwing2);
		}

		// Calculate swing error we need to correct
		FReal DSwingAngle = 0;
		if (SwingAngle > SwingAngleMax)
		{
			DSwingAngle = SwingAngle - SwingAngleMax;
		}
		else if (SwingAngle < -SwingAngleMax)
		{
			DSwingAngle = SwingAngle + SwingAngleMax;
		}

		// Apply swing correction
		ApplyRotationDelta(SwingStiffness, SwingAxis, SwingAxis, DSwingAngle);
		UpdateDerivedState();
	}


	void FJointSolverGaussSeidel::ApplyConeDrive(
		const FReal Dt,
		const FPBDJointSettings& JointSettings)
	{
		// Decompose rotation of body 1 relative to body 0 into swing and twist rotations, assuming twist is X axis
		FRotation3 R01Twist, R01Swing;
		FPBDJointUtilities::DecomposeSwingTwistLocal(Rs[0], Rs[1], R01Swing, R01Twist);

		FVec3 SwingAxis01;
		FReal SwingAngle;
		R01Swing.ToAxisAndAngleSafe(SwingAxis01, SwingAngle, FJointConstants::Swing1Axis(), SwingTwistAngleTolerance);
		if (SwingAngle > PI)
		{
			SwingAngle = SwingAngle - (FReal)2 * PI;
		}

		const FVec3 SwingAxis = Rs[0] * SwingAxis01;

		// Circular swing target (max of Swing1, Swing2 targets)
		// @todo(ccaulfield): what should cone target really do?
		FReal Swing1Target = JointSettings.Motion.AngularDriveTargetAngles[(int32)EJointAngularConstraintIndex::Swing1];
		FReal Swing2Target = JointSettings.Motion.AngularDriveTargetAngles[(int32)EJointAngularConstraintIndex::Swing2];
		FReal SwingAngleTarget = FMath::Max(Swing1Target, Swing2Target);
		FReal DSwingAngle = SwingAngle - SwingAngleTarget;

		ApplyDriveRotationDelta(Dt, AngularDriveStiffness, AngularDriveDamping, SwingAxis, SwingAxis, DSwingAngle, SwingLambda);
		UpdateDerivedState();
	}


	void FJointSolverGaussSeidel::ApplySwingConstraint(
		const FReal Dt,
		const FPBDJointSettings& JointSettings,
		const EJointAngularConstraintIndex SwingConstraintIndex,
		const EJointAngularAxisIndex SwingAxisIndex)
	{
		// @todo(ccaulfield): this can be made much simpler I think...swing axis should just be Body0's swing axis

		// Decompose rotation of body 1 relative to body 0 into swing and twist rotations, assuming twist is X axis
		FRotation3 R01Twist, R01Swing;
		FPBDJointUtilities::DecomposeSwingTwistLocal(Rs[0], Rs[1], R01Swing, R01Twist);

		FVec3 TwistAxis01 = FJointConstants::TwistAxis();
		FReal TwistAngle = R01Twist.GetAngle();
		if (TwistAngle > PI)
		{
			TwistAngle = TwistAngle - (FReal)2 * PI;
		}
		if (R01Twist.X < 0)
		{
			TwistAngle = -TwistAngle;
		}
		const FVec3 TwistAxis = Rs[0] * TwistAxis01;

		const FRotation3 R1NoTwist = Rs[1] * R01Twist.Inverse();
		const FMatrix33 Axes0 = Rs[0].ToMatrix();
		const FMatrix33 Axes1 = R1NoTwist.ToMatrix();
		FVec3 SwingCross = FVec3::CrossProduct(Axes0.GetAxis((int32)SwingAxisIndex), Axes1.GetAxis((int32)SwingAxisIndex));
		SwingCross = SwingCross - FVec3::DotProduct(TwistAxis, SwingCross) * TwistAxis;
		const FReal SwingCrossLen = SwingCross.Size();
		if (SwingCrossLen > KINDA_SMALL_NUMBER)
		{
			const FVec3 SwingAxis = SwingCross / SwingCrossLen;

			FReal SwingAngle = FMath::Asin(FMath::Clamp(SwingCrossLen, (FReal)0, (FReal)1));
			const FReal SwingDot = FVec3::DotProduct(Axes0.GetAxis((int32)SwingAxisIndex), Axes1.GetAxis((int32)SwingAxisIndex));
			if (SwingDot < (FReal)0)
			{
				SwingAngle = (FReal)PI - SwingAngle;
			}

			FReal SwingAngleMax = FLT_MAX;
			if (JointSettings.Motion.AngularMotionTypes[(int32)SwingConstraintIndex] == EJointMotionType::Limited)
			{
				FReal SwingLimit = JointSettings.Motion.AngularLimits[(int32)SwingConstraintIndex];
				SwingAngleMax = SwingLimit;
			}
			else if (JointSettings.Motion.AngularMotionTypes[(int32)SwingConstraintIndex] == EJointMotionType::Locked)
			{
				SwingAngleMax = 0;
			}

			// Calculate swing error we need to correct
			FReal DSwingAngle = 0;
			if (SwingAngle > SwingAngleMax)
			{
				DSwingAngle = SwingAngle - SwingAngleMax;
			}
			else if (SwingAngle < -SwingAngleMax)
			{
				DSwingAngle = SwingAngle + SwingAngleMax;
			}

			// Apply swing correction
			ApplyRotationDelta(SwingStiffness, SwingAxis, SwingAxis, DSwingAngle);
			UpdateDerivedState();
		}
	}


	void FJointSolverGaussSeidel::ApplySwingDrive(
		const FReal Dt,
		const FPBDJointSettings& JointSettings,
		const EJointAngularConstraintIndex SwingConstraintIndex,
		const EJointAngularAxisIndex SwingAxisIndex)
	{
		// @todo(ccaulfield): implement swing drive
	}


	void FJointSolverGaussSeidel::ApplySLerpDrive(
		const FReal Dt,
		const FPBDJointSettings& JointSettings)
	{
		// Calculate the rotation we need to apply to resolve the rotation delta
		const FRotation3 TargetR1 = Rs[0] * JointSettings.Motion.AngularDriveTarget;
		const FRotation3 DR1 = TargetR1 * Rs[1].Inverse();

		FVec3 SLerpAxis;
		FReal SLerpAngle;
		if (DR1.ToAxisAndAngleSafe(SLerpAxis, SLerpAngle, FVec3(1, 0, 0)))
		{
			if (SLerpAngle > (FReal)PI)
			{
				SLerpAngle = SLerpAngle - (FReal)2 * PI;
			}
			ApplyDriveRotationDelta(Dt, AngularDriveStiffness, AngularDriveDamping, SLerpAxis, SLerpAxis, -SLerpAngle, SwingLambda);
		}
	}


	void FJointSolverGaussSeidel::ApplyPositionConstraint(
		const FReal Dt,
		const FPBDJointSettings& JointSettings)
	{
		// @todo(ccaulfield): we should really be calculating axes based on joint config, rather than fixing the error components
		// Calculate constraint error
		const FVec3 CX = FPBDJointUtilities::GetLimitedPositionError(JointSettings, Rs[0], Xs[1] - Xs[0]);

		const FMatrix33 InvI0 = Utilities::ComputeWorldSpaceInertia(Qs[0], InvILs[0]);
		const FMatrix33 InvI1 = Utilities::ComputeWorldSpaceInertia(Qs[1], InvILs[1]);

		// Calculate constraint correction
		FMatrix33 M0 = FMatrix33(0, 0, 0);
		FMatrix33 M1 = FMatrix33(0, 0, 0);
		if (InvMs[0] > 0)
		{
			M0 = Utilities::ComputeJointFactorMatrix(Xs[0] - Ps[0], InvI0, InvMs[0]);
		}
		if (InvMs[1] > 0)
		{
			M1 = Utilities::ComputeJointFactorMatrix(Xs[1] - Ps[1], InvI1, InvMs[1]);
		}
		const FMatrix33 MI = (M0 + M1).Inverse();
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


	void FJointSolverGaussSeidel::ApplyPositionProjection(
		const FReal Dt,
		const FPBDJointSettings& JointSettings)
	{
		// Apply a position correction with the parent body set to infinite mass, then correct the velocity.
		FVec3 CX = FPBDJointUtilities::GetLimitedPositionError(JointSettings, Rs[0], Xs[1] - Xs[0]);
		const FReal CXLen = CX.Size();
		if (CXLen > KINDA_SMALL_NUMBER)
		{
			const FVec3 CXDir = CX / CXLen;
			const FVec3 V0 = Vs[0] + FVec3::CrossProduct(Ws[0], Xs[0] - Ps[0]);
			const FVec3 V1 = Vs[1] + FVec3::CrossProduct(Ws[1], Xs[1] - Ps[1]);
			FVec3 CV = FVec3::DotProduct(V1 - V0, CXDir) * CXDir;

			FMatrix33 InvI1 = Utilities::ComputeWorldSpaceInertia(Qs[1], InvILs[1]);
			FMatrix33 M1 = Utilities::ComputeJointFactorMatrix(Xs[1] - Ps[1], InvI1, InvMs[1]);
			FMatrix33 MI = M1.Inverse();

			const FVec3 DX = Utilities::Multiply(MI, CX);
			const FVec3 DV = Utilities::Multiply(MI, CV);

			const FVec3 DP1 = -InvMs[1] * DX;
			const FVec3 DR1 = Utilities::Multiply(InvI1, FVec3::CrossProduct(Xs[1] - Ps[1], -DX));
			const FVec3 DV1 = -InvMs[1] * DV;
			const FVec3 DW1 = Utilities::Multiply(InvI1, FVec3::CrossProduct(Xs[1] - Ps[1], -DV));

			ApplyPositionDelta(LinearProjection, FVec3(0), DP1);
			ApplyRotationDelta(LinearProjection, FVec3(0), DR1);
			ApplyVelocityDelta(LinearProjection, FVec3(0), FVec3(0), DV1, DW1);
			UpdateDerivedState();
		}
	}
}
