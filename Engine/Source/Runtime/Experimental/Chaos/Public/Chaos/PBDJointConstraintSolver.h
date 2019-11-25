// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Chaos/DenseMatrix.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/PBDJointConstraintTypes.h"
#include "Chaos/PBDJointConstraintUtilities.h"
#include "Chaos/Utilities.h"

namespace Chaos
{
	class FJointConstraintSolver
	{
	public:
		using FDenseMatrix66 = TDenseMatrix<6 * 6>;
		using FDenseMatrix61 = TDenseMatrix<6 * 1>;

		void InitConstraints(
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

			UpdateConstraints(Dt, SolverSettings, JointSettings, P0, Q0, P1, Q1);
		}

		void ApplyConstraints(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const FReal Stiffness)
		{
			// @todo(ccaulfield): Custom mass-matrix multiplication methods

			// For N constraints
			// Constraint error: C(Nx1)
			// Jacobian : J(Nx6)
			FDenseMatrix61 C;
			FDenseMatrix66 J0, J1;
			BuildJacobianAndResidual(SolverSettings, JointSettings, J0, J1, C);

			// InvM(6x6) = inverse mass matrix
			FDenseMatrix66 InvM0 = FDenseMatrix66::Make(6, 6, 0);
			InvM0.SetDiagonalAt(0, 3, InvMs[0]);
			InvM0.SetBlockAt(3, 3, Utilities::ComputeWorldSpaceInertia(Qs[0], InvILs[0]));
			FDenseMatrix66 InvM1 = FDenseMatrix66::Make(6, 6, 0);
			InvM1.SetDiagonalAt(0, 3, InvMs[1]);
			InvM1.SetBlockAt(3, 3, Utilities::ComputeWorldSpaceInertia(Qs[1], InvILs[1]));

			// Joint-space mass: F(NxN) = J.IM.Jt
			// Joint-space correction: L(Nx1) = (1/F).C
			FDenseMatrix66 F0 = FDenseMatrix66::MultiplyAB(J0, FDenseMatrix66::MultiplyABt(InvM0, J0));
			FDenseMatrix66 F1 = FDenseMatrix66::MultiplyAB(J1, FDenseMatrix66::MultiplyABt(InvM1, J1));
			FDenseMatrix66 F = FDenseMatrix66::Add(F0, F1);
			FDenseMatrix61 L;
			if (FDenseMatrixSolver::SolvePositiveDefinite(F, C, L))
			{
				// World-space correction: D(6x1) = IM.Jt.L
				FDenseMatrix61 D0 = FDenseMatrix61::MultiplyAB(InvM0, FDenseMatrix61::MultiplyAtB(J0, L));
				FDenseMatrix61 D1 = FDenseMatrix61::MultiplyAB(InvM1, FDenseMatrix61::MultiplyAtB(J1, L));

				// Extract world-space position correction
				const FVec3 DP0 = FVec3(D0.At(0, 0), D0.At(1, 0), D0.At(2, 0));
				const FVec3 DP1 = FVec3(D1.At(0, 0), D1.At(1, 0), D1.At(2, 0));
				const FVec3 P0 = Ps[0] + Stiffness * DP0;
				const FVec3 P1 = Ps[1] + Stiffness * DP1;

				// Extract world-space rotation correction
				const FVec3 DR0 = FVec3(D0.At(3, 0), D0.At(4, 0), D0.At(5, 0));
				const FVec3 DR1 = FVec3(D1.At(3, 0), D1.At(4, 0), D1.At(5, 0));
				const FRotation3 DQ0 = (FRotation3::FromElements(Stiffness * DR0, 0) * Qs[0]) * (FReal)0.5;
				const FRotation3 DQ1 = (FRotation3::FromElements(Stiffness * DR1, 0) * Qs[1]) * (FReal)0.5;
				const FRotation3 Q0 = (Qs[0] + DQ0).GetNormalized();
				const FRotation3 Q1 = (Qs[1] + DQ1).GetNormalized();

				// Apply corrections
				UpdateConstraints(Dt, SolverSettings, JointSettings, P0, Q0, P1, Q1);
			}
		}

		FORCEINLINE const FVec3& GetP(const int32 Index) const
		{
			checkSlow(Index < 2);
			return Ps[Index];
		}

		FORCEINLINE const FRotation3& GetQ(const int32 Index) const
		{
			checkSlow(Index < 2);
			return Qs[Index];
		}

	private:

