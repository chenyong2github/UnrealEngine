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

	enum class EJointSolverPhase
	{
		None,
		Apply,
		ApplyPushOut,
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

	class CHAOS_API FPBDJointMotionSettings
	{
	public:
		FPBDJointMotionSettings();
		FPBDJointMotionSettings(const TVector<EJointMotionType, 3>& InLinearMotionTypes, const TVector<EJointMotionType, 3>& InAngularMotionTypes);

		void Sanitize();

		FReal Stiffness;
		FReal LinearProjection;
		FReal AngularProjection;

		TVector<EJointMotionType, 3> LinearMotionTypes;
		FReal LinearLimit;

		TVector<EJointMotionType, 3> AngularMotionTypes;
		FVec3 AngularLimits;

		bool bSoftLinearLimitsEnabled;
		bool bSoftTwistLimitsEnabled;
		bool bSoftSwingLimitsEnabled;
		FReal SoftLinearStiffness;
		FReal SoftTwistStiffness;
		FReal SoftSwingStiffness;

		// @todo(ccaulfield): remove one of these
		FRotation3 AngularDriveTarget;
		FVec3 AngularDriveTargetAngles;

		bool bAngularSLerpDriveEnabled;
		bool bAngularTwistDriveEnabled;
		bool bAngularSwingDriveEnabled;
		FReal AngularDriveStiffness;
	};


	class CHAOS_API FPBDJointSettings
	{
	public:
		using FTransformPair = TVector<FRigidTransform3, 2>;

		FPBDJointSettings();

		// Particle-relative joint axes and positions
		FTransformPair ConstraintFrames;

		// How the constraint is allowed to move
		FPBDJointMotionSettings Motion;
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
		EJointSolverPhase ProjectionPhase;
		FReal LinearProjection;
		FReal AngularProjection;
		FReal Stiffness;
		FReal DriveStiffness;
		FReal SoftLinearStiffness;
		FReal SoftAngularStiffness;
	};
}