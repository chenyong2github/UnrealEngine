// Copyright Epic Games, Inc. All Rights Reserved.
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

	using FJointBreakCallback = TFunction<void(FPBDJointConstraintHandle * ConstraintHandle)>;

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
	// @todo(ccaulfield): rename EJointAngularConstraintType
	enum class EJointAngularConstraintIndex : int32
	{
		Twist,
		Swing2,
		Swing1,
	};

	struct FJointConstants
	{
		/** The constraint-space twist axis (X Axis) */
		static inline const FVec3 TwistAxis() { return FVec3(1, 0, 0); }

		/** The constraint-space Swing1 axis (Z Axis) */
		static inline const FVec3 Swing1Axis() { return FVec3(0, 0, 1); }

		/** The constraint-space Swing2 axis (Y Axis) */
		static inline const FVec3 Swing2Axis() { return FVec3(0, 1, 0); }

		/** Get the local-space axis for the specified constraint type. This will be one of the cardinal axes. */
		static inline const FVec3 Axis(const EJointAngularConstraintIndex ConstraintIndex)
		{
			switch (ConstraintIndex)
			{
			case EJointAngularConstraintIndex::Twist:
				return TwistAxis();
			case EJointAngularConstraintIndex::Swing1:
				return Swing1Axis();
			case EJointAngularConstraintIndex::Swing2:
				return Swing2Axis();
			}
		}

		static inline const FVec3 SwingAxis(const EJointAngularConstraintIndex ConstraintIndex)
		{
			return (ConstraintIndex == EJointAngularConstraintIndex::Swing1) ? Swing1Axis() : Swing2Axis();
		}

		static inline const FVec3 OtherSwingAxis(const EJointAngularConstraintIndex ConstraintIndex)
		{
			return (ConstraintIndex == EJointAngularConstraintIndex::Swing1) ? Swing2Axis() : Swing1Axis();
		}

		/** Get the local-space axis index for the specified constraint type. This can be used to index the vectors of a transform matrix for example. */
		static inline const int32 AxisIndex(const EJointAngularConstraintIndex ConstraintIndex)
		{
			if (ConstraintIndex == EJointAngularConstraintIndex::Twist)
			{
				return 0;	// X
			}
			else if (ConstraintIndex == EJointAngularConstraintIndex::Swing1)
			{
				return 2;	// Z
			}
			else
			{
				return 1;	// Y
			}
		}
	};

	class CHAOS_API FPBDJointSettings
	{
	public:
		FPBDJointSettings();

		void Sanitize();

		FReal Stiffness;
		FReal LinearProjection;
		FReal AngularProjection;
		FReal ParentInvMassScale;

		bool bCollisionEnabled;
		bool bProjectionEnabled;
		bool bSoftProjectionEnabled;

		TVector<EJointMotionType, 3> LinearMotionTypes;
		FReal LinearLimit;

		TVector<EJointMotionType, 3> AngularMotionTypes;
		FVec3 AngularLimits;

		bool bSoftLinearLimitsEnabled;
		bool bSoftTwistLimitsEnabled;
		bool bSoftSwingLimitsEnabled;
		EJointForceMode LinearSoftForceMode;
		EJointForceMode AngularSoftForceMode;
		FReal SoftLinearStiffness;
		FReal SoftLinearDamping;
		FReal SoftTwistStiffness;
		FReal SoftTwistDamping;
		FReal SoftSwingStiffness;
		FReal SoftSwingDamping;

		FReal LinearRestitution;
		FReal TwistRestitution;
		FReal SwingRestitution;

		FReal LinearContactDistance;
		FReal TwistContactDistance;
		FReal SwingContactDistance;

		FVec3 LinearDrivePositionTarget;
		FVec3 LinearDriveVelocityTarget;
		TVector<bool, 3> bLinearPositionDriveEnabled;
		TVector<bool, 3> bLinearVelocityDriveEnabled;
		EJointForceMode LinearDriveForceMode;
		FReal LinearDriveStiffness;
		FReal LinearDriveDamping;

		FRotation3 AngularDrivePositionTarget;
		FVec3 AngularDriveVelocityTarget;

		bool bAngularSLerpPositionDriveEnabled;
		bool bAngularSLerpVelocityDriveEnabled;
		bool bAngularTwistPositionDriveEnabled;
		bool bAngularTwistVelocityDriveEnabled;
		bool bAngularSwingPositionDriveEnabled;
		bool bAngularSwingVelocityDriveEnabled;
		EJointForceMode AngularDriveForceMode;
		FReal AngularDriveStiffness;
		FReal AngularDriveDamping;

		FReal LinearBreakForce;
		FReal LinearPlasticityLimit;
		FReal AngularBreakTorque;
		FReal AngularPlasticityLimit;

		void* UserData;
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
		FReal PositionTolerance;
		FReal AngleTolerance;

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