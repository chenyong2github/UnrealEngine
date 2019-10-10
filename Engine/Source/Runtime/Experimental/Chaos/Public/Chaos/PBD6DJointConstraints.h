// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Chaos/Array.h"
#include "Chaos/Transform.h"
#include "Chaos/Vector.h"

#include "Chaos/ConstraintHandle.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/PBDConstraintContainer.h"

namespace Chaos
{
	template<class T, int d>
	class TPBD6DJointConstraints;

	template<class T, int d>
	class TPBD6DJointConstraintHandle : public TContainerConstraintHandle<TPBD6DJointConstraints<T, d>>
	{
	public:
		using Base = TContainerConstraintHandle<TPBD6DJointConstraints<T, d>>;
		using FConstraintContainer = TPBD6DJointConstraints<T, d>;

		CHAOS_API TPBD6DJointConstraintHandle();
		CHAOS_API TPBD6DJointConstraintHandle(FConstraintContainer* InConstraintContainer, int32 InConstraintIndex);

		CHAOS_API void CalculateConstraintSpace(TVector<T, d>& OutXa, PMatrix<T, d, d>& OutRa, TVector<T, d>& OutXb, PMatrix<T, d, d>& OutRb, TVector<T, d>& OutCR) const;
		CHAOS_API void SetParticleLevels(const TVector<int32, 2>& ParticleLevels);
		CHAOS_API int32 GetConstraintLevel() const;

	protected:
		using Base::ConstraintIndex;
		using Base::ConstraintContainer;
	};

	template<typename T, int d>
	using TD6JointPostApplyCallback = TFunction<void(const T Dt, const TArray<TPBD6DJointConstraintHandle<T, d>*>& InConstraintHandles)>;

	template<typename T, int d>
	using TD6JointPreApplyCallback = TFunction<void(const T Dt, const TArray<TPBD6DJointConstraintHandle<T, d>*>& InConstraintHandles)>;

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


	template<class T, int d>
	struct T6DJointConstants
	{
		/** The constraint-space twist axis (X Axis) */
		static const TVector<T, d> TwistAxis() { return TVector<T, d>(1, 0, 0); }

		/** The constraint-space Swing1 axis (Z Axis) */
		static const TVector<T, d> Swing1Axis() { return TVector<T, d>(0, 0, 1); }

		/** The constraint-space Swing2 axis (Y Axis) */
		static const TVector<T, d> Swing2Axis() { return TVector<T, d>(0, 1, 0); }
	};

	template<class T, int d>
	class CHAOS_API TPBD6DJointMotionSettings
	{
	public:
		TPBD6DJointMotionSettings();
		TPBD6DJointMotionSettings(const TVector<E6DJointMotionType, d>& InLinearMotionTypes, const TVector<E6DJointMotionType, d>& InAngularMotionTypes);

		T Stiffness;

		TVector<E6DJointMotionType, d> LinearMotionTypes;
		TVector<T, d> LinearLimits;

		TVector<E6DJointMotionType, d> AngularMotionTypes;
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
	class CHAOS_API TPBD6DJointSettings
	{
	public:
		using FTransformPair = TVector<TRigidTransform<T, d>, 2>;

		TPBD6DJointSettings();

		// Particle-relative joint axes and positions
		FTransformPair ConstraintFrames;

		// How the constraint is allowed to move
		TPBD6DJointMotionSettings<T, d> Motion;
	};

	template<class T, int d>
	class CHAOS_API TPBD6DJointState
	{
	public:
		TPBD6DJointState();

		// XPBD state lambda (See XPBD paper for info)
		// This needs to be initialized each frame before we start iterating, but we only know
		// some of the values during the iteration, hence the initialize flag.
		TVector<T, d> LambdaXa;
		TVector<T, d> LambdaRa;
		TVector<T, d> LambdaXb;
		TVector<T, d> LambdaRb;
		TVector<T, d> PrevTickCX;
		TVector<T, d> PrevTickCR;
		TVector<T, d> PrevItCX;
		TVector<T, d> PrevItCR;

		// Priorities used for ordering, mass conditioning, projection, and freezing
		int32 Level;
		TVector<int32, 2> ParticleLevels;
	};

	template<class T, int d>
	class CHAOS_API TPBD6DJointSolverSettings
	{
	public:
		TPBD6DJointSolverSettings();

		// Tolerances
		T SolveTolerance;
		T InvertedAxisTolerance;
		T SwingTwistAngleTolerance;

