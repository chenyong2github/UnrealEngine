// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/PBDJointConstraintTypes.h"


namespace Chaos
{
	class FPBDJointUtilities
	{
	public:
		static void DecomposeSwingTwistLocal(const FRotation3& R0, const FRotation3& R1, FRotation3& R01Swing, FRotation3& R01Twist);

		/**
		 * Increase the lower inertia components to ensure that the maximum ratio between any pair of elements is MaxRatio.
		 *
		 * @param InI The input inertia.
		 * @return An altered inertia so that the minimum element is at least MaxElement/MaxRatio.
		 */
		static CHAOS_API FVec3 ConditionInertia(
			const FVec3& InI, 
			const FReal MaxRatio);

		/**
		 * Increase the IParent inertia so that its largest component is at least MinRatio times the largest IChild component.
		 * This is used to condition joint chains for more robust solving with low iteration counts or larger time steps.
		 *
		 * @param IParent The input inertia.
		 * @param IChild The input inertia.
		 * @param OutIParent The output inertia.
		 * @param MinRatio Parent inertia will be at least this multiple of child inertia
		 * @return The max/min ratio of InI elements.
		 */
		static CHAOS_API FVec3 ConditionParentInertia(
			const FVec3& IParent, 
			const FVec3& IChild, 
			const FReal MinRatio);

		static CHAOS_API FReal ConditionParentMass(
			const FReal MParent, 
			const FReal MChild, 
			const FReal MinRatio);

		static CHAOS_API void GetConditionedInverseMass(
			const float MParent,
			const FVec3 IParent,
			const float MChild,
			const FVec3 IChild,
			FReal& OutInvMParent,
			FReal& OutInvMChild,
			FMatrix33& OutInvIParent,
			FMatrix33& OutInvIChild,
			const FReal MinParentMassRatio,
			const FReal MaxInertiaRatio);

		static CHAOS_API void GetConditionedInverseMass(
			const float M,
			const FVec3 I,
			FReal& OutInvM0,
			FMatrix33& OutInvI0, 
			const FReal MaxInertiaRatio);


		static FReal GetLinearStiffness(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);
		
