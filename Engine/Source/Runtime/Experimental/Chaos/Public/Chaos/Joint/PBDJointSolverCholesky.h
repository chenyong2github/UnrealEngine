// Copyright Epic Games, Inc. All Rights Reserved.
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
		static const int32 MaxConstrainedBodies = 2;

		using FDenseMatrix66 = TDenseMatrix<6 * 6>;
		using FDenseMatrix61 = TDenseMatrix<6 * 1>;

		FORCEINLINE const FVec3& GetP(const int32 Index) const
		{
			checkSlow(Index < MaxConstrainedBodies);
			return Ps[Index];
		}

		FORCEINLINE const FRotation3& GetQ(const int32 Index) const
		{
			checkSlow(Index < MaxConstrainedBodies);
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

		void ApplyDrives(
			const FReal Dt,
			const FPBDJointSettings& JointSettings);

	private:

		void UpdateDerivedState();

		void AddLinearRow(
			const FVec3& Axis,
			const FVec3& Connector0,
			const FVec3& Connector1,
			const FReal Error,
			FDenseMatrix66& J0,
			FDenseMatrix66& J1,
			FDenseMatrix61& C);

		void AddAngularRow(
			const FVec3& Axis0,
			const FVec3& Axis1,
			const FReal Error,
			FDenseMatrix66& J0,
			FDenseMatrix66& J1,
			FDenseMatrix61& C);

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

		void AddAngularDrive_SLerp(
			const FPBDJointSettings& JointSettings,
			FDenseMatrix66& J0,
			FDenseMatrix66& J1,
			FDenseMatrix61& C);

		void AddAngularDrive_Swing(
			const FPBDJointSettings& JointSettings,
			const EJointAngularConstraintIndex SwingConstraintIndex,
			const EJointAngularAxisIndex SwingAxisIndex,
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

		void AddAngularDrives(
			const FPBDJointSettings& JointSettings,
			FDenseMatrix66& J0,
			FDenseMatrix66& J1,
			FDenseMatrix61& C);

		void BuildJacobianAndResidual_Constraints(
			const FPBDJointSettings& JointSettings,
			FDenseMatrix66& J0,
			FDenseMatrix66& J1,
			FDenseMatrix61& C);

		void BuildJacobianAndResidual_Drives(
			const FPBDJointSettings& JointSettings,
			FDenseMatrix66& J0,
			FDenseMatrix66& J1,
			FDenseMatrix61& C);

		void SolveAndApply(
			const FPBDJointSettings& JointSettings,
			const FDenseMatrix66& J0,
			const FDenseMatrix66& J1,
			const FDenseMatrix61& C);

		// Local-space constraint settings
		FRigidTransform3 XLs[MaxConstrainedBodies];	// Local-space joint connector transforms
		FMatrix33 InvILs[MaxConstrainedBodies];		// Local-space inverse inertias
		FReal InvMs[MaxConstrainedBodies];				// Inverse masses

		// World-space constraint state
		FVec3 Xs[MaxConstrainedBodies];				// World-space joint connector positions
		FRotation3 Rs[MaxConstrainedBodies];			// World-space joint connector rotations

		// World-space body state
		FVec3 Ps[MaxConstrainedBodies];				// World-space particle CoM positions
		FRotation3 Qs[MaxConstrainedBodies];			// World-space particle CoM rotations

		// Settings
		FReal Stiffness;
		FReal AngularDriveStiffness;
		FReal SwingTwistAngleTolerance;
		bool bEnableTwistLimits;
		bool bEnableSwingLimits;
		bool bEnableDrives;
	};

}