		// Stability control
		bool bApplyProjection;
		int32 MaxIterations;
		int32 MaxPreIterations;
		int32 MaxDriveIterations;
		T MaxRotComponent;
		T PBDMinParentMassRatio;
		T PBDMaxInertiaRatio;
		int32 FreezeIterations;
		int32 FrozenIterations;

		// @todo(ccaulfield): remove these TEMP overrides for testing
		bool bEnableAutoStiffness;
		bool bEnableTwistLimits;
		bool bEnableSwingLimits;
		bool bEnableDrives;
		T XPBDAlphaX;
		T XPBDAlphaR;
		T XPBDBetaX;
		T XPBDBetaR;
		T PBDStiffness;
		T PBDDriveStiffness;

		bool bFastSolve;
	};

	/**
	 * A joint restricting up to 6 degrees of freedom, with linear and angular limits.
	 */
	template<class T, int d>
	class TPBD6DJointConstraints : public TPBDConstraintContainer<T, d>
	{
	public:
		using Base = TPBDConstraintContainer<T, d>;
		using FReal = T;
		static const int Dimensions = d;
		using FConstraintHandle = TPBD6DJointConstraintHandle<FReal, Dimensions>;
		using FConstraintHandleAllocator = TConstraintHandleAllocator<TPBD6DJointConstraints<FReal, Dimensions>>;
		using FParticlePair = TVector<TGeometryParticleHandle<T, d>*, 2>;
		using FVectorPair = TVector<TVector<T, d>, 2>;
		using FTransformPair = TVector<TRigidTransform<T, d>, 2>;
		using FJointSettings = TPBD6DJointSettings<T, d>;
		using FJointState = TPBD6DJointState<T, d>;

		CHAOS_API TPBD6DJointConstraints(const TPBD6DJointSolverSettings<T, d>& InSettings);

		virtual ~TPBD6DJointConstraints();

		CHAOS_API const TPBD6DJointSolverSettings<T, d>& GetSettings() const;
		CHAOS_API void SetSettings(const TPBD6DJointSolverSettings<T, d>& InSettings);

		//
		// Constraint Container API
		//

		/**
		 * Get the number of constraints.
		 */
		CHAOS_API int32 NumConstraints() const;

		/** 
		 * Add a constraint with particle-space constraint offsets. 
		 */
		CHAOS_API FConstraintHandle* AddConstraint(const FParticlePair& InConstrainedParticles, const FTransformPair& ConstraintFrames);
		CHAOS_API FConstraintHandle* AddConstraint(const FParticlePair& InConstrainedParticles, const TPBD6DJointSettings<T, d>& InConstraintSettings);

		/**
		 * Remove the specified constraint.
		 */
		CHAOS_API void RemoveConstraint(int ConstraintIndex);

		// @todo(ccaulfield): rename/remove  this
		CHAOS_API void RemoveConstraints(const TSet<TGeometryParticleHandle<T, d>*>& RemovedParticles);

		CHAOS_API void SetPreApplyCallback(const TD6JointPostApplyCallback<T, d>& Callback);
		CHAOS_API void ClearPreApplyCallback();

		CHAOS_API void SetPostApplyCallback(const TD6JointPostApplyCallback<T, d>& Callback);
		CHAOS_API void ClearPostApplyCallback();

		//
		// Constraint API
		//

		CHAOS_API const FConstraintHandle* GetConstraintHandle(int32 ConstraintIndex) const;
		CHAOS_API FConstraintHandle* GetConstraintHandle(int32 ConstraintIndex);

		/**
		 * Get the particles that are affected by the specified constraint.
		 */
		CHAOS_API const FParticlePair& GetConstrainedParticles(int32 ConstraintIndex) const;

		int32 GetConstraintLevel(int32 ConstraintIndex) const;
		void SetParticleLevels(int32 ConstraintIndex, const TVector<int32, 2>& ParticleLevels);

		//
		// Island Rule API
		//

		CHAOS_API void UpdatePositionBasedState(const T Dt);

		CHAOS_API void Apply(const T Dt, const TArray<FConstraintHandle*>& InConstraintHandles, const int32 It, const int32 NumIts);

		// @todo(ccaulfield): remove  this
		CHAOS_API void ApplyPushOut(const T Dt, const TArray<FConstraintHandle*>& InConstraintHandles);

	protected:
		using Base::GetConstraintIndex;
		using Base::SetConstraintIndex;

	private:
		friend class TPBD6DJointConstraintHandle<T, d>;