		void UpdateConstraints(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const FVec3& P0,
			const FRotation3& Q0,
			const FVec3& P1,
			const FRotation3& Q1)
		{
			Ps[0] = P0;
			Ps[1] = P1;
			Qs[0] = Q0;
			Qs[1] = Q1;
			Qs[1].EnforceShortestArcWith(Q0);

			Xs[0] = Ps[0] + Qs[0] * XLs[0].GetTranslation();
			Xs[1] = Ps[1] + Qs[1] * XLs[1].GetTranslation();
			Rs[0] = Qs[0] * XLs[0].GetRotation();
			Rs[1] = Qs[1] * XLs[1].GetRotation();
		}

		// 3 constraints along principle axes
		void AddLinearConstraints_Point(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings)
		{
			const FVec3 ConstraintSeparation = Xs[1] - Xs[0];

			NumLinearConstraints = 3;
			LinearConstraintAxes[0] = FVec3(1, 0, 0);
			LinearConstraintAxes[1] = FVec3(0, 1, 0);
			LinearConstraintAxes[2] = FVec3(0, 0, 1);
			LinearConstraintDistances[0] = ConstraintSeparation[0];
			LinearConstraintDistances[1] = ConstraintSeparation[1];
			LinearConstraintDistances[2] = ConstraintSeparation[2];
			LinearConstraintErrors[0] = ConstraintSeparation[0];
			LinearConstraintErrors[1] = ConstraintSeparation[1];
			LinearConstraintErrors[2] = ConstraintSeparation[2];
		}

		// up to 1 constraint limiting distance
		void AddLinearConstraints_Sphere(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings)
		{
			NumLinearConstraints = 0;

			const FReal Limit = JointSettings.Motion.LinearLimit;
			const FVec3 ConstraintSeparation = Xs[1] - Xs[0];
			const FReal ConstraintSeparationLen = ConstraintSeparation.Size();

			bool bConstraintActive = (ConstraintSeparationLen >= FMath::Max(Limit, KINDA_SMALL_NUMBER));
			if (bConstraintActive)
			{
				const FVec3 Axis = ConstraintSeparation / ConstraintSeparationLen;

				NumLinearConstraints = 1;
				LinearConstraintAxes[0] = Axis;
				LinearConstraintDistances[0] = ConstraintSeparationLen;
				LinearConstraintErrors[0] = ConstraintSeparationLen - Limit;
			}
		}

		// up to 2 constraint: 1 limiting distance along the axis and another limiting lateral distance from the axis
		void AddLinearConstraints_Cylinder(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const EJointMotionType AxisMotion,
			const FVec3& Axis)
		{
			const FVec3 ConstraintSeparation = Xs[1] - Xs[0];

			// Axial Constraint
			const FReal ConstraintDistanceAxial = FVec3::DotProduct(ConstraintSeparation, Axis);
			bool bAxisConstraintActive = (AxisMotion != EJointMotionType::Free);
			if (bAxisConstraintActive)
			{
				int32 ConstraintIndex = NumLinearConstraints++;
				LinearConstraintAxes[ConstraintIndex] = Axis;
				LinearConstraintDistances[ConstraintIndex] = ConstraintDistanceAxial;
				LinearConstraintErrors[ConstraintIndex] = ConstraintDistanceAxial;
			}

			// Radial Constraint
			const FVec3 ConstraintSeparationRadial = ConstraintSeparation - ConstraintDistanceAxial * Axis;
			const FReal ConstraintDistanceRadial = ConstraintSeparationRadial.Size();
			const FReal RadialLimit = JointSettings.Motion.LinearLimit;
			bool bRadialConstraintActive = (ConstraintDistanceRadial >= RadialLimit);
			if (bRadialConstraintActive)
			{
				int32 ConstraintIndex = NumLinearConstraints++;
				LinearConstraintAxes[ConstraintIndex] = ConstraintSeparationRadial / ConstraintDistanceRadial;
				LinearConstraintDistances[ConstraintIndex] = ConstraintDistanceRadial;
				LinearConstraintErrors[ConstraintIndex] = ConstraintDistanceRadial - RadialLimit;
			}
		}

