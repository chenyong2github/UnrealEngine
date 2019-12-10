// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDJointConstraintSolver.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDJointConstraintUtilities.h"
#include "Chaos/Utilities.h"
#include "ChaosLog.h"
#include "ChaosStats.h"

//#pragma optimize("", off)

// Set to '1' enable solver stats (very high frequency, so usually disabled)
#define CHAOS_JOINTSOLVER_STATSENABLED 0

namespace Chaos
{
#if CHAOS_JOINTSOLVER_STATSENABLED
	DECLARE_CYCLE_STAT(TEXT("TPBDJointConstraints::Apply"), STAT_JointSolver_Apply, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("TPBDJointConstraints::ApplyPushOut"), STAT_JointSolver_Jacobian, STATGROUP_Chaos);
	#define CHAOS_JOINTSOLVER_SCOPE_CYCLE_STAT SCOPE_CYCLE_COUNTER(X)
#else
	#define CHAOS_JOINTSOLVER_SCOPE_CYCLE_STAT(X)
#endif

	void FJointConstraintSolver::DecomposeSwingTwistLocal(const FRotation3& R0, const FRotation3& R1, FRotation3& R01Twist, FRotation3& R01Swing)
	{
		const FRotation3 R01 = R0.Inverse() * R1;
		R01.ToSwingTwistX(FJointConstants::TwistAxis(), R01Swing, R01Twist);
	}


	void FJointConstraintSolver::InitConstraints(
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

		Ps[0] = P0;
		Ps[1] = P1;
		Qs[0] = Q0;
		Qs[1] = Q1;
		Qs[1].EnforceShortestArcWith(Qs[0]);

		UpdateDerivedState();
	}

