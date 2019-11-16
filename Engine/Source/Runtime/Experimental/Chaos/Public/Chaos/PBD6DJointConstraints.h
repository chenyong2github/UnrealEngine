// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Chaos/Core.h"
#include "Chaos/Array.h"
#include "Chaos/Transform.h"
#include "Chaos/Vector.h"

#include "Chaos/ConstraintHandle.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/PBDConstraintContainer.h"

namespace Chaos
{
	class FPBD6DJointConstraints;

	class CHAOS_API FPBD6DJointConstraintHandle : public TContainerConstraintHandle<FPBD6DJointConstraints>
	{
	public:
		using Base = TContainerConstraintHandle<FPBD6DJointConstraints>;
		using FConstraintContainer = FPBD6DJointConstraints;

		FPBD6DJointConstraintHandle();
		FPBD6DJointConstraintHandle(FConstraintContainer* InConstraintContainer, int32 InConstraintIndex);

		void CalculateConstraintSpace(FVec3& OutXa, FMatrix33& OutRa, FVec3& OutXb, FMatrix33& OutRb, FVec3& OutCR) const;
		void SetParticleLevels(const TVector<int32, 2>& ParticleLevels);
		int32 GetConstraintLevel() const;
		TVector<TGeometryParticleHandle<float,3>*, 2> GetConstrainedParticles() const;
		
	protected:
		using Base::ConstraintIndex;
		using Base::ConstraintContainer;
	};

	using FD6JointPostApplyCallback = TFunction<void(const FReal Dt, const TArray<FPBD6DJointConstraintHandle*>& InConstraintHandles)>;

	using FD6JointPreApplyCallback = TFunction<void(const FReal Dt, const TArray<FPBD6DJointConstraintHandle*>& InConstraintHandles)>;

	enum class E6DJointMotionType : int32
	{
		Free,
		Limited,
		Locked,
	};

	/**
	 * The order of the angular constraints in 6dof solver, if present.
	 */
	enum class E6DJointAngularConstraintIndex : int32
	{
		Twist,
		Swing2,
		Swing1,
	};

	/**
	 * The constraint-space axis about which each rotation constraint is applied
	 */
	enum class E6DJointAngularAxisIndex : int32
	{
		Twist = 0,		// Twist Axis = X
		Swing2 = 1,		// Swing2 Axis = Y
		Swing1 = 2,		// Swing1 Axis = Z
	};

	enum class E6DJointAngularMotorIndex : int32
	{
		Twist,
		Swing,
		Slerp,
	};


	struct F6DJointConstants
	{
		/** The constraint-space twist axis (X Axis) */
		static const FVec3 TwistAxis() { return FVec3(1, 0, 0); }

		/** The constraint-space Swing1 axis (Z Axis) */
		static const FVec3 Swing1Axis() { return FVec3(0, 0, 1); }

		/** The constraint-space Swing2 axis (Y Axis) */
		static const FVec3 Swing2Axis() { return FVec3(0, 1, 0); }
	};

	class CHAOS_API FPBD6DJointMotionSettings
	{
	public:
		FPBD6DJointMotionSettings();
		FPBD6DJointMotionSettings(const TVector<E6DJointMotionType, 3>& InLinearMotionTypes, const TVector<E6DJointMotionType, 3>& InAngularMotionTypes);

		FReal Stiffness;

		TVector<E6DJointMotionType, 3> LinearMotionTypes;
		FVec3 LinearLimits;

		TVector<E6DJointMotionType, 3> AngularMotionTypes;
		FVec3 AngularLimits;

		// @todo(ccaulfield): remove one of these
		FRotation3 AngularDriveTarget;
		FVec3 AngularDriveTargetAngles;

		bool bAngularSLerpDriveEnabled;
		bool bAngularTwistDriveEnabled;
		bool bAngularSwingDriveEnabled;

		FReal AngularDriveStiffness;
		FReal AngularDriveDamping;
	};


	class CHAOS_API FPBD6DJointSettings
	{
	public:
		using FTransformPair = TVector<FRigidTransform3, 2>;

		FPBD6DJointSettings();