		// up to 1 constraint limiting distance along the axis (lateral motion unrestricted)
		void AddLinearConstraints_Plane(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const EJointMotionType AxisMotion,
			const FVec3& Axis)
		{
			const FReal Limit = (AxisMotion == EJointMotionType::Limited) ? JointSettings.Motion.LinearLimit : (FReal)0;
			const FVec3 ConstraintSeparation = Xs[1] - Xs[0];

			// Planar Constraint
			const FReal ConstraintDistanceAxial = FVec3::DotProduct(ConstraintSeparation, Axis);
			bool bAxisConstraintActive = (ConstraintDistanceAxial <= -Limit) || (ConstraintDistanceAxial >= Limit);
			if (bAxisConstraintActive)
			{
				int32 ConstraintIndex = NumLinearConstraints++;
				LinearConstraintAxes[ConstraintIndex] = Axis;
				LinearConstraintDistances[ConstraintIndex] = ConstraintDistanceAxial;
				LinearConstraintErrors[ConstraintIndex] = (ConstraintDistanceAxial >= Limit) ? ConstraintDistanceAxial - Limit : ConstraintDistanceAxial + Limit;
			}
		}

		// up to 1 constraint limiting rotation about twist axes
		void AddAngularConstraints_Twist(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings)
		{
			// Calculate the Twist Axis and Angle for each body
			const FRotation3 R01 = Rs[0].Inverse() * Rs[1];
			FRotation3 R01Twist, R01Swing;
			R01.ToSwingTwist(FJointConstants::TwistAxis(), R01Swing, R01Twist);
			R01Swing = R01Swing.GetNormalized();
			R01Twist = R01Twist.GetNormalized();

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

			FReal TwistAngleMax = FLT_MAX;
			if (JointSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Twist] == EJointMotionType::Limited)
			{
				TwistAngleMax = JointSettings.Motion.AngularLimits[(int32)EJointAngularConstraintIndex::Twist];
			}
			else if (JointSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Twist] == EJointMotionType::Locked)
			{
				TwistAngleMax = 0;
			}

