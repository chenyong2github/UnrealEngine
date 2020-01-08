// Copyright Epic Games, Inc. All Rights Reserved.
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
			FVec3& OutInvIParent,
			FVec3& OutInvIChild,
			const FReal MinParentMassRatio,
			const FReal MaxInertiaRatio);

		static CHAOS_API void GetConditionedInverseMass(
			const float M,
			const FVec3 I,
			FReal& OutInvM0,
			FVec3& OutInvI0,
			const FReal MaxInertiaRatio);


		static FReal GetLinearStiffness(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		static FReal GetSoftLinearStiffness(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		static FReal GetSoftLinearDamping(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		static FReal GetTwistStiffness(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		static FReal GetSoftTwistStiffness(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		static FReal GetSoftTwistDamping(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		static FReal GetSwingStiffness(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		static FReal GetSoftSwingStiffness(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		static FReal GetSoftSwingDamping(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		static FReal GetLinearDriveStiffness(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		static FReal GetLinearDriveDamping(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		static FReal GetAngularTwistDriveStiffness(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		static FReal GetAngularTwistDriveDamping(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		static FReal GetAngularSwingDriveStiffness(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		static FReal GetAngularSwingDriveDamping(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		static FReal GetAngularSLerpDriveStiffness(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		static FReal GetAngularSLerpDriveDamping(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		static FReal GetLinearProjection(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		static FReal GetAngularProjection(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		static bool GetLinearSoftAccelerationMode(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		static bool GetAngularSoftAccelerationMode(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		static bool GetDriveAccelerationMode(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		static FReal GetAngularPositionCorrection(
			const FPBDJointSolverSettings& SolverSettings,
			const FPBDJointSettings& JointSettings);

		static FVec3 GetSphereLimitedPositionError(const FVec3& CX, const FReal Radius);
		static FVec3 GetCylinderLimitedPositionError(const FVec3& CX, const FVec3& Axis, const FReal Limit, const EJointMotionType AxisMotion);
		static FVec3 GetLineLimitedPositionError(const FVec3& CX, const FVec3& Axis, const FReal Limit, const EJointMotionType AxisMotion);
		static FVec3 GetLimitedPositionError(const FPBDJointSettings& JointSettings, const FRotation3& R0, const FVec3& CX);
	};
}