		// Particle-relative joint axes and positions
		FTransformPair ConstraintFrames;

		// How the constraint is allowed to move
		FPBD6DJointMotionSettings Motion;
	};

	class CHAOS_API FPBD6DJointState
	{
	public:
		FPBD6DJointState();

		// XPBD state lambda (See XPBD paper for info)
		// This needs to be initialized each frame before we start iterating, but we only know
		// some of the values during the iteration, hence the initialize flag.
		FVec3 LambdaXa;
		FVec3 LambdaRa;
		FVec3 LambdaXb;
		FVec3 LambdaRb;
		FVec3 PrevTickCX;
		FVec3 PrevTickCR;
		FVec3 PrevItCX;
		FVec3 PrevItCR;

		// Priorities used for ordering, mass conditioning, projection, and freezing
		int32 Level;
		TVector<int32, 2> ParticleLevels;
	};

	class CHAOS_API FPBD6DJointSolverSettings
	{
	public:
		FPBD6DJointSolverSettings();

		// Tolerances
		FReal SolveTolerance;
		FReal InvertedAxisTolerance;
		FReal SwingTwistAngleTolerance;

		// Stability control
		bool bApplyProjection;
		int32 MaxIterations;
		int32 MaxPreIterations;
		int32 MaxDriveIterations;
		FReal MaxRotComponent;
		FReal PBDMinParentMassRatio;
		FReal PBDMaxInertiaRatio;
		int32 FreezeIterations;
		int32 FrozenIterations;

		// @todo(ccaulfield): remove these TEMP overrides for testing
		bool bEnableAutoStiffness;
		bool bEnableTwistLimits;
		bool bEnableSwingLimits;
		bool bEnableDrives;
		FReal XPBDAlphaX;
		FReal XPBDAlphaR;
		FReal XPBDBetaX;
		FReal XPBDBetaR;
		FReal PBDStiffness;
		FReal PBDDriveStiffness;

		bool bFastSolve;
	};

	/**
	 * A joint restricting up to 6 degrees of freedom, with linear and angular limits.
	 */
	class CHAOS_API FPBD6DJointConstraints : public FPBDConstraintContainer
	{
	public:
		using Base = FPBDConstraintContainer;
		using FConstraintContainerHandle = FPBD6DJointConstraintHandle;
		using FConstraintHandleAllocator = TConstraintHandleAllocator<FPBD6DJointConstraints>;
		using FParticlePair = TVector<TGeometryParticleHandle<FReal, 3>*, 2>;
		using FVectorPair = TVector<FVec3, 2>;
		using FTransformPair = TVector<FRigidTransform3, 2>;
		using FHandles = TArray<FConstraintContainerHandle*>;

		FPBD6DJointConstraints(const FPBD6DJointSolverSettings& InSettings);

		virtual ~FPBD6DJointConstraints();

		const FPBD6DJointSolverSettings& GetSettings() const;
		void SetSettings(const FPBD6DJointSolverSettings& InSettings);

		//
		// Constraint Container API
		//

		/**
		 * Get the number of constraints.
		 */
		int32 NumConstraints() const;

		/** 
		 * Add a constraint with particle-space constraint offsets. 
		 */
		FConstraintContainerHandle* AddConstraint(const FParticlePair& InConstrainedParticles, const FTransformPair& ConstraintFrames);
		FConstraintContainerHandle* AddConstraint(const FParticlePair& InConstrainedParticles, const FPBD6DJointSettings& InConstraintSettings);

		/**
		 * Remove the specified constraint.
		 */
		void RemoveConstraint(int ConstraintIndex);

		// @todo(ccaulfield): rename/remove  this
		void RemoveConstraints(const TSet<TGeometryParticleHandle<FReal, 3>*>& RemovedParticles);

		void SetPreApplyCallback(const FD6JointPostApplyCallback& Callback);
		void ClearPreApplyCallback();

		void SetPostApplyCallback(const FD6JointPostApplyCallback& Callback);
		void ClearPostApplyCallback();

		//
		// Constraint API
		//
		FHandles& GetConstraintHandles()
		{
			return Handles;
		}
		const FHandles& GetConstConstraintHandles() const
		{
			return Handles;
		}