		CHAOS_API void CalculateConstraintSpace(int32 ConstraintIndex, TVector<T, d>& OutXa, PMatrix<T, d, d>& OutRa, TVector<T, d>& OutXb, PMatrix<T, d, d>& OutRb, TVector<T, d>& OutCR) const;

		CHAOS_API void ApplySingle(const T Dt, const int32 ConstraintIndex, const T FreezeScale);
		CHAOS_API void ApplyDynamicStatic(const T Dt, const int32 ConstraintIndex, const int32 PBDRigid0Index, const int32 Static1Index);
		CHAOS_API void ApplyDynamicDynamic(const T Dt, const int32 ConstraintIndex, const int32 PBDRigid0Index, const int32 PBDRigid1Index, const T FreezeScale);

		CHAOS_API void ApplyPushOutSingle(const T Dt, const int32 ConstraintIndex);

		CHAOS_API void ApplySingleFast(const T Dt, const int32 ConstraintIndex, const int32 It, const int32 NumIts);

		TPBD6DJointSolverSettings<T, d> Settings;

		TArray<FJointSettings> ConstraintSettings;
		TArray<FParticlePair> ConstraintParticles;
		TArray<FJointState> ConstraintStates;

		TArray<FConstraintHandle*> Handles;
		FConstraintHandleAllocator HandleAllocator;

		TD6JointPreApplyCallback<T, d> PreApplyCallback;
		TD6JointPostApplyCallback<T, d> PostApplyCallback;
	};

	template<class T, int d>
	class CHAOS_API TPBD6DJointConstraintUtilities
	{
	public:
		static void BlockwiseInverse(const PMatrix<T, d, d>& A, const PMatrix<T, d, d>& B, const PMatrix<T, d, d>& C, const PMatrix<T, d, d>& D, PMatrix<T, d, d>& AI, PMatrix<T, d, d>& BI, PMatrix<T, d, d>& CI, PMatrix<T, d, d>& DI);
		static void BlockwiseInverse2(const PMatrix<T, d, d>& A, const PMatrix<T, d, d>& B, const PMatrix<T, d, d>& C, const PMatrix<T, d, d>& D, PMatrix<T, d, d>& AI, PMatrix<T, d, d>& BI, PMatrix<T, d, d>& CI, PMatrix<T, d, d>& DI);
		
		static void ComputeJointFactorMatrix(const PMatrix<T, d, d>& XR, const PMatrix<T, d, d>& RR, float MInv, const PMatrix<T, d, d>& IInv, PMatrix<T, d, d>& M00, PMatrix<T, d, d>& M01, PMatrix<T, d, d>& M10, PMatrix<T, d, d>& M11);

		static TVector<T, d> Calculate6dConstraintAngles(
			const TPBD6DJointSolverSettings<T, d>& SolverSettings,
			const TRotation<T, d>& Ra,
			const TRotation<T, d>& Rb, 
			const TPBD6DJointMotionSettings<T, d>& MotionSettings);
		
		static bool Calculate6dConstraintRotation(
			const TPBD6DJointSolverSettings<T, d>& SolverSettings,
			const TRotation<T, d>& Ra,
			const TRotation<T, d>& Rb, 
			const TPBD6DJointMotionSettings<T, d>& MotionSettings,
			TVector<T, d>& CR, 
			PMatrix<T, d, d>& RRa, 
			PMatrix<T, d, d>& RRb);
		
		static void Calculate6dConstraintRotationLimits(
			const TPBD6DJointSolverSettings<T, d>& SolverSettings,
			const TRotation<T, d>& Ra,
			const TRotation<T, d>& Rb, 
			const TPBD6DJointMotionSettings<T, d>& MotionSettings,
			TVector<T, d>& SR,
			TVector<T, d>& CR,
			PMatrix<T, d, d>& RRa, 
			PMatrix<T, d, d>& RRb, 
			TVector<T, d>& LRMin, 
			TVector<T, d>& LRMax);

		static bool Calculate6dConstraintRotation_SwingCone(
			const TPBD6DJointSolverSettings<T, d>& SolverSettings,
			const TRotation<T, d>& Ra,
			const TRotation<T, d>& Rb, 
			const TPBD6DJointMotionSettings<T, d>& MotionSettings,
			TVector<T, d>& CR, 
			PMatrix<T, d, d>& RRa, 
			PMatrix<T, d, d>& RRb);