		static FReal GetTwistStiffness(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		static FReal GetSwingStiffness(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		static FReal GetAngularDriveStiffness(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		static FReal GetAngularDriveDamping(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		static FReal GetLinearProjection(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		static FReal GetAngularProjection(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		static FVec3 GetSphereLimitedPositionError(const FVec3& CX, const FReal Radius);
		static FVec3 GetSphereLimitedVelocityError(const FVec3& CX, const FReal Radius, const FVec3& CV);
		static FVec3 GetCylinderLimitedPositionError(const FVec3& InCX, const FVec3& Axis, const FReal Limit, const EJointMotionType AxisMotion);
		static FVec3 GetCylinderLimitedVelocityError(const FVec3& InCX, const FVec3& Axis, const FReal Limit, const EJointMotionType AxisMotion, const FVec3& CV);
		static FVec3 GetLineLimitedPositionError(const FVec3& CX, const FVec3& Axis, const FReal Limit, const EJointMotionType AxisMotion);
		static FVec3 GetLineLimitedVelocityError(const FVec3& CX, const FVec3& Axis, const FReal Limit, const EJointMotionType AxisMotion, const FVec3& CV);
		static FVec3 GetLimitedPositionError(const FPBDJointSettings& JointSettings, const FRotation3& R0, const FVec3& InCX);
		static FVec3 GetLimitedVelocityError(const FPBDJointSettings& JointSettings, const FRotation3& R0, const FVec3& InCX, const FVec3& InCV);

		static CHAOS_API void CalculateSwingConstraintSpace(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const FRigidTransform3& XL0,
			const FRigidTransform3& XL1,
			const FVec3& P0,
			const FRotation3& Q0,
			const FVec3& P1,
			const FRotation3& Q1,
			FVec3& OutX0,
			FMatrix33& OutR0,
			FVec3& OutX1,
			FMatrix33& OutR1,
			FVec3& OutCR);

		static CHAOS_API void CalculateConeConstraintSpace(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const FRigidTransform3& XL0,
			const FRigidTransform3& XL1,
			const FVec3& P0,
			const FRotation3& Q0,
			const FVec3& P1,
			const FRotation3& Q1,
			FVec3& OutX0,
			FMatrix33& OutR0,
			FVec3& OutX1,
			FMatrix33& OutR1,
			FVec3& OutCR);

		static CHAOS_API void ApplyJointPositionConstraint(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const FReal Stiffness,
			const FRigidTransform3& XL0,
			const FRigidTransform3& XL1,
			FVec3& P0,
			FRotation3& Q0,
			FVec3& V0,
			FVec3& W0,
			FVec3& P1,
			FRotation3& Q1,
			FVec3& V1,
			FVec3& W1,
			float InvM0,
			const FMatrix33& InvIL0,
			float InvM1,
			const FMatrix33& InvIL1);

		static CHAOS_API void ApplyJointVelocityConstraint(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const FReal Stiffness,
			const FRigidTransform3& XL0,
			const FRigidTransform3& XL1,
			FVec3& P0,
			FRotation3& Q0,
			FVec3& V0,
			FVec3& W0,
			FVec3& P1,
			FRotation3& Q1,
			FVec3& V1,
			FVec3& W1,
			float InvM0,
			const FMatrix33& InvIL0,
			float InvM1,
			const FMatrix33& InvIL1);

		static CHAOS_API void ApplyJointTwistConstraint(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const FReal Stiffness,
			const FRigidTransform3& XL0,
			const FRigidTransform3& XL1,
			FVec3& P0,
			FRotation3& Q0,
			FVec3& V0,
			FVec3& W0,
			FVec3& P1,
			FRotation3& Q1,
			FVec3& V1,
			FVec3& W1,
			float InvM0,
			const FMatrix33& InvIL0,
			float InvM1,
			const FMatrix33& InvIL1);

		static CHAOS_API void ApplyJointTwistVelocityConstraint(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const FReal Stiffness,
			const FRigidTransform3& XL0,
			const FRigidTransform3& XL1,
			FVec3& P0,
			FRotation3& Q0,
			FVec3& V0,
			FVec3& W0,
			FVec3& P1,
			FRotation3& Q1,
			FVec3& V1,
			FVec3& W1,
			float InvM0,
			const FMatrix33& InvIL0,
			float InvM1,
			const FMatrix33& InvIL1);

		static CHAOS_API void ApplyJointConeConstraint(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const FReal Stiffness,
			const FRigidTransform3& XL0,
			const FRigidTransform3& XL1,
			FVec3& P0,
			FRotation3& Q0,
			FVec3& V0,
			FVec3& W0,
			FVec3& P1,
			FRotation3& Q1,
			FVec3& V1,
			FVec3& W1,
			float InvM0,
			const FMatrix33& InvIL0,
			float InvM1,
			const FMatrix33& InvIL1);

		static CHAOS_API void ApplyJointConeVelocityConstraint(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const FReal Stiffness,
			const FRigidTransform3& XL0,
			const FRigidTransform3& XL1,
			FVec3& P0,
			FRotation3& Q0,
			FVec3& V0,
			FVec3& W0,
			FVec3& P1,
			FRotation3& Q1,
			FVec3& V1,
			FVec3& W1,
			float InvM0,
			const FMatrix33& InvIL0,
			float InvM1,
			const FMatrix33& InvIL1);

		static CHAOS_API void ApplyJointSwingConstraint(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const FReal Stiffness,
			const FRigidTransform3& XL0,
			const FRigidTransform3& XL1,
			const EJointAngularConstraintIndex SwingConstraintIndex,
			const EJointAngularAxisIndex SwingAxisIndex,
			FVec3& P0,
			FRotation3& Q0,
			FVec3& V0,
			FVec3& W0,
			FVec3& P1,
			FRotation3& Q1,
			FVec3& V1,
			FVec3& W1,
			float InvM0,
			const FMatrix33& InvIL0,
			float InvM1,
			const FMatrix33& InvIL1);

		static CHAOS_API void ApplyJointSwingVelocityConstraint(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const FReal Stiffness,
			const FRigidTransform3& XL0,
			const FRigidTransform3& XL1,
			const EJointAngularConstraintIndex SwingConstraintIndex,
			const EJointAngularAxisIndex SwingAxisIndex,
			FVec3& P0,
			FRotation3& Q0,
			FVec3& V0,
			FVec3& W0,
			FVec3& P1,
			FRotation3& Q1,
			FVec3& V1,
			FVec3& W1,
			float InvM0,
			const FMatrix33& InvIL0,
			float InvM1,
			const FMatrix33& InvIL1);

		static CHAOS_API void ApplyJointTwistDrive(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const FRigidTransform3& XL0,
			const FRigidTransform3& XL1,
			FVec3& P0,
			FRotation3& Q0,
			FVec3& V0,
			FVec3& W0,
			FVec3& P1,
			FRotation3& Q1,
			FVec3& V1,
			FVec3& W1,
			float InvM0,
			const FMatrix33& InvIL0,
			float InvM1,
			const FMatrix33& InvIL1);

		static CHAOS_API void ApplyJointConeDrive(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const FRigidTransform3& XL0,
			const FRigidTransform3& XL1,
			FVec3& P0,
			FRotation3& Q0,
			FVec3& V0,
			FVec3& W0,
			FVec3& P1,
			FRotation3& Q1,
			FVec3& V1,
			FVec3& W1,
			float InvM0,
			const FMatrix33& InvIL0,
			float InvM1,
			const FMatrix33& InvIL1);

		static CHAOS_API void ApplyJointSLerpDrive(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const FRigidTransform3& XL0,
			const FRigidTransform3& XL1,
			FVec3& P0,
			FRotation3& Q0,
			FVec3& V0,
			FVec3& W0,
			FVec3& P1,
			FRotation3& Q1,
			FVec3& V1,
			FVec3& W1,
			float InvM0,
			const FMatrix33& InvIL0,
			float InvM1,
			const FMatrix33& InvIL1);

		static CHAOS_API void ApplyJointPositionProjection(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const FReal Stiffness,
			const FRigidTransform3& XL0,
			const FRigidTransform3& XL1,
			FVec3& P0,
			FRotation3& Q0,
			FVec3& P1,
			FRotation3& Q1,
			float InvM0,
			const FMatrix33& InvIL0,
			float InvM1,
			const FMatrix33& InvIL1);

		static CHAOS_API void ApplyJointTwistProjection(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const FReal Stiffness,
			const FRigidTransform3& XL0,
			const FRigidTransform3& XL1,
			FVec3& P0,
			FRotation3& Q0,
			FVec3& P1,
			FRotation3& Q1,
			float InvM0,
			const FMatrix33& InvIL0,
			float InvM1,
			const FMatrix33& InvIL1);

		static CHAOS_API void ApplyJointConeProjection(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const FReal Stiffness,
			const FRigidTransform3& XL0,
			const FRigidTransform3& XL1,
			FVec3& P0,
			FRotation3& Q0,
			FVec3& P1,
			FRotation3& Q1,
			float InvM0,
			const FMatrix33& InvIL0,
			float InvM1,
			const FMatrix33& InvIL1);

		static CHAOS_API void ApplyJointSwingProjection(
			const FReal Dt,
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings,
			const FReal Stiffness,
			const FRigidTransform3& XL0,
			const FRigidTransform3& XL1,
			const EJointAngularConstraintIndex SwingConstraintIndex,
			const EJointAngularAxisIndex SwingAxisIndex,
			FVec3& P0,
			FRotation3& Q0,
			FVec3& P1,
			FRotation3& Q1,
			float InvM0,
			const FMatrix33& InvIL0,
			float InvM1,
			const FMatrix33& InvIL1);

	};
}