		const FConstraintContainerHandle* GetConstraintHandle(int32 ConstraintIndex) const;
		FConstraintContainerHandle* GetConstraintHandle(int32 ConstraintIndex);


		/**
		 * Get the particles that are affected by the specified constraint.
		 */
		const FParticlePair& GetConstrainedParticles(int32 ConstraintIndex) const;

		int32 GetConstraintLevel(int32 ConstraintIndex) const;
		void SetParticleLevels(int32 ConstraintIndex, const TVector<int32, 2>& ParticleLevels);

		//
		// Island Rule API
		//

		void UpdatePositionBasedState(const FReal Dt);

		void Apply(const FReal Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const int32 It, const int32 NumIts);

		bool ApplyPushOut(const FReal Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const int32 It, const int32 NumIts);

	protected:
		using Base::GetConstraintIndex;
		using Base::SetConstraintIndex;

	private:
		friend class FPBD6DJointConstraintHandle;

		void CalculateConstraintSpace(int32 ConstraintIndex, FVec3& OutXa, FMatrix33& OutRa, FVec3& OutXb, FMatrix33& OutRb, FVec3& OutCR) const;

		void ApplySingle(const FReal Dt, const int32 ConstraintIndex, const FReal FreezeScale);
		void ApplyDynamicStatic(const FReal Dt, const int32 ConstraintIndex, const int32 PBDRigid0Index, const int32 Static1Index);
		void ApplyDynamicDynamic(const FReal Dt, const int32 ConstraintIndex, const int32 PBDRigid0Index, const int32 PBDRigid1Index, const FReal FreezeScale);

		void ApplyPushOutSingle(const FReal Dt, const int32 ConstraintIndex);

		void ApplySingleFast(const FReal Dt, const int32 ConstraintIndex, const int32 It, const int32 NumIts);

		FPBD6DJointSolverSettings Settings;

		TArray<FPBD6DJointSettings> ConstraintSettings;
		TArray<FParticlePair> ConstraintParticles;
		TArray<FPBD6DJointState> ConstraintStates;

		FHandles Handles;
		FConstraintHandleAllocator HandleAllocator;

		FD6JointPreApplyCallback PreApplyCallback;
		FD6JointPostApplyCallback PostApplyCallback;
	};

	class CHAOS_API FPBD6DJointConstraintUtilities
	{
	public:
		static void BlockwiseInverse(const FMatrix33& A, const FMatrix33& B, const FMatrix33& C, const FMatrix33& D, FMatrix33& AI, FMatrix33& BI, FMatrix33& CI, FMatrix33& DI);
		static void BlockwiseInverse2(const FMatrix33& A, const FMatrix33& B, const FMatrix33& C, const FMatrix33& D, FMatrix33& AI, FMatrix33& BI, FMatrix33& CI, FMatrix33& DI);
		
		static void ComputeJointFactorMatrix(const FMatrix33& XR, const FMatrix33& RR, float MInv, const FMatrix33& IInv, FMatrix33& M00, FMatrix33& M01, FMatrix33& M10, FMatrix33& M11);

		static FVec3 Calculate6dConstraintAngles(
			const FPBD6DJointSolverSettings& SolverSettings,
			const FRotation3& Ra,
			const FRotation3& Rb, 
			const FPBD6DJointMotionSettings& MotionSettings);
		
		static bool Calculate6dConstraintRotation(
			const FPBD6DJointSolverSettings& SolverSettings,
			const FRotation3& Ra,
			const FRotation3& Rb, 
			const FPBD6DJointMotionSettings& MotionSettings,
			FVec3& CR, 
			FMatrix33& RRa, 
			FMatrix33& RRb);
		
		static void Calculate6dConstraintRotationLimits(
			const FPBD6DJointSolverSettings& SolverSettings,
			const FRotation3& Ra,
			const FRotation3& Rb, 
			const FPBD6DJointMotionSettings& MotionSettings,
			FVec3& SR,
			FVec3& CR,
			FMatrix33& RRa, 
			FMatrix33& RRb, 
			FVec3& LRMin, 
			FVec3& LRMax);

