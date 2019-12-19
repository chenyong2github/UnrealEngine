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
		static const int32 MaxConstrainedBodies = 2;

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

		FORCEINLINE const FVec3& GetV(const int32 Index) const
		{
			checkSlow(Index < MaxConstrainedBodies);
			return Vs[Index];
		}

		FORCEINLINE const FVec3& GetW(const int32 Index) const
		{
			checkSlow(Index < MaxConstrainedBodies);
			return Ws[Index];
		}

		FJointSolverGaussSeidel();

		void Init(
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
			const FRigidTransform3& XL1);

		void Update(
			const FReal Dt,
			const FVec3& P0,
			const FRotation3& Q0,
			const FVec3& V0,
			const FVec3& W0,
			const FVec3& P1,
			const FRotation3& Q1,
			const FVec3& V1,
			const FVec3& W1);

		void ApplyConstraints(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		void ApplyDrives(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		void ApplyProjections(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

	private:

		void UpdateDerivedState(const int32 BodyIndex);
		void UpdateDerivedState();

		void ApplyPositionConstraints(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		void ApplyRotationConstraints(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		void ApplyRotationDrives(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		void ApplyRotationProjection(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);


		void ApplyPositionDelta(
			const int32 BodyIndex,
			const FReal Stiffness,
			const FVec3& DP);

		void ApplyPositionDelta(
			const FReal Stiffness,
			const FVec3& DP0,
			const FVec3& DP1);

		void ApplyRotationDelta(
			const int32 BodyIndex,
			const FReal Stiffness,
			const FVec3& DR);

		void ApplyRotationDelta(
			const FReal Stiffness,
			const FVec3& DR0,
			const FVec3& DR1);

		void ApplyVelocityDelta(
			const int32 BodyIndex,
			const FReal Stiffness,
			const FVec3& DV,
			const FVec3& DW);

		void ApplyVelocityDelta(
			const FReal Stiffness,
			const FVec3& DV0,
			const FVec3& DW0,
			const FVec3& DV1,
			const FVec3& DW1);

		void ApplyRotationCorrection(
			const FReal Stiffness,
			const FVec3& Axis0,
			const FVec3& Axis1,
			const FReal Angle);

		void ApplyRotationCorrectionSoft(
			const FReal Dt,
			const FReal Stiffness,
			const FReal Damping,
			const bool bAccelerationMode,
			const FVec3& Axis0,
			const FVec3& Axis1,
			const FReal Angle,
			FReal& Lambda);

		void ApplyTwistConstraint(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		void ApplyTwistConstraintSoft(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		void ApplyTwistDrive(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		void ApplyTwistProjection(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		void ApplyConeConstraint(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		void ApplyConeConstraintSoft(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		void ApplyConeDrive(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		void ApplyConeProjection(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		void ApplySwingConstraint(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const EJointAngularConstraintIndex SwingConstraintIndex,
			const EJointAngularAxisIndex SwingAxisIndex);

		void ApplySwingConstraintSoft(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const EJointAngularConstraintIndex SwingConstraintIndex,
			const EJointAngularAxisIndex SwingAxisIndex);

		void ApplySwingDrive(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const EJointAngularConstraintIndex SwingConstraintIndex,
			const EJointAngularAxisIndex SwingAxisIndex);

		void ApplySwingProjection(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const EJointAngularConstraintIndex SwingConstraintIndex,
			const EJointAngularAxisIndex SwingAxisIndex);

		void ApplySLerpDrive(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		void ApplyPositionConstraint(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		void ApplyPositionProjection(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);


		// Local-space constraint settings
		FRigidTransform3 XLs[MaxConstrainedBodies];	// Local-space joint connector transforms
		FMatrix33 InvILs[MaxConstrainedBodies];		// Local-space inverse inertias
		FReal InvMs[MaxConstrainedBodies];			// Inverse masses

		// World-space constraint state
		FVec3 Xs[MaxConstrainedBodies];				// World-space joint connector positions
		FRotation3 Rs[MaxConstrainedBodies];		// World-space joint connector rotations

		// World-space body state
		FVec3 Ps[MaxConstrainedBodies];				// World-space particle CoM positions
		FRotation3 Qs[MaxConstrainedBodies];		// World-space particle CoM rotations
		FVec3 Vs[MaxConstrainedBodies];				// World-space particle CoM velocities
		FVec3 Ws[MaxConstrainedBodies];				// World-space particle CoM angular velocities

		// XPBD Previous iteration world-space body state
		FRotation3 PrevQs[MaxConstrainedBodies];	// World-space particle CoM rotations

		// XPBD constraint multipliers
		FReal TwistSoftLambda;
		FReal SwingSoftLambda;
		FReal TwistDriveLambda;
		FReal SwingDriveLambda;
	};

}