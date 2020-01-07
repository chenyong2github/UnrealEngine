// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Chaos/Core.h"
#include "Chaos/Transform.h"
#include "Chaos/Vector.h"

namespace Chaos
{
	class FPBDJointConstraints;

	class FPBDJointConstraintHandle;

	using FJointPreApplyCallback = TFunction<void(const FReal Dt, const TArray<FPBDJointConstraintHandle*>& InConstraintHandles)>;

	using FJointPostApplyCallback = TFunction<void(const FReal Dt, const TArray<FPBDJointConstraintHandle*>& InConstraintHandles)>;

	enum class EJointMotionType : int32
	{
		Free,
		Limited,
		Locked,
	};

	enum class EJointForceMode : int32
	{
		Acceleration,
		Force,
	};

	/**
	 * The order of the angular constraints (for settings held in vectors etc)
	 */
	enum class EJointAngularConstraintIndex : int32
	{
		Twist,
		Swing2,
		Swing1,
	};

	/**
	 * The constraint-space axis about which each rotation constraint is applied
	 */
	enum class EJointAngularAxisIndex : int32
	{
		Twist = 0,		// Twist Axis = X
		Swing2 = 1,		// Swing2 Axis = Y
		Swing1 = 2,		// Swing1 Axis = Z
	};

	struct FJointConstants
	{
		/** The constraint-space twist axis (X Axis) */
		static const FVec3 TwistAxis() { return FVec3(1, 0, 0); }

		/** The constraint-space Swing1 axis (Z Axis) */
		static const FVec3 Swing1Axis() { return FVec3(0, 0, 1); }

		/** The constraint-space Swing2 axis (Y Axis) */
		static const FVec3 Swing2Axis() { return FVec3(0, 1, 0); }
	};

	class CHAOS_API FPBDJointSettings
	{
	public:
		FPBDJointSettings();
		FPBDJointSettings(const TVector<EJointMotionType, 3>& InLinearMotionTypes, const TVector<EJointMotionType, 3>& InAngularMotionTypes);

		void Sanitize();

		FReal Stiffness;
		FReal LinearProjection;
		FReal AngularProjection;
		FReal ParentInvMassScale;

		TVector<EJointMotionType, 3> LinearMotionTypes;
		FReal LinearLimit;

		TVector<EJointMotionType, 3> AngularMotionTypes;
		FVec3 AngularLimits;

		bool bSoftLinearLimitsEnabled;
		bool bSoftTwistLimitsEnabled;
		bool bSoftSwingLimitsEnabled;
		EJointForceMode SoftForceMode;
		FReal SoftLinearStiffness;
		FReal SoftLinearDamping;
		FReal SoftTwistStiffness;
		FReal SoftTwistDamping;
		FReal SoftSwingStiffness;
		FReal SoftSwingDamping;

		FVec3 LinearDriveTarget;
		TVector<bool, 3> bLinearDriveEnabled;
		EJointForceMode LinearDriveForceMode;
		FReal LinearDriveStiffness;
		FReal LinearDriveDamping;

		// @todo(ccaulfield): remove one of these
		FRotation3 AngularDriveTarget;
		FVec3 AngularDriveTargetAngles;

		bool bAngularSLerpDriveEnabled;
		bool bAngularTwistDriveEnabled;
		bool bAngularSwingDriveEnabled;
		EJointForceMode AngularDriveForceMode;
		FReal AngularDriveStiffness;
		FReal AngularDriveDamping;
	};

	class CHAOS_API FPBDJointSolverSettings
	{
	public:
		FPBDJointSolverSettings();

		// Iterations
		int32 ApplyPairIterations;
		int32 ApplyPushOutPairIterations;

		// Tolerances
		FReal SwingTwistAngleTolerance;

		// Stability control
		FReal MinParentMassRatio;
		FReal MaxInertiaRatio;

		// @todo(ccaulfield): remove these TEMP overrides for testing
		bool bEnableTwistLimits;
		bool bEnableSwingLimits;
		bool bEnableDrives;
		FReal LinearProjection;
		FReal AngularProjection;
		FReal Stiffness;
		FReal LinearDriveStiffness;
		FReal LinearDriveDamping;
		FReal AngularDriveStiffness;
		FReal AngularDriveDamping;
		FReal SoftLinearStiffness;
		FReal SoftLinearDamping;
		FReal SoftTwistStiffness;
		FReal SoftTwistDamping;
		FReal SoftSwingStiffness;
		FReal SoftSwingDamping;
	};
}