		static bool Calculate6dConstraintRotation_SwingCone(
			const FPBD6DJointSolverSettings& SolverSettings,
			const FRotation3& Ra,
			const FRotation3& Rb, 
			const FPBD6DJointMotionSettings& MotionSettings,
			FVec3& CR, 
			FMatrix33& RRa, 
			FMatrix33& RRb);

		static void Calculate6dConstraintRotationLimits_SwingCone(
			const FPBD6DJointSolverSettings& SolverSettings,
			const FRotation3& Ra,
			const FRotation3& Rb, 
			const FPBD6DJointMotionSettings& MotionSettings,
			FVec3& SR,
			FVec3& CR,
			FMatrix33& RRa,
			FMatrix33& RRb, 
			FVec3& LRMin, 
			FVec3& LRMax);

		static bool Calculate6dConstraintRotation_SwingFixed(
			const FPBD6DJointSolverSettings& SolverSettings,
			const FRotation3& Ra,
			const FRotation3& Rb, 
			const FPBD6DJointMotionSettings& MotionSettings,
			FVec3& CR, 
			FMatrix33& RRa, 
			FMatrix33& RRb);

		static void Calculate6dConstraintRotationLimits_SwingFixed(
			const FPBD6DJointSolverSettings& SolverSettings,
			const FRotation3& Ra,
			const FRotation3& Rb, 
			const FPBD6DJointMotionSettings& MotionSettings,
			FVec3& SR,
			FVec3& CR,
			FMatrix33& RRa, 
			FMatrix33& RRb, 
			FVec3& LRMin, 
			FVec3& LRMax);

		static bool Calculate6dDelta(
			const FPBD6DJointSolverSettings& SolverSettings,
			const FReal Dt,
			const FVec3& Pa,
			const FRotation3& Qa,
			float MaInv,
			const FMatrix33& IaInv,
			const FVec3& Pb,
			const FRotation3& Qb,
			float MbInv,
			const FMatrix33& IbInv,
			const FVec3& Xa,
			const FRotation3& Ra,
			const FVec3& Xb,
			const FRotation3& Rb,
			const FPBD6DJointMotionSettings& MotionSettings,
			FPBD6DJointState& State,
			FVec3& DPa,
			FRotation3& DQa,
			FVec3& DPb,
			FRotation3& DQb);

		static int Solve6dConstraint(
			const FPBD6DJointSolverSettings& SolverSettings,
			const FReal Dt,
			FVec3& Pa,
			FRotation3& Qa,
			FReal MaInv,
			const FMatrix33& ILaInv,
			const FVec3& XLa,
			const FRotation3& RLa,
			FVec3& Pb,
			FRotation3& Qb,
			FReal MbInv,
			const FMatrix33& ILbInv,
			const FVec3& XLb,
			const FRotation3& RLb,
			const FPBD6DJointMotionSettings& MotionSettings,
			FPBD6DJointState& State);


		static void Calculate3dDelta(
			const FPBD6DJointSolverSettings& SolverSettings,
			const FVec3& Pa,
			const FRotation3& Qa,
			FReal MaInv,
			const FMatrix33& IaInv,
			const FVec3& Pb,
			const FRotation3& Qb,
			FReal MbInv,
			const FMatrix33& IbInv,
			const FVec3& Xa,
			const FVec3& Xb,
			const FPBD6DJointMotionSettings& XSettings,
			FVec3& DPa,
			FRotation3& DQa,
			FVec3& DPb,
			FRotation3& DQb);

		static void Solve3dConstraint(
			const FPBD6DJointSolverSettings& SolverSettings,
			FVec3& Pa,
			FRotation3& Qa,
			FReal MaInv,
			const FMatrix33& ILaInv,
			const FVec3& XLa,
			const FRotation3& RLa,
			FVec3& Pb,
			FRotation3& Qb,
			FReal MbInv,
			const FMatrix33& ILbInv,
			const FVec3& XLb,
			const FRotation3& RLb,
			const FPBD6DJointMotionSettings& MotionSettings);
	};

}