	void FJointConstraintSolver::ApplyConstraints(
		const FReal Dt,
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FReal Stiffness)
	{
		CHAOS_JOINTSOLVER_SCOPE_CYCLE_STAT(STAT_JointSolver_Apply);

		// Solving for world-space position/rotation corrections D(6x1) where
		// D = [I.Jt / [J.I.Jt]].C = I.Jt.L
		// I is the inverse mass matrix, J the Jacobian, C the current constraint violation,
		// and L = [1 / [J.I.Jt]].C is the Joint-space correction.

		// For N constraints
		// Constraint error: C(Nx1)
		// Jacobian : J(Nx6)
		// The Jacobians will be some sub-set of the following rows, depending on the "active" constraints 
		// ("active" = enabled and either fixed or with limits exceeded).
		//
		// J0(Nx6) = | XAxis          -XAxis x Connector0 |
		//           | YAxis          -YAxis x Connector0 |
		//           | ZAxis          -ZAxis x Connector0 |
		//           | 0              TwistAxis           |
		//           | 0              Swing1Axis          |
		//           | 0              Swing2Axis          |
		//
		// J1(Nx6) = | -XAxis         XAxis x Connector1  |
		//           | -YAxis         YAxis x Connector1  |
		//           | -ZAxis         ZAxis x Connector1  |
		//           | 0              -TwistAxis          |
		//           | 0              -Swing1Axis         |
		//           | 0              -Swing2Axis         |
		//
		FDenseMatrix61 C;
		FDenseMatrix66 J0, J1;
		BuildJacobianAndResidual(SolverSettings, JointSettings, J0, J1, C);

		// InvM(6x6) = inverse mass matrix
		const FMassMatrix InvM0 = FMassMatrix::Make(InvMs[0], Qs[0], InvILs[0]);
		const FMassMatrix InvM1 = FMassMatrix::Make(InvMs[1], Qs[1], InvILs[1]);

		// IJt(6xN) = I(6x6).Jt(6xN)
		const FDenseMatrix66 IJt0 = FDenseMatrix66::MultiplyABt(InvM0, J0);
		const FDenseMatrix66 IJt1 = FDenseMatrix66::MultiplyABt(InvM1, J1);

		// Joint-space mass: F(NxN) = J(Nx6).I(6x6).Jt(6xN) = J(Nx6).IJt(6xN)
		// NOTE: Result is symmetric
		const FDenseMatrix66 F0 = FDenseMatrix66::MultiplyAB_Symmetric(J0, IJt0);
		const FDenseMatrix66 F = FDenseMatrix66::MultiplyBCAddA_Symmetric(F0, J1, IJt1);

		// Joint-space correction: L(Nx1) = [1/F](NxN).C(Nx1)
		FDenseMatrix61 L;
		if (FDenseMatrixSolver::SolvePositiveDefinite(F, C, L))
		{
			// World-space correction: D(6x1) = I(6x6).Jt(6xN).L(Nx1) = IJt(6xN).L(Nx1)
			const FDenseMatrix61 D0 = FDenseMatrix61::MultiplyAB(IJt0, L);
			const FDenseMatrix61 D1 = FDenseMatrix61::MultiplyAB(IJt1, L);

			// Extract world-space position correction
			Ps[0] = Ps[0] + FVec3(Stiffness * D0.At(0, 0), Stiffness * D0.At(1, 0), Stiffness * D0.At(2, 0));
			Ps[1] = Ps[1] + FVec3(Stiffness * D1.At(0, 0), Stiffness * D1.At(1, 0), Stiffness * D1.At(2, 0));

			// Extract world-space rotation correction
			const FReal HalfStiffness = (FReal)0.5 * Stiffness;
			const FRotation3 DQ0 = FRotation3::FromElements(HalfStiffness * D0.At(3, 0), HalfStiffness * D0.At(4, 0), HalfStiffness * D0.At(5, 0), 0) * Qs[0];
			const FRotation3 DQ1 = FRotation3::FromElements(HalfStiffness * D1.At(3, 0), HalfStiffness * D1.At(4, 0), HalfStiffness * D1.At(5, 0), 0) * Qs[1];
			Qs[0] = (Qs[0] + DQ0).GetNormalized();
			Qs[1] = (Qs[1] + DQ1).GetNormalized();
			Qs[1].EnforceShortestArcWith(Qs[0]);

			UpdateDerivedState();
		}
	}

	void FJointConstraintSolver::BuildJacobianAndResidual(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		FDenseMatrix66& J0,
		FDenseMatrix66& J1,
		FDenseMatrix61& C)
	{
		CHAOS_JOINTSOLVER_SCOPE_CYCLE_STAT(STAT_JointSolver_Jacobian);

		// Initialize with zero constraints
		J0.SetDimensions(0, 6);
		J1.SetDimensions(0, 6);
		C.SetDimensions(0, 1);

		AddLinearConstraints(SolverSettings, JointSettings, J0, J1, C);
		AddAngularConstraints(SolverSettings, JointSettings, J0, J1, C);
	}

	void FJointConstraintSolver::UpdateDerivedState()
	{
		Xs[0] = Ps[0] + Qs[0] * XLs[0].GetTranslation();
		Xs[1] = Ps[1] + Qs[1] * XLs[1].GetTranslation();
		Rs[0] = Qs[0] * XLs[0].GetRotation();
		Rs[1] = Qs[1] * XLs[1].GetRotation();
	}