		static void Calculate6dConstraintRotationLimits_SwingCone(
			const TPBD6DJointSolverSettings<T, d>& SolverSettings,
			const TRotation<T, d>& Ra,
			const TRotation<T, d>& Rb, 
			const TPBD6DJointMotionSettings<T, d>& MotionSettings,
			TVector<T, d>& SR,
			TVector<T, d>& CR,
			PMatrix<T, d, d>& RRa,
			PMatrix<T, d, d>& RRb, 
			TVector<T, d>& LRMin, 
			TVector<T, d>& LRMax);

		static bool Calculate6dConstraintRotation_SwingFixed(
			const TPBD6DJointSolverSettings<T, d>& SolverSettings,
			const TRotation<T, d>& Ra,
			const TRotation<T, d>& Rb, 
			const TPBD6DJointMotionSettings<T, d>& MotionSettings,
			TVector<T, d>& CR, 
			PMatrix<T, d, d>& RRa, 
			PMatrix<T, d, d>& RRb);

		static void Calculate6dConstraintRotationLimits_SwingFixed(
			const TPBD6DJointSolverSettings<T, d>& SolverSettings,
			const TRotation<T, d>& Ra,
			const TRotation<T, d>& Rb, 
			const TPBD6DJointMotionSettings<T, d>& MotionSettings,
			TVector<T, d>& SR,
			TVector<T, d>& CR,
			PMatrix<T, d, d>& RRa, 
			PMatrix<T, d, d>& RRb, 
			TVector<T, d>& LRMin, 
			TVector<T, d>& LRMax);

		static bool Calculate6dDelta(
			const TPBD6DJointSolverSettings<T, d>& SolverSettings,
			const T Dt,
			const TVector<T, d>& Pa,
			const TRotation<T, d>& Qa,
			float MaInv,
			const PMatrix<T, d, d>& IaInv,
			const TVector<T, d>& Pb,
			const TRotation<T, d>& Qb,
			float MbInv,
			const PMatrix<T, d, d>& IbInv,
			const TVector<T, d>& Xa,
			const TRotation<T, d>& Ra,
			const TVector<T, d>& Xb,
			const TRotation<T, d>& Rb,
			const TPBD6DJointMotionSettings<T, d>& MotionSettings,
			TPBD6DJointState<T, d>& State,
			TVector<T, d>& DPa,
			TRotation<T, d>& DQa,
			TVector<T, d>& DPb,
			TRotation<T, d>& DQb);

		static int Solve6dConstraint(
			const TPBD6DJointSolverSettings<T, d>& SolverSettings,
			const T Dt,
			TVector<T, d>& Pa,
			TRotation<T, d>& Qa,
			T MaInv,
			const PMatrix<T, d, d>& ILaInv,
			const TVector<T, d>& XLa,
			const TRotation<T, d>& RLa,
			TVector<T, d>& Pb,
			TRotation<T, d>& Qb,
			T MbInv,
			const PMatrix<T, d, d>& ILbInv,
			const TVector<T, d>& XLb,
			const TRotation<T, d>& RLb,
			const TPBD6DJointMotionSettings<T, d>& MotionSettings,
			TPBD6DJointState<T, d>& State);


		static void Calculate3dDelta(
			const TPBD6DJointSolverSettings<T, d>& SolverSettings,
			const TVector<T, d>& Pa,
			const TRotation<T, d>& Qa,
			float MaInv,
			const PMatrix<T, d, d>& IaInv,
			const TVector<T, d>& Pb,
			const TRotation<T, d>& Qb,
			float MbInv,
			const PMatrix<T, d, d>& IbInv,
			const TVector<T, d>& Xa,
			const TVector<T, d>& Xb,
			const TPBD6DJointMotionSettings<T, d>& XSettings,
			TVector<T, d>& DPa,
			TRotation<T, d>& DQa,
			TVector<T, d>& DPb,
			TRotation<T, d>& DQb);

		static void Solve3dConstraint(
			const TPBD6DJointSolverSettings<T, d>& SolverSettings,
			TVector<T, d>& Pa,
			TRotation<T, d>& Qa,
			T MaInv,
			const PMatrix<T, d, d>& ILaInv,
			const TVector<T, d>& XLa,
			const TRotation<T, d>& RLa,
			TVector<T, d>& Pb,
			TRotation<T, d>& Qb,
			T MbInv,
			const PMatrix<T, d, d>& ILbInv,
			const TVector<T, d>& XLb,
			const TRotation<T, d>& RLb,
			const TPBD6DJointMotionSettings<T, d>& MotionSettings);
	};

}
