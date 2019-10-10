// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Chaos/Transform.h"
#include "Chaos/Vector.h"

namespace Chaos
{
	template<class T, int d>
	class TPBDJointConstraints;

	template<class T, int d>
	class TPBDJointConstraintHandle;

	template<typename T, int d>
	using TJointPreApplyCallback = TFunction<void(const T Dt, const TArray<TPBDJointConstraintHandle<T, d>*>& InConstraintHandles)>;

	template<typename T, int d>
	using TJointPostApplyCallback = TFunction<void(const T Dt, const TArray<TPBDJointConstraintHandle<T, d>*>& InConstraintHandles)>;

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

	template<class T, int d>
	struct TJointConstants
	{
		/** The constraint-space twist axis (X Axis) */
		static const TVector<T, d> TwistAxis() { return TVector<T, d>(1, 0, 0); }

		/** The constraint-space Swing1 axis (Z Axis) */
		static const TVector<T, d> Swing1Axis() { return TVector<T, d>(0, 0, 1); }

		/** The constraint-space Swing2 axis (Y Axis) */
		static const TVector<T, d> Swing2Axis() { return TVector<T, d>(0, 1, 0); }
	};

	template<class T, int d>
	class CHAOS_API TPBDJointMotionSettings
	{
	public:
		TPBDJointMotionSettings();
		TPBDJointMotionSettings(const TVector<EJointMotionType, d>& InLinearMotionTypes, const TVector<EJointMotionType, d>& InAngularMotionTypes);

		T Stiffness;

		TVector<EJointMotionType, d> LinearMotionTypes;
		T LinearLimit;

		TVector<EJointMotionType, d> AngularMotionTypes;
		TVector<T, d> AngularLimits;

		// @todo(ccaulfield): remove one of these
		TRotation<T, d> AngularDriveTarget;
		TVector<T, d> AngularDriveTargetAngles;

		bool bAngularSLerpDriveEnabled;
		bool bAngularTwistDriveEnabled;
		bool bAngularSwingDriveEnabled;

		T AngularDriveStiffness;
		T AngularDriveDamping;
	};


	template<class T, int d>
	class CHAOS_API TPBDJointSettings
	{
	public:
		using FTransformPair = TVector<TRigidTransform<T, d>, 2>;

		TPBDJointSettings();

		// Particle-relative joint axes and positions
		FTransformPair ConstraintFrames;

		// How the constraint is allowed to move
		TPBDJointMotionSettings<T, d> Motion;
	};

	template<class T, int d>
	class CHAOS_API TPBDJointSolverSettings
	{
	public:
		TPBDJointSolverSettings();

		// Tolerances
		T SwingTwistAngleTolerance;

		// Stability control
		T PBDMinParentMassRatio;
		T PBDMaxInertiaRatio;
		int32 FreezeIterations;
		int32 FrozenIterations;

		// @todo(ccaulfield): remove these TEMP overrides for testing
		bool bEnableLinearLimits;
		bool bEnableTwistLimits;
		bool bEnableSwingLimits;
		bool bEnableDrives;
		T PBDDriveStiffness;
	};
}