	// 3 constraints along principle axes
	void FJointConstraintSolver::AddLinearConstraints_Point(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		FDenseMatrix66& J0,
		FDenseMatrix66& J1,
		FDenseMatrix61& C)
	{
		const int32 RowIndex = J0.NumRows();
		J0.AddRows(3);
		J1.AddRows(3);
		C.AddRows(3);

		// Cross Product
		//Result[0] = V1[1] * V2[2] - V1[2] * V2[1];
		//Result[1] = V1[2] * V2[0] - V1[0] * V2[2];
		//Result[2] = V1[0] * V2[1] - V1[1] * V2[0];

		const FVec3 XP0 = Xs[0] - Ps[0];
		J0.SetBlockAtDiagonal33(RowIndex, 0, (FReal)1, (FReal)0);
		J0.SetRowAt(RowIndex + 0, 3, (FReal)0, XP0[2], -XP0[1]);	// -(1,0,0) x XP0
		J0.SetRowAt(RowIndex + 1, 3, -XP0[2], (FReal)0, XP0[0]);	// -(0,1,0) x XP0
		J0.SetRowAt(RowIndex + 2, 3, XP0[1], -XP0[0], (FReal)0);	// -(0,0,1) x XP0

		const FVec3 XP1 = Xs[1] - Ps[1];
		J1.SetBlockAtDiagonal33(RowIndex, 0, (FReal)-1, (FReal)0);
		J1.SetRowAt(RowIndex + 0, 3, (FReal)0, -XP1[2], XP1[1]);	// (1,0,0) x XP1
		J1.SetRowAt(RowIndex + 1, 3, XP1[2], (FReal)0, -XP1[0]);	// (0,1,0) x XP1
		J1.SetRowAt(RowIndex + 2, 3, -XP1[1], XP1[0], (FReal)0);	// (0,0,1) x XP1

		const FVec3 ConstraintSeparation = Xs[1] - Xs[0];
		C.SetAt(RowIndex + 0, 0, ConstraintSeparation[0]);
		C.SetAt(RowIndex + 1, 0, ConstraintSeparation[1]);
		C.SetAt(RowIndex + 2, 0, ConstraintSeparation[2]);
	}

	// up to 1 constraint limiting distance
	void FJointConstraintSolver::AddLinearConstraints_Sphere(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		FDenseMatrix66& J0,
		FDenseMatrix66& J1,
		FDenseMatrix61& C)
	{
		const FReal Limit = JointSettings.Motion.LinearLimit;
		const FVec3 ConstraintSeparation = Xs[1] - Xs[0];
		const FReal ConstraintSeparationLen = ConstraintSeparation.Size();

		bool bConstraintActive = (ConstraintSeparationLen >= FMath::Max(Limit, KINDA_SMALL_NUMBER));
		if (bConstraintActive)
		{
			const FVec3 XP0 = Xs[0] - Ps[0];
			const FVec3 XP1 = Xs[1] - Ps[1];
			const FVec3 Axis = ConstraintSeparation / ConstraintSeparationLen;

			const int32 RowIndex = J0.NumRows();
			J0.AddRows(1);
			J1.AddRows(1);
			C.AddRows(1);

			J0.SetRowAt(RowIndex, 0, Axis);
			J0.SetRowAt(RowIndex, 3, -FVec3::CrossProduct(Axis, XP0));

			J1.SetRowAt(RowIndex, 0, -Axis);
			J1.SetRowAt(RowIndex, 3, FVec3::CrossProduct(Axis, XP1));

			C.SetAt(RowIndex, 0, ConstraintSeparationLen - Limit);
		}
	}