			bool bConstraintActive = (TwistAngle <= -TwistAngleMax) || (TwistAngle >= TwistAngleMax);
			if (bConstraintActive)
			{
				int32 ConstraintIndex = NumAngularConstraints++;
				AngularConstraintAxes[ConstraintIndex][0] = Rs[0] * TwistAxis01;
				AngularConstraintAxes[ConstraintIndex][1] = Rs[1] * TwistAxis01;
				AngularConstraintDistances[ConstraintIndex] = TwistAngle;
				AngularConstraintErrors[ConstraintIndex] = (TwistAngle >= TwistAngleMax) ? TwistAngle - TwistAngleMax : TwistAngle + TwistAngleMax;
			}
		}

		// up to 1 constraint limiting angle between twist axes
		void AddAngularConstraints_Cone(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings)
		{
			// Calculate Swing axis for each body
			const FRotation3 R01 = Rs[0].Inverse() * Rs[1];
			FRotation3 R01Twist, R01Swing;
			R01.ToSwingTwist(FJointConstants::TwistAxis(), R01Swing, R01Twist);
			R01Swing = R01Swing.GetNormalized();
			R01Twist = R01Twist.GetNormalized();

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
				const int32 ConstraintIndex = NumAngularConstraints++;
				AngularConstraintAxes[ConstraintIndex][0] = Rs[0] * SwingAxis01;
				AngularConstraintAxes[ConstraintIndex][1] = AngularConstraintAxes[ConstraintIndex][0];
				AngularConstraintDistances[ConstraintIndex] = SwingAngle;
				AngularConstraintErrors[ConstraintIndex] = (SwingAngle >= SwingAngleMax) ? SwingAngle - SwingAngleMax : SwingAngle + SwingAngleMax;
			}
		}

		// up to 1 constraint limiting rotation about swing axis (relative to body 0)
		void AddAngularConstraints_Swing(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const EJointAngularConstraintIndex SwingConstraintIndex,
			const EJointAngularAxisIndex SwingAxisIndex)
		{
			// Calculate the swing axis for each body
			const FRotation3 R01 = Rs[0].Inverse() * Rs[1];
			FRotation3 R01Twist, R01Swing;
			R01.ToSwingTwist(FJointConstants::TwistAxis(), R01Swing, R01Twist);
			R01Swing = R01Swing.GetNormalized();
			R01Twist = R01Twist.GetNormalized();

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

				bool bConstraintActive = (SwingAngle <= -SwingAngleMax) || (SwingAngle >= SwingAngleMax);
				if (bConstraintActive)
				{
					int32 ConstraintIndex = NumAngularConstraints++;

					const FVec3 SwingAxis = SwingCross / SwingCrossLen;
					AngularConstraintAxes[ConstraintIndex][0] = SwingAxis;
					AngularConstraintAxes[ConstraintIndex][1] = SwingAxis;
					AngularConstraintDistances[ConstraintIndex] = SwingAngle;
					AngularConstraintErrors[ConstraintIndex] = (SwingAngle >= SwingAngleMax) ? SwingAngle - SwingAngleMax : SwingAngle + SwingAngleMax;
				}
			}
		}

		// Add linear constraints to solver
		void AddLinearConstraints(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings)
		{
			NumLinearConstraints = 0;

			const TVector<EJointMotionType, 3>& Motion = JointSettings.Motion.LinearMotionTypes;
			if ((Motion[0] == EJointMotionType::Locked) && (Motion[1] == EJointMotionType::Locked) && (Motion[2] == EJointMotionType::Locked))
			{
				AddLinearConstraints_Point(SolverSettings, JointSettings);
			}
			else if ((Motion[0] == EJointMotionType::Limited) && (Motion[1] == EJointMotionType::Limited) && (Motion[2] == EJointMotionType::Limited))
			{
				AddLinearConstraints_Sphere(SolverSettings, JointSettings);
			}
			else if ((Motion[1] == EJointMotionType::Limited) && (Motion[2] == EJointMotionType::Limited))
			{
				// Circular Limit (X Axis)
				AddLinearConstraints_Cylinder(SolverSettings, JointSettings, Motion[0], Rs[0] * FVec3(1, 0, 0));
			}
			else if ((Motion[0] == EJointMotionType::Limited) && (Motion[2] == EJointMotionType::Limited))
			{
				// Circular Limit (Y Axis)
				AddLinearConstraints_Cylinder(SolverSettings, JointSettings, Motion[1], Rs[0] * FVec3(0, 1, 0));
			}
			else if ((Motion[0] == EJointMotionType::Limited) && (Motion[1] == EJointMotionType::Limited))
			{
				// Circular Limit (Z Axis)
				AddLinearConstraints_Cylinder(SolverSettings, JointSettings, Motion[2], Rs[0] * FVec3(0, 0, 1));
			}
			else
			{
				// Plane/Square/Cube Limits (no way to author square or cube limits, but would work if we wanted it)
				if (Motion[0] != EJointMotionType::Free)
				{
					AddLinearConstraints_Plane(SolverSettings, JointSettings, Motion[0], Rs[0] * FVec3(1, 0, 0));
				}
				if (Motion[1] != EJointMotionType::Free)
				{
					AddLinearConstraints_Plane(SolverSettings, JointSettings, Motion[1], Rs[0] * FVec3(0, 1, 0));
				}
				if (Motion[2] != EJointMotionType::Free)
				{
					AddLinearConstraints_Plane(SolverSettings, JointSettings, Motion[2], Rs[0] * FVec3(0, 0, 1));
				}
			}
		}

		// Add angular constraints to solver
		void AddAngularConstraints(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings)
		{
			NumAngularConstraints = 0;

			EJointMotionType TwistMotion = JointSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Twist];
			EJointMotionType Swing1Motion = JointSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing1];
			EJointMotionType Swing2Motion = JointSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing2];

			// Apply twist constraint
			if (SolverSettings.bEnableTwistLimits)
			{
				if (TwistMotion != EJointMotionType::Free)
				{
					AddAngularConstraints_Twist(SolverSettings, JointSettings);
				}
			}

			// Apply swing constraints
			if (SolverSettings.bEnableSwingLimits)
			{
				if ((Swing1Motion == EJointMotionType::Limited) && (Swing2Motion == EJointMotionType::Limited))
				{
					AddAngularConstraints_Cone(SolverSettings, JointSettings);
				}
				else
				{
					if (Swing1Motion != EJointMotionType::Free)
					{
						AddAngularConstraints_Swing(SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing1, EJointAngularAxisIndex::Swing1);
					}
					if (Swing2Motion != EJointMotionType::Free)
					{
						AddAngularConstraints_Swing(SolverSettings, JointSettings, EJointAngularConstraintIndex::Swing2, EJointAngularAxisIndex::Swing2);
					}
				}
			}
		}

		void BuildJacobianAndResidual(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			FDenseMatrix66& J0,
			FDenseMatrix66& J1,
			FDenseMatrix61& C)
		{
			// Calculate constraint axes and errors
			AddLinearConstraints(SolverSettings, JointSettings);
			AddAngularConstraints(SolverSettings, JointSettings);

			// The Jacobians will be some sub-set of the following rows, depending on which constraints are enabled
			// and whether the limits are currently being violated.
			//
			// J0(Nx6) = | XAxis          -XAxis x Connector0 |
			//           | YAxis          -YAxis x Connector0 |
			//           | ZAxis          -ZAxis x Connector0 |
			//           | 0              TwistAxis           |
			//           | 0              Swing1Axis          |
			//           | 0              Swing2Axis          |
			//
			// J0(Nx6) = | -XAxis         XAxis x Connector1  |
			//           | -YAxis         YAxis x Connector1  |
			//           | -ZAxis         ZAxis x Connector1  |
			//           | 0              -TwistAxis          |
			//           | 0              -Swing1Axis         |
			//           | 0              -Swing2Axis         |
			//
			const int32 NumConstraints = NumLinearConstraints + NumAngularConstraints;
			J0.SetDimensions(NumConstraints, 6);
			J1.SetDimensions(NumConstraints, 6);
			C.SetDimensions(NumConstraints, 1);

			const FVec3 XP0 = Xs[0] - Ps[0];
			const FVec3 XP1 = Xs[1] - Ps[1];

			int32 ConstraintIndex = 0;
			for (int32 LinearConstraintIndex = 0; LinearConstraintIndex < NumLinearConstraints; ++LinearConstraintIndex, ++ConstraintIndex)
			{
				const FVec3& ConstraintAxis = LinearConstraintAxes[LinearConstraintIndex];

				J0.SetRowAt(ConstraintIndex, 0, ConstraintAxis);
				J0.SetRowAt(ConstraintIndex, 3, -FVec3::CrossProduct(ConstraintAxis, XP0));

				J1.SetRowAt(ConstraintIndex, 0, -ConstraintAxis);
				J1.SetRowAt(ConstraintIndex, 3, FVec3::CrossProduct(ConstraintAxis, XP1));

				C.At(ConstraintIndex, 0) = LinearConstraintErrors[LinearConstraintIndex];
			}

			for (int32 AngularConstraintIndex = 0; AngularConstraintIndex < NumAngularConstraints; ++AngularConstraintIndex, ++ConstraintIndex)
			{
				const FVec3& ConstraintAxis0 = AngularConstraintAxes[AngularConstraintIndex][0];
				const FVec3& ConstraintAxis1 = AngularConstraintAxes[AngularConstraintIndex][1];

				J0.SetRowAt(ConstraintIndex, 0, FVec3::ZeroVector);
				J0.SetRowAt(ConstraintIndex, 3, ConstraintAxis0);

				J1.SetRowAt(ConstraintIndex, 0, FVec3::ZeroVector);
				J1.SetRowAt(ConstraintIndex, 3, -ConstraintAxis1);

				C.At(ConstraintIndex, 0) = AngularConstraintErrors[AngularConstraintIndex];
			}
		}

		static const int32 MaxConstraintedBodies = 2;
		static const int32 MaxLinearConstraints = 3;
		static const int32 MaxAngularConstraints = 3;
		static const int32 MaxConstraints = MaxLinearConstraints + MaxAngularConstraints;

		// Joint solver state
		// @todo(ccaulfield): maybe just store the Jacobians and errors here rather than the data to construct them
		FVec3 LinearConstraintAxes[MaxConstraints];
		FReal LinearConstraintDistances[MaxConstraints];
		FReal LinearConstraintErrors[MaxConstraints];
		FVec3 AngularConstraintAxes[MaxConstraints][MaxConstraintedBodies];	// @todo(ccaulfield): per body for twist - should probably switch back to shared twist axis
		FReal AngularConstraintDistances[MaxConstraints];
		FReal AngularConstraintErrors[MaxConstraints];
		int32 NumLinearConstraints;
		int32 NumAngularConstraints;

		// Local-space constraint settings
		FRigidTransform3 XLs[MaxConstraintedBodies];	// Local-space joint connector transforms
		FMatrix33 InvILs[MaxConstraintedBodies];		// Local-space inverse inertias
		FReal InvMs[MaxConstraintedBodies];				// Inverse masses

		// World-space constraint state
		FVec3 Xs[MaxConstraintedBodies];				// World-space joint connector positions
		FRotation3 Rs[MaxConstraintedBodies];			// World-space joint connector rotations

		// World-space body state
		FVec3 Ps[MaxConstraintedBodies];				// World-space particle CoM positions
		FRotation3 Qs[MaxConstraintedBodies];			// World-space particle CoM rotations
	};

}