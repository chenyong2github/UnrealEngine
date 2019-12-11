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
	/**
	 * Calculate new positions and rotations for a pair of bodies connected by a joint.
	 *
	 * This solver treats of the 6 possible constraints (up to 3 linear and 3 angular)
	 * as a single block and resolves them in simultaneously.
	 *
	 * \see FJointSolverGaussSeidel
	 */
	class FJointSolverCholesky
	{
	public:
		static const int32 MaxConstraintedBodies = 2;

		using FDenseMatrix66 = TDenseMatrix<6 * 6>;
		using FDenseMatrix61 = TDenseMatrix<6 * 1>;

		FORCEINLINE const FVec3& GetP(const int32 Index) const
		{
			checkSlow(Index < MaxConstraintedBodies);
			return Ps[Index];
		}

		FORCEINLINE const FRotation3& GetQ(const int32 Index) const
		{
			checkSlow(Index < MaxConstraintedBodies);
			return Qs[Index];
		}

		FJointSolverCholesky();

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
			const FRigidTransform3& XL1);

		void ApplyConstraints(
			const FReal Dt,
			const FPBDJointSettings& JointSettings);

	private:

		void UpdateDerivedState();

		void AddLinearConstraints_Point(
			const FPBDJointSettings& JointSettings,
			FDenseMatrix66& J0,
			FDenseMatrix66& J1,
			FDenseMatrix61& C);

		void AddLinearConstraints_Sphere(
			const FPBDJointSettings& JointSettings,
			FDenseMatrix66& J0,
			FDenseMatrix66& J1,
			FDenseMatrix61& C);

		void AddLinearConstraints_Cylinder(
			const FPBDJointSettings& JointSettings,
			const EJointMotionType AxisMotion,
			const FVec3& Axis,
			FDenseMatrix66& J0,
			FDenseMatrix66& J1,
			FDenseMatrix61& C);

		void AddLinearConstraints_Plane(
			const FPBDJointSettings& JointSettings,
			const EJointMotionType AxisMotion,
			const FVec3& Axis,
			FDenseMatrix66& J0,
			FDenseMatrix66& J1,
			FDenseMatrix61& C);

		void AddAngularConstraints_Twist(
			const FPBDJointSettings& JointSettings,
			const FRotation3& R01Twist,
			const FRotation3& R01Swing,
			FDenseMatrix66& J0,
			FDenseMatrix66& J1,
			FDenseMatrix61& C);

		void AddAngularConstraints_Cone(
			const FPBDJointSettings& JointSettings,
			const FRotation3& R01Twist,
			const FRotation3& R01Swing,
			FDenseMatrix66& J0,
			FDenseMatrix66& J1,
			FDenseMatrix61& C);

		void AddAngularConstraints_Swing(
			const FPBDJointSettings& JointSettings,
			const EJointAngularConstraintIndex SwingConstraintIndex,
			const EJointAngularAxisIndex SwingAxisIndex,
			const FRotation3& R01Twist,
			const FRotation3& R01Swing,
			FDenseMatrix66& J0,
			FDenseMatrix66& J1,
			FDenseMatrix61& C);

		void AddLinearConstraints(
			const FPBDJointSettings& JointSettings,
			FDenseMatrix66& J0,
			FDenseMatrix66& J1,
			FDenseMatrix61& C);

		void AddAngularConstraints(
			const FPBDJointSettings& JointSettings,
			FDenseMatrix66& J0,
			FDenseMatrix66& J1,
			FDenseMatrix61& C);

		void BuildJacobianAndResidual(
			const FPBDJointSettings& JointSettings,
			FDenseMatrix66& J0,
			FDenseMatrix66& J1,
			FDenseMatrix61& C);

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

		// Settings
		FReal Stiffness;
		FReal SwingTwistAngleTolerance;
		bool bEnableTwistLimits;
		bool bEnableSwingLimits;
	};

}