	// up to 2 constraint: 1 limiting distance along the axis and another limiting lateral distance from the axis
	void FJointConstraintSolver::AddLinearConstraints_Cylinder(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const EJointMotionType AxisMotion,
		const FVec3& Axis,
		FDenseMatrix66& J0,
		FDenseMatrix66& J1,
		FDenseMatrix61& C)
	{
		const FVec3 ConstraintSeparation = Xs[1] - Xs[0];
		const FVec3 XP0 = Xs[0] - Ps[0];
		const FVec3 XP1 = Xs[1] - Ps[1];

		// Axial Constraint
		const FReal ConstraintDistanceAxial = FVec3::DotProduct(ConstraintSeparation, Axis);
		bool bAxisConstraintActive = (AxisMotion != EJointMotionType::Free);
		if (bAxisConstraintActive)
		{
			const int32 RowIndex = J0.NumRows();
			J0.AddRows(1);
			J1.AddRows(1);
			C.AddRows(1);

			J0.SetRowAt(RowIndex, 0, Axis);
			J0.SetRowAt(RowIndex, 3, -FVec3::CrossProduct(Axis, XP0));

			J1.SetRowAt(RowIndex, 0, -Axis);
			J1.SetRowAt(RowIndex, 3, FVec3::CrossProduct(Axis, XP1));

			C.SetAt(RowIndex, 0, ConstraintDistanceAxial);
		}

		// Radial Constraint
		const FVec3 ConstraintSeparationRadial = ConstraintSeparation - ConstraintDistanceAxial * Axis;
		const FReal ConstraintDistanceRadial = ConstraintSeparationRadial.Size();
		const FReal RadialLimit = JointSettings.Motion.LinearLimit;
		bool bRadialConstraintActive = (ConstraintDistanceRadial >= RadialLimit);
		if (bRadialConstraintActive)
		{
			const FVec3 RadialAxis = ConstraintSeparationRadial / ConstraintDistanceRadial;

			const int32 RowIndex = J0.NumRows();
			J0.AddRows(1);
			J1.AddRows(1);
			C.AddRows(1);

			J0.SetRowAt(RowIndex, 0, RadialAxis);
			J0.SetRowAt(RowIndex, 3, -FVec3::CrossProduct(RadialAxis, XP0));

			J1.SetRowAt(RowIndex, 0, -RadialAxis);
			J1.SetRowAt(RowIndex, 3, FVec3::CrossProduct(RadialAxis, XP1));

			C.SetAt(RowIndex, 0, ConstraintDistanceRadial - RadialLimit);
		}
	}

	// up to 1 constraint limiting distance along the axis (lateral motion unrestricted)
	void FJointConstraintSolver::AddLinearConstraints_Plane(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const EJointMotionType AxisMotion,
		const FVec3& Axis,
		FDenseMatrix66& J0,
		FDenseMatrix66& J1,
		FDenseMatrix61& C)
	{
		const FReal Limit = (AxisMotion == EJointMotionType::Limited) ? JointSettings.Motion.LinearLimit : (FReal)0;
		const FVec3 ConstraintSeparation = Xs[1] - Xs[0];

		// Planar Constraint
		const FReal ConstraintDistanceAxial = FVec3::DotProduct(ConstraintSeparation, Axis);
		bool bAxisConstraintActive = (ConstraintDistanceAxial <= -Limit) || (ConstraintDistanceAxial >= Limit);
		if (bAxisConstraintActive)
		{
			const FVec3 XP0 = Xs[0] - Ps[0];
			const FVec3 XP1 = Xs[1] - Ps[1];

			const int32 RowIndex = J0.NumRows();
			J0.AddRows(1);
			J1.AddRows(1);
			C.AddRows(1);

			J0.SetRowAt(RowIndex, 0, Axis);
			J0.SetRowAt(RowIndex, 3, -FVec3::CrossProduct(Axis, XP0));

			J1.SetRowAt(RowIndex, 0, -Axis);
			J1.SetRowAt(RowIndex, 3, FVec3::CrossProduct(Axis, XP1));

			C.SetAt(RowIndex, 0, (ConstraintDistanceAxial >= Limit) ? ConstraintDistanceAxial - Limit : ConstraintDistanceAxial + Limit);
		}
	}

