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

	void FJointSolverGaussSeidel::InitConstraints(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FVec3& P0,
		const FRotation3& Q0,
		const FVec3& P1,
		const FRotation3& Q1,
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

		Ps[0] = P0;
		Ps[1] = P1;
		Qs[0] = Q0;
		Qs[1] = Q1;
		Qs[1].EnforceShortestArcWith(Qs[0]);

		LinearStiffness = FPBDJointUtilities::GetLinearStiffness(SolverSettings, JointSettings);
		TwistStiffness = FPBDJointUtilities::GetTwistStiffness(SolverSettings, JointSettings);
		SwingStiffness = FPBDJointUtilities::GetSwingStiffness(SolverSettings, JointSettings);
		SwingTwistAngleTolerance = SolverSettings.SwingTwistAngleTolerance;
		bEnableTwistLimits = SolverSettings.bEnableTwistLimits;
		bEnableTwistLimits = SolverSettings.bEnableTwistLimits;

		UpdateDerivedState();
	}

	void FJointSolverGaussSeidel::ApplyConstraints(
		const FReal Dt,
		const FPBDJointSettings& JointSettings)
	{
		ApplyRotationConstraints(Dt, JointSettings);
		ApplyPositionConstraints(Dt, JointSettings);
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
		const FReal Angle0,
		const FVec3& Axis1,
		const FReal Angle1)
	{
		const FMatrix33 InvI0 = Utilities::ComputeWorldSpaceInertia(Qs[0], InvILs[0]);
		const FMatrix33 InvI1 = Utilities::ComputeWorldSpaceInertia(Qs[1], InvILs[1]);

		const FReal L = (FReal)1 / (FVec3::DotProduct(Axis0, Utilities::Multiply(InvI0, Axis0)) + FVec3::DotProduct(Axis1, Utilities::Multiply(InvI1, Axis1)));
		const FVec3 DR0 = Utilities::Multiply(InvI0, Axis0) * L * Angle0;
		const FVec3 DR1 = Utilities::Multiply(InvI1, Axis1) * L * Angle1;

		ApplyRotationDelta(Stiffness, DR0, DR1);
	}

	void FJointSolverGaussSeidel::ApplyTwistConstraint(
		const FReal Dt,
		const FPBDJointSettings& JointSettings)
	{
		// Decompose rotation of body 1 relative to body 0 into swing and twist rotations, assuming twist is X axis
		FRotation3 R01Twist, R01Swing;
		FPBDJointUtilities::DecomposeSwingTwistLocal(Rs[0], Rs[1], R01Swing, R01Twist);

		FVec3 TwistAxis01;
		FReal TwistAngle;
		R01Twist.ToAxisAndAngleSafe(TwistAxis01, TwistAngle, FJointConstants::TwistAxis(), SwingTwistAngleTolerance);
		if (TwistAngle > PI)
		{
			TwistAngle = TwistAngle - (FReal)2 * PI;
		}
		if (FVec3::DotProduct(TwistAxis01, FJointConstants::TwistAxis()) < 0)
		{
			TwistAxis01 = -TwistAxis01;
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
		const FReal DTwistAngle0 = DTwistAngle;
		const FReal DTwistAngle1 = -DTwistAngle;

		// Apply twist correction
		ApplyRotationDelta(TwistStiffness, TwistAxis0, DTwistAngle0, TwistAxis1, DTwistAngle1);
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
			SwingAngleMax = FMath::Sqrt(Swing1Limit * DotSwing2 * Swing1Limit * DotSwing2 + Swing2Limit * DotSwing1 * Swing2Limit * DotSwing1);
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
		ApplyRotationDelta(SwingStiffness, SwingAxis, DSwingAngle, SwingAxis, -DSwingAngle);
		UpdateDerivedState();
	}


	void FJointSolverGaussSeidel::ApplySwingConstraint(
		const FReal Dt,
		const FPBDJointSettings& JointSettings,
		const EJointAngularConstraintIndex SwingConstraintIndex,
		const EJointAngularAxisIndex SwingAxisIndex)
	{
		// Decompose rotation of body 1 relative to body 0 into swing and twist rotations, assuming twist is X axis
		FRotation3 R01Twist, R01Swing;
		FPBDJointUtilities::DecomposeSwingTwistLocal(Rs[0], Rs[1], R01Swing, R01Twist);

		FVec3 TwistAxis01;
		FReal TwistAngle;
		R01Twist.ToAxisAndAngleSafe(TwistAxis01, TwistAngle, FJointConstants::TwistAxis(), SwingTwistAngleTolerance);
		if (TwistAngle > PI)
		{
			TwistAngle = TwistAngle - (FReal)2 * PI;
		}
		if (FVec3::DotProduct(TwistAxis01, FJointConstants::TwistAxis()) < 0)
		{
			TwistAxis01 = -TwistAxis01;
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
			ApplyRotationDelta(SwingStiffness, SwingAxis, DSwingAngle, SwingAxis, -DSwingAngle);
			UpdateDerivedState();
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

}
