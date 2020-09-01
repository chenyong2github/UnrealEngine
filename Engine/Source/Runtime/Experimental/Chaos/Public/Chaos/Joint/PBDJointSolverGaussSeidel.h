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

		FORCEINLINE const FVec3& GetInitP(const int32 Index) const
		{
			checkSlow(Index < MaxConstrainedBodies);
			return InitPs[Index];
		}

		FORCEINLINE const FRotation3& GetInitQ(const int32 Index) const
		{
			checkSlow(Index < MaxConstrainedBodies);
			return InitQs[Index];
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

		FORCEINLINE const FVec3& GetNetLinearImpulse() const
		{
			return NetLinearImpulse;
		}

		FORCEINLINE const FVec3& GetNetAngularImpulse() const
		{
			return NetAngularImpulse;
		}

		FORCEINLINE int32 GetNumActiveConstraints() const
		{
			// We use -1 as unitialized, but that should not be exposed outside the solver
			return FMath::Max(NumActiveConstraints, 0);
		}

		FORCEINLINE bool GetIsActive() const
		{
			return bIsActive;
		}

		FORCEINLINE FReal InvM(int32 index) const
		{
			return InvMs[index];
		}


		FJointSolverGaussSeidel();

		void Init(
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
			const FRigidTransform3& XL1);

		void Update(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
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

		void ApplyProjections(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

	private:

		void InitDerivedState();

		void UpdateDerivedState(const int32 BodyIndex);

		void UpdateDerivedState();

		bool UpdateIsActive(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);


		void ApplyPositionConstraints(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		void ApplyRotationConstraints(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		void ApplyPositionDrives(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		void ApplyRotationDrives(
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

		void ApplyDelta(
			const int32 BodyIndex,
			const FReal Stiffness,
			const FVec3& DP,
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

		void ApplyPositionConstraint(
			const FReal Stiffness,
			const FVec3& Axis,
			const FReal Delta);

		void ApplyPositionConstraintSoft(
			const FReal Dt,
			const FReal Stiffness,
			const FReal Damping,
			const bool bAccelerationMode,
			const FVec3& Axis,
			const FReal Delta,
			const FReal TargetVel,
			FReal& Lambda);

		void ApplyRotationConstraint(
			const FReal Stiffness,
			const FVec3& Axis,
			const FReal Angle);

		void ApplyRotationConstraintKD(
			const int32 KIndex,
			const int32 DIndex,
			const FReal Stiffness,
			const FVec3& Axis,
			const FReal Angle);

		void ApplyRotationConstraintDD(
			const FReal Stiffness,
			const FVec3& Axis,
			const FReal Angle);

		void ApplyRotationConstraintSoft(
			const FReal Dt,
			const FReal Stiffness,
			const FReal Damping,
			const bool bAccelerationMode,
			const FVec3& Axis,
			const FReal Angle,
			const FReal AngVelTarget,
			FReal& Lambda);

		void ApplyRotationConstraintSoftKD(
			const int32 KIndex,
			const int32 DIndex,
			const FReal Dt,
			const FReal Stiffness,
			const FReal Damping,
			const bool bAccelerationMode,
			const FVec3& Axis,
			const FReal Angle,
			const FReal AngVelTarget,
			FReal& Lambda);

		void ApplyRotationConstraintSoftDD(
			const FReal Dt,
			const FReal Stiffness,
			const FReal Damping,
			const bool bAccelerationMode,
			const FVec3& Axis,
			const FReal Angle,
			const FReal AngVelTarget,
			FReal& Lambda);

		void ApplyLockedRotationConstraints(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const bool bApplyTwist,
			const bool bApplySwing);

		void ApplyTwistConstraint(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const bool bUseSoftLimit);

		void ApplyConeConstraint(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const bool bUseSoftLimit);

		// One Swing axis is free, and the other locked. This applies the lock: Body1 Twist axis is confined to a plane.
		void ApplySingleLockedSwingConstraint(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const EJointAngularConstraintIndex SwingConstraintIndex,
			const bool bUseSoftLimit);

		// One Swing axis is free, and the other limited. This applies the limit: Body1 Twist axis is confined to space between two cones.
		void ApplyDualConeSwingConstraint(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const EJointAngularConstraintIndex SwingConstraintIndex,
			const bool bUseSoftLimit);

		// One swing axis is locked, the other limited or locked. This applies the Limited axis (ApplyDualConeSwingConstraint is used for the locked axis).
		void ApplySwingConstraint(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const EJointAngularConstraintIndex SwingConstraintIndex,
			const bool bUseSoftLimit);

		void ApplySwingTwistDrives(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const bool bTwistDriveEnabled,
			const bool bSwing1DriveEnabled,
			const bool bSwing2DriveEnabled);

		void ApplySLerpDrive(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		void ApplyPointPositionConstraintKD(
			const int32 KIndex,
			const int32 DIndex,
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		void ApplyPointPositionConstraintDD(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		void ApplySphericalPositionConstraint(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		void ApplyCylindricalPositionConstraint(
			const FReal Dt,
			const int32 AxisIndex,
			const EJointMotionType AxialMotion,
			const EJointMotionType RadialMotion,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		void ApplyPlanarPositionConstraint(
			const FReal Dt,
			const int32 AxisIndex,
			const EJointMotionType AxialMotion,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		void ApplyPositionDrive(
			const FReal Dt,
			const int32 AxisIndex,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const FVec3& Axis,
			const FReal DeltaPos,
			const FReal DeltaVel);


		void ApplyPointProjection(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const float Alpha,
			FVec3& NetDP1,
			FVec3& NetDR1);

		void ApplySphereProjection(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const float Alpha,
			FVec3& NetDP1,
			FVec3& NetDR1);

		void ApplyTranslateProjection(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const float Alpha,
			FVec3& NetDP1,
			FVec3& NetDR1);

		void ApplyConeProjection(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const float Alpha,
			const bool bPositionLocked,
			FVec3& NetDP1,
			FVec3& NetDR1);

		void ApplySwingProjection(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const EJointAngularConstraintIndex SwingConstraintIndex,
			const float Alpha,
			const bool bPositionLocked,
			FVec3& NetDP1,
			FVec3& NetDR1);

		void ApplySingleLockedSwingProjection(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const EJointAngularConstraintIndex SwingConstraintIndex,
			const float Alpha,
			const bool bPositionLocked,
			FVec3& NetDP1,
			FVec3& NetDR1);

		void ApplyDoubleLockedSwingProjection(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const float Alpha,
			const bool bPositionLocked,
			FVec3& NetDP1,
			FVec3& NetDR1);

		void ApplyDualConeSwingProjection(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const EJointAngularConstraintIndex SwingConstraintIndex,
			const float Alpha,
			const bool bPositionLocked,
			FVec3& NetDP1,
			FVec3& NetDR1);

		void ApplyTwistProjection(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const float Alpha,
			const bool bPositionLocked,
			FVec3& NetDP1,
			FVec3& NetDR1);

		void ApplyVelocityProjection(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const float Alpha,
			const FVec3& DP1,
			const FVec3& DR1);

		void CalculateLinearConstraintPadding(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const FReal Restitution,
			const int32 AxisIndex,
			const FVec3 Axis,
			FReal& InOutPos);

		void CalculateAngularConstraintPadding(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const FReal Restitution,
			const EJointAngularConstraintIndex ConstraintIndex,
			const FVec3 Axis,
			FReal& InOutAngle);

		inline bool HasLinearConstraintPadding(const int32 AxisIndex) const
		{
			return LinearConstraintPadding[AxisIndex] >= 0.0f;
		}

		inline FReal GetLinearConstraintPadding(const int32 AxisIndex) const
		{
			return FMath::Max(LinearConstraintPadding[AxisIndex], 0.0f);
		}

		inline void SetLinearConstraintPadding(const int32 AxisIndex, FReal Padding)
		{
			LinearConstraintPadding[AxisIndex] = Padding;
		}


		inline bool HasAngularConstraintPadding(const EJointAngularConstraintIndex ConstraintIndex) const
		{
			return AngularConstraintPadding[(int32)ConstraintIndex] >= 0.0f;
		}

		inline FReal GetAngularConstraintPadding(const EJointAngularConstraintIndex ConstraintIndex) const
		{
			return FMath::Max(AngularConstraintPadding[(int32)ConstraintIndex], 0.0f);
		}

		inline void SetAngularConstraintPadding(const EJointAngularConstraintIndex ConstraintIndex, FReal Padding)
		{
			AngularConstraintPadding[(int32)ConstraintIndex] = Padding;
		}


		using FDenseMatrix66 = TDenseMatrix<6 * 6>;
		using FDenseMatrix61 = TDenseMatrix<6 * 1>;

		void ApplyConstraintsMatrix(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		int32 AddLinear(
			const FReal Dt,
			const FVec3& Axis,
			const FVec3& ConnectorOffset0,
			const FVec3& ConnectorOffset1,
			const FReal Error,
			const FReal VelTarget,
			const FReal Stiffness,
			const FReal Damping,
			const bool bSoft,
			const bool bAccelerationMode,
			FDenseMatrix66& J0,
			FDenseMatrix66& J1,
			FDenseMatrix61& C,
			FDenseMatrix61& V,
			FDenseMatrix61& S,
			FDenseMatrix61& D);

		int32 AddAngular(
			const FReal Dt,
			const FVec3& Axis,
			const FReal Error,
			const FReal VelTarget,
			const FReal Stiffness,
			const FReal Damping,
			const bool bSoft,
			const bool bAccelerationMode,
			FDenseMatrix66& J0,
			FDenseMatrix66& J1,
			FDenseMatrix61& C,
			FDenseMatrix61& V,
			FDenseMatrix61& S,
			FDenseMatrix61& D);

		// Local-space constraint settings
		FRigidTransform3 XLs[MaxConstrainedBodies];	// Local-space joint connector transforms
		FVec3 InvILs[MaxConstrainedBodies];			// Local-space inverse inertias
		FReal InvMs[MaxConstrainedBodies];			// Inverse masses

		// World-space constraint state
		FVec3 Xs[MaxConstrainedBodies];				// World-space joint connector positions
		FRotation3 Rs[MaxConstrainedBodies];		// World-space joint connector rotations

		// World-space body state
		FVec3 Ps[MaxConstrainedBodies];				// World-space particle CoM positions
		FRotation3 Qs[MaxConstrainedBodies];		// World-space particle CoM rotations
		FVec3 Vs[MaxConstrainedBodies];				// World-space particle CoM velocities
		FVec3 Ws[MaxConstrainedBodies];				// World-space particle CoM angular velocities
		FMatrix33 InvIs[MaxConstrainedBodies];		// World-space inverse inertias

		// XPBD Initial iteration world-space body state
		FVec3 InitPs[MaxConstrainedBodies];			// World-space particle positions
		FRotation3 InitQs[MaxConstrainedBodies];	// World-space particle rotations
		FVec3 InitXs[MaxConstrainedBodies];			// World-space joint connector positions
		FRotation3 InitRs[MaxConstrainedBodies];	// World-space joint connector rotations

		// Accumulated Impulse and AngularImpulse (Impulse * Dt since they are mass multiplied position corrections)
		FVec3 NetLinearImpulse;
		FVec3 NetAngularImpulse;

		// XPBD Accumulators (net impulse for each soft constraint/drive)
		FReal LinearSoftLambda;
		FReal TwistSoftLambda;
		FReal SwingSoftLambda;
		FVec3 LinearDriveLambdas;
		FVec3 RotationDriveLambdas;

		// Constraint padding which can act something like a velocity constraint (for reslockfreetitution)
		FVec3 LinearConstraintPadding;
		FVec3 AngularConstraintPadding;

		// Tolerances below which we stop solving
		FReal PositionTolerance;					// Distance error below which we consider a constraint or drive solved
		FReal AngleTolerance;						// Angle error below which we consider a constraint or drive solved

		// Tracking whether the solver is resolved
		FVec3 LastPs[MaxConstrainedBodies];			// Positions at the beginning of the iteration
		FRotation3 LastQs[MaxConstrainedBodies];	// Rotations at the beginning of the iteration
		int32 NumActiveConstraints;					// The number of active constraints and drives in the last iteration (-1 initial value)
		bool bIsActive;								// Whether any constraints actually moved any bodies in last iteration
	};

}