	// up to 1 constraint limiting rotation about twist axes
	void FJointConstraintSolver::AddAngularConstraints_Twist(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FRotation3& R01Twist,
		const FRotation3& R01Swing,
		FDenseMatrix66& J0,
		FDenseMatrix66& J1,
		FDenseMatrix61& C)
	{
		FVec3 TwistAxis01 = FJointConstants::TwistAxis();
		FReal TwistAngle = R01Twist.GetAngle();
		//R01Twist.ToAxisAndAngleSafe(TwistAxis01, TwistAngle, FJointConstants::TwistAxis(), SolverSettings.SwingTwistAngleTolerance);
		if (TwistAngle > PI)
		{
			TwistAngle = TwistAngle - (FReal)2 * PI;
		}
		//if (FVec3::DotProduct(TwistAxis01, FJointConstants::TwistAxis()) < 0)
		if (R01Twist.X < 0)
		{
			TwistAngle = -TwistAngle;
		}

		const FReal TwistAngleMax = JointSettings.Motion.AngularLimits[(int32)EJointAngularConstraintIndex::Twist];
		bool bConstraintActive = (TwistAngle <= -TwistAngleMax) || (TwistAngle >= TwistAngleMax);
		if (bConstraintActive)
		{
			const FVec3 Axis0 = Rs[0] * TwistAxis01;
			const FVec3 Axis1 = Rs[1] * TwistAxis01;
			const FVec3 XP0 = Xs[0] - Ps[0];
			const FVec3 XP1 = Xs[1] - Ps[1];

			const int32 RowIndex = J0.NumRows();
			J0.AddRows(1);
			J1.AddRows(1);
			C.AddRows(1);

			J0.SetRowAt(RowIndex + 0, 0, 0, 0, 0);
			J0.SetRowAt(RowIndex + 0, 3, Axis0);

			J1.SetRowAt(RowIndex + 0, 0, 0, 0, 0);
			J1.SetRowAt(RowIndex + 0, 3, -Axis1);

			C.SetAt(RowIndex, 0, (TwistAngle >= TwistAngleMax) ? TwistAngle - TwistAngleMax : TwistAngle + TwistAngleMax);
		}
	}

	// up to 1 constraint limiting angle between twist axes
	void FJointConstraintSolver::AddAngularConstraints_Cone(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const FRotation3& R01Twist,
		const FRotation3& R01Swing,
		FDenseMatrix66& J0,
		FDenseMatrix66& J1,
		FDenseMatrix61& C)
	{
		FVec3 SwingAxis01;
		FReal SwingAngle;
		R01Swing.ToAxisAndAngleSafe(SwingAxis01, SwingAngle, FJointConstants::Swing1Axis(), SolverSettings.SwingTwistAngleTolerance);
		if (SwingAngle > PI)
		{
			SwingAngle = SwingAngle - (FReal)2 * PI;
		}

		// Calculate swing limit for the current swing axis
		FReal SwingAngleMax = FLT_MAX;
		const FReal Swing1Limit = JointSettings.Motion.AngularLimits[(int32)EJointAngularConstraintIndex::Swing1];
		const FReal Swing2Limit = JointSettings.Motion.AngularLimits[(int32)EJointAngularConstraintIndex::Swing2];

		// Circular swing limit
		SwingAngleMax = Swing1Limit;

		// Elliptical swing limit
		if (!FMath::IsNearlyEqual(Swing1Limit, Swing2Limit, KINDA_SMALL_NUMBER))
		{
			// Map swing axis to ellipse and calculate limit for this swing axis
			const FReal DotSwing1 = FMath::Abs(FVec3::DotProduct(SwingAxis01, FJointConstants::Swing1Axis()));
			const FReal DotSwing2 = FMath::Abs(FVec3::DotProduct(SwingAxis01, FJointConstants::Swing2Axis()));
			SwingAngleMax = FMath::Sqrt(Swing1Limit * DotSwing2 * Swing1Limit * DotSwing2 + Swing2Limit * DotSwing1 * Swing2Limit * DotSwing1);
		}

		bool bConstraintActive = (SwingAngle <= -SwingAngleMax) || (SwingAngle >= SwingAngleMax);
		if (bConstraintActive)
		{
			const FVec3 XP0 = Xs[0] - Ps[0];
			const FVec3 XP1 = Xs[1] - Ps[1];
			const FVec3 Axis = Rs[0] * SwingAxis01;

			const int32 RowIndex = J0.NumRows();
			J0.AddRows(1);
			J1.AddRows(1);
			C.AddRows(1);

			J0.SetRowAt(RowIndex + 0, 0, 0, 0, 0);
			J0.SetRowAt(RowIndex + 0, 3, Axis);

			J1.SetRowAt(RowIndex + 0, 0, 0, 0, 0);
			J1.SetRowAt(RowIndex + 0, 3, -Axis);

			C.SetAt(RowIndex, 0, (SwingAngle >= SwingAngleMax) ? SwingAngle - SwingAngleMax : SwingAngle + SwingAngleMax);
		}
	}

