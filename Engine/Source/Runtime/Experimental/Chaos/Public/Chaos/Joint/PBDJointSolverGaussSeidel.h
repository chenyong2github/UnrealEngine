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
	 * individually and resolves them in sequence.
	 *
	 * \see FJointSolverCholesky
	 */
	class FJointSolverGaussSeidel
	{
	public:
		static const int32 MaxConstraintedBodies = 2;

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

		FJointSolverGaussSeidel();

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

		void ApplyPositionConstraints(
			const FReal Dt,
			const FPBDJointSettings& JointSettings);

		void ApplyRotationConstraints(
			const FReal Dt,
			const FPBDJointSettings& JointSettings);

		void ApplyPositionDelta(
			const FReal Stiffness,
			const FVec3& DP0,
			const FVec3& DP1);

		void ApplyRotationDelta(
			const FReal Stiffness,
			const FVec3& DR0,
			const FVec3& DR1);

		void ApplyRotationDelta(
			const FReal Stiffness,
			const FVec3& Axis0,
			const FReal Angle0,
			const FVec3& Axis1,
			const FReal Angle1);

		void ApplyJointTwistConstraint(
			const FReal Dt,
			const FPBDJointSettings& JointSettings,
			const FRotation3& R01Twist,
			const FRotation3& R01Swing);

		void ApplyJointConeConstraint(
			const FReal Dt,
			const FPBDJointSettings& JointSettings,
			const FRotation3& R01Twist,
			const FRotation3& R01Swing);

		void ApplyJointSwingConstraint(
			const FReal Dt,
			const FPBDJointSettings& JointSettings,
			const FRotation3& R01Twist,
			const FRotation3& R01Swing,
			const EJointAngularConstraintIndex SwingConstraintIndex,
			const EJointAngularAxisIndex SwingAxisIndex);

		void ApplyJointPositionConstraint(
			const FReal Dt,
			const FPBDJointSettings& JointSettings);

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
		FReal LinearStiffness;
		FReal TwistStiffness;
		FReal SwingStiffness;
		FReal SwingTwistAngleTolerance;
		bool bEnableTwistLimits;
		bool bEnableSwingLimits;
	};

}