	// up to 1 constraint limiting rotation about swing axis (relative to body 0)
	void FJointConstraintSolver::AddAngularConstraints_Swing(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		const EJointAngularConstraintIndex SwingConstraintIndex,
		const EJointAngularAxisIndex SwingAxisIndex,
		const FRotation3& R01Twist,
		const FRotation3& R01Swing,
		FDenseMatrix66& J0,
		FDenseMatrix66& J1,
		FDenseMatrix61& C)
	{
		FVec3 TwistAxis01;
		FReal TwistAngle;
		R01Twist.ToAxisAndAngleSafe(TwistAxis01, TwistAngle, FJointConstants::TwistAxis(), SolverSettings.SwingTwistAngleTolerance);
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
			FReal SwingAngle = FMath::Asin(FMath::Clamp(SwingCrossLen, (FReal)0, (FReal)1));
			const FReal SwingDot = FVec3::DotProduct(Axes0.GetAxis((int32)SwingAxisIndex), Axes1.GetAxis((int32)SwingAxisIndex));
			if (SwingDot < (FReal)0)
			{
				SwingAngle = (FReal)PI - SwingAngle;
			}

			FReal SwingAngleMax = JointSettings.Motion.AngularLimits[(int32)SwingConstraintIndex];
			bool bConstraintActive = (SwingAngle <= -SwingAngleMax) || (SwingAngle >= SwingAngleMax);
			if (bConstraintActive)
			{
				const FVec3 XP0 = Xs[0] - Ps[0];
				const FVec3 XP1 = Xs[1] - Ps[1];
				const FVec3 Axis = SwingCross / SwingCrossLen;

				const int32 RowIndex = J0.NumRows();
				J0.AddRows(1);
				J1.AddRows(1);
				C.AddRows(1);

				J0.SetRowAt(RowIndex + 0, 0, 0, 0, 0);
				J0.SetRowAt(RowIndex + 0, 3, Axis);

				J1.SetRowAt(RowIndex + 0, 0, 0, 0, 0);
				J1.SetRowAt(RowIndex + 0, 3, -Axis);

				C.SetAt(RowIndex, 0, (SwingAngle >= SwingAngleMax) ? SwingAngle - SwingAngleMax : SwingAngle + SwingAngleMax);
			}
		}
	}

	// Add linear constraints to solver
	void FJointConstraintSolver::AddLinearConstraints(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		FDenseMatrix66& J0,
		FDenseMatrix66& J1,
		FDenseMatrix61& C)
	{
		const TVector<EJointMotionType, 3>& Motion = JointSettings.Motion.LinearMotionTypes;
		if ((Motion[0] == EJointMotionType::Locked) && (Motion[1] == EJointMotionType::Locked) && (Motion[2] == EJointMotionType::Locked))
		{
			AddLinearConstraints_Point(SolverSettings, JointSettings, J0, J1, C);
		}
		else if ((Motion[0] == EJointMotionType::Limited) && (Motion[1] == EJointMotionType::Limited) && (Motion[2] == EJointMotionType::Limited))
		{
			AddLinearConstraints_Sphere(SolverSettings, JointSettings, J0, J1, C);
		}
		else if ((Motion[1] == EJointMotionType::Limited) && (Motion[2] == EJointMotionType::Limited))
		{
			// Circular Limit (X Axis)
			AddLinearConstraints_Cylinder(SolverSettings, JointSettings, Motion[0], Rs[0] * FVec3(1, 0, 0), J0, J1, C);
		}
		else if ((Motion[0] == EJointMotionType::Limited) && (Motion[2] == EJointMotionType::Limited))
		{
			// Circular Limit (Y Axis)
			AddLinearConstraints_Cylinder(SolverSettings, JointSettings, Motion[1], Rs[0] * FVec3(0, 1, 0), J0, J1, C);
		}
		else if ((Motion[0] == EJointMotionType::Limited) && (Motion[1] == EJointMotionType::Limited))
		{
			// Circular Limit (Z Axis)
			AddLinearConstraints_Cylinder(SolverSettings, JointSettings, Motion[2], Rs[0] * FVec3(0, 0, 1), J0, J1, C);
		}
		else
		{
			// Plane/Square/Cube Limits (no way to author square or cube limits, but would work if we wanted it)
			if (Motion[0] != EJointMotionType::Free)
			{
				AddLinearConstraints_Plane(SolverSettings, JointSettings, Motion[0], Rs[0] * FVec3(1, 0, 0), J0, J1, C);
			}
			if (Motion[1] != EJointMotionType::Free)
			{
				AddLinearConstraints_Plane(SolverSettings, JointSettings, Motion[1], Rs[0] * FVec3(0, 1, 0), J0, J1, C);
			}
			if (Motion[2] != EJointMotionType::Free)
			{
				AddLinearConstraints_Plane(SolverSettings, JointSettings, Motion[2], Rs[0] * FVec3(0, 0, 1), J0, J1, C);
			}
		}
	}

	// Add angular constraints to solver
	void FJointConstraintSolver::AddAngularConstraints(
		const FPBDJointSolverSettings& SolverSettings,
		const FPBDJointSettings& JointSettings,
		FDenseMatrix66& J0,
		FDenseMatrix66& J1,
		FDenseMatrix61& C)
	{
		EJointMotionType TwistMotion = JointSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Twist];
		EJointMotionType Swing1Motion = JointSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing1];
		EJointMotionType Swing2Motion = JointSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing2];

		bool bAddTwist = SolverSettings.bEnableTwistLimits && (TwistMotion != EJointMotionType::Free);
		bool bAddConeOrSwing = SolverSettings.bEnableSwingLimits && ((Swing1Motion != EJointMotionType::Free) || (Swing2Motion != EJointMotionType::Free));

		if (bAddTwist || bAddConeOrSwing)
		{
			// Calculate axes for each body
			FRotation3 R01Twist, R01Swing;
			DecomposeSwingTwistLocal(Rs[0], Rs[1], R01Twist, R01Swing);

			// Add twist constraint
			if (bAddTwist)
			{
				AddAngularConstraints_Twist(SolverSettings, JointSettings, R01Twist, R01Swing, J0, J1, C);
			}

			// Add swing constraints
			if (bAddConeOrSwing)
			{
				if ((Swing1Motion == EJointMotionType::Limited) && (Swing2Motion == EJointMotionType::Limited))
				{
					AddAngularConstraints_Cone(SolverSettings, JointSettings, R01Twist, R01Swing, J0, J1, C);
				}
				else
				{
					if (Swing1Motion != EJointMotionType::Free)
					{
						AddAngularConstraints_Swing(SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing1, EJointAngularAxisIndex::Swing1, R01Twist, R01Swing, J0, J1, C);
					}
					if (Swing2Motion != EJointMotionType::Free)
					{
						AddAngularConstraints_Swing(SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing2, EJointAngularAxisIndex::Swing2, R01Twist, R01Swing, J0, J1, C);
					}
				}
			}
		}
	}

}
