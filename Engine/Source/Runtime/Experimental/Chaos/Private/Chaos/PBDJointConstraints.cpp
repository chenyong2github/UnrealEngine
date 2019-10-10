// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/ChaosDebugDraw.h"
#include "Chaos/DebugDrawQueue.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDJointConstraintUtilities.h"
#include "Chaos/Utilities.h"
#include "ChaosLog.h"

#include "HAL/IConsoleManager.h"


namespace Chaos
{
	//
	// Constraint Handle
	//

	template<typename T, int d>
	TPBDJointConstraintHandle<T, d>::TPBDJointConstraintHandle()
	{
	}

	template<typename T, int d>
	TPBDJointConstraintHandle<T, d>::TPBDJointConstraintHandle(FConstraintContainer* InConstraintContainer, int32 InConstraintIndex) : TContainerConstraintHandle<TPBDJointConstraints<T, d>>(InConstraintContainer, InConstraintIndex)
	{
	}

	template<typename T, int d>
	void TPBDJointConstraintHandle<T, d>::CalculateConstraintSpace(TVector<T, d>& OutXa, PMatrix<T, d, d>& OutRa, TVector<T, d>& OutXb, PMatrix<T, d, d>& OutRb, TVector<T, d>& OutCR) const
	{
		ConstraintContainer->CalculateConstraintSpace(ConstraintIndex, OutXa, OutRa, OutXb, OutRb, OutCR);
	}

	template<typename T, int d>
	void TPBDJointConstraintHandle<T, d>::SetParticleLevels(const TVector<int32, 2>& ParticleLevels)
	{
		ConstraintContainer->SetParticleLevels(ConstraintIndex, ParticleLevels);
	}

	template<typename T, int d>
	int32 TPBDJointConstraintHandle<T, d>::GetConstraintLevel() const
	{
		return ConstraintContainer->GetConstraintLevel(ConstraintIndex);
	}

	template<typename T, int d>
	const TPBDJointSettings<T, d>& TPBDJointConstraintHandle<T, d>::GetSettings() const
	{
		return ConstraintContainer->GetConstraintSettings(ConstraintIndex);
	}

	//
	// Constraint Settings
	//

	template<class T, int d>
	TPBDJointMotionSettings<T, d>::TPBDJointMotionSettings()
		: Stiffness((T)1)
		, LinearMotionTypes({ EJointMotionType::Locked, EJointMotionType::Locked, EJointMotionType::Locked })
		, LinearLimit(FLT_MAX)
		, AngularMotionTypes({ EJointMotionType::Free, EJointMotionType::Free, EJointMotionType::Free })
		, AngularLimits(TVector<T, d>(FLT_MAX, FLT_MAX, FLT_MAX))
		, AngularDriveTarget(TRotation<T, d>::FromIdentity())
		, AngularDriveTargetAngles(TVector<T, d>(0, 0, 0))
		, bAngularSLerpDriveEnabled(false)
		, bAngularTwistDriveEnabled(false)
		, bAngularSwingDriveEnabled(false)
		, AngularDriveStiffness(0)
		, AngularDriveDamping(0)
	{
	}

	template<class T, int d>
	TPBDJointMotionSettings<T, d>::TPBDJointMotionSettings(const TVector<EJointMotionType, d>& InLinearMotionTypes, const TVector<EJointMotionType, d>& InAngularMotionTypes)
		: Stiffness((T)1)
		, LinearMotionTypes(InLinearMotionTypes)
		, LinearLimit(FLT_MAX)
		, AngularMotionTypes({ EJointMotionType::Free, EJointMotionType::Free, EJointMotionType::Free })
		, AngularLimits(TVector<T, d>(FLT_MAX, FLT_MAX, FLT_MAX))
		, AngularDriveTarget(TRotation<T, d>::FromIdentity())
		, AngularDriveTargetAngles(TVector<T, d>(0, 0, 0))
		, bAngularSLerpDriveEnabled(false)
		, bAngularTwistDriveEnabled(false)
		, bAngularSwingDriveEnabled(false)
		, AngularDriveStiffness(0)
		, AngularDriveDamping(0)
	{
	}

	template<class T, int d>
	TPBDJointSettings<T, d>::TPBDJointSettings()
		: ConstraintFrames({ FTransform::Identity, FTransform::Identity })
	{
	}

	template<class T, int d>
	TPBDJointState<T, d>::TPBDJointState()
		: Level(INDEX_NONE)
		, ParticleLevels({ INDEX_NONE, INDEX_NONE })
	{
	}

	//
	// Solver Settings
	//

	template<class T, int d>
	TPBDJointSolverSettings<T, d>::TPBDJointSolverSettings()
		: SwingTwistAngleTolerance(1.0e-6f)
		, PBDMinParentMassRatio(0.5f)
		, PBDMaxInertiaRatio(5.0f)
		, FreezeIterations(0)
		, FrozenIterations(0)
		, bEnableLinearLimits(1)
		, bEnableTwistLimits(1)
		, bEnableSwingLimits(1)
		, bEnableDrives(1)
		, PBDDriveStiffness(0.0f)
	{
	}

	//
	// Constraint Container
	//

	template<class T, int d>
	TPBDJointConstraints<T, d>::TPBDJointConstraints(const TPBDJointSolverSettings<T, d>& InSettings)
		: Settings(InSettings)
		, PreApplyCallback(nullptr)
		, PostApplyCallback(nullptr)
	{
	}

	template<class T, int d>
	TPBDJointConstraints<T, d>::~TPBDJointConstraints()
	{
	}

	template<class T, int d>
	const TPBDJointSolverSettings<T, d>& TPBDJointConstraints<T, d>::GetSettings() const
	{
		return Settings;
	}

	template<class T, int d>
	void TPBDJointConstraints<T, d>::SetSettings(const TPBDJointSolverSettings<T, d>& InSettings)
	{
		Settings = InSettings;
	}

	template<class T, int d>
	int32 TPBDJointConstraints<T, d>::NumConstraints() const
	{
		return ConstraintParticles.Num();
	}

	template<class T, int d>
	typename TPBDJointConstraints<T, d>::FConstraintHandle* TPBDJointConstraints<T, d>::AddConstraint(const FParticlePair& InConstrainedParticles, const TRigidTransform<FReal, Dimensions>& WorldConstraintFrame)
	{
		FTransformPair ConstraintFrames;
		ConstraintFrames[0] = TRigidTransform<FReal, Dimensions>(
			WorldConstraintFrame.GetTranslation() - InConstrainedParticles[0]->X(),
			WorldConstraintFrame.GetRotation() * InConstrainedParticles[0]->R().Inverse()
			);
		ConstraintFrames[1] = TRigidTransform<FReal, Dimensions>(
			WorldConstraintFrame.GetTranslation() - InConstrainedParticles[1]->X(),
			WorldConstraintFrame.GetRotation() * InConstrainedParticles[1]->R().Inverse()
			);
		return AddConstraint(InConstrainedParticles, ConstraintFrames);
	}

	template<class T, int d>
	typename TPBDJointConstraints<T, d>::FConstraintHandle* TPBDJointConstraints<T, d>::AddConstraint(const FParticlePair& InConstrainedParticles, const FTransformPair& ConstraintFrames)
	{
		int ConstraintIndex = Handles.Num();
		Handles.Add(HandleAllocator.AllocHandle(this, ConstraintIndex));
		ConstraintParticles.Add(InConstrainedParticles);
		ConstraintSettings.Add(FJointSettings());
		ConstraintSettings[ConstraintIndex].ConstraintFrames = ConstraintFrames;
		ConstraintStates.Add(FJointState());
		return Handles.Last();
	}

	template<class T, int d>
	typename TPBDJointConstraints<T, d>::FConstraintHandle* TPBDJointConstraints<T, d>::AddConstraint(const FParticlePair& InConstrainedParticles, const TPBDJointSettings<T, d>& InConstraintSettings)
	{
		int ConstraintIndex = Handles.Num();
		Handles.Add(HandleAllocator.AllocHandle(this, ConstraintIndex));
		ConstraintParticles.Add(InConstrainedParticles);
		ConstraintSettings.Add(InConstraintSettings);
		ConstraintStates.Add(FJointState());
		return Handles.Last();
	}

	template<class T, int d>
	void TPBDJointConstraints<T, d>::RemoveConstraint(int ConstraintIndex)
	{
		FConstraintHandle* ConstraintHandle = Handles[ConstraintIndex];
		if (ConstraintHandle != nullptr)
		{
			// Release the handle for the freed constraint
			HandleAllocator.FreeHandle(ConstraintHandle);
			Handles[ConstraintIndex] = nullptr;
		}

		// Swap the last constraint into the gap to keep the array packed
		ConstraintParticles.RemoveAtSwap(ConstraintIndex);
		ConstraintSettings.RemoveAtSwap(ConstraintIndex);
		ConstraintStates.RemoveAtSwap(ConstraintIndex);
		Handles.RemoveAtSwap(ConstraintIndex);

		// Update the handle for the constraint that was moved
		if (ConstraintIndex < Handles.Num())
		{
			SetConstraintIndex(Handles[ConstraintIndex], ConstraintIndex);
		}
	}

	template<class T, int d>
	void TPBDJointConstraints<T, d>::RemoveConstraints(const TSet<TGeometryParticleHandle<T, d>*>& RemovedParticles)
	{
	}


	template<class T, int d>
	void TPBDJointConstraints<T, d>::SetPreApplyCallback(const TJointPreApplyCallback<T, d>& Callback)
	{
		PreApplyCallback = Callback;
	}

	template<class T, int d>
	void TPBDJointConstraints<T, d>::ClearPreApplyCallback()
	{
		PreApplyCallback = nullptr;
	}

	template<class T, int d>
	void TPBDJointConstraints<T, d>::SetPostApplyCallback(const TJointPostApplyCallback<T, d>& Callback)
	{
		PostApplyCallback = Callback;
	}

	template<class T, int d>
	void TPBDJointConstraints<T, d>::ClearPostApplyCallback()
	{
		PostApplyCallback = nullptr;
	}

	template<class T, int d>
	const typename TPBDJointConstraints<T, d>::FConstraintHandle* TPBDJointConstraints<T, d>::GetConstraintHandle(int32 ConstraintIndex) const
	{
		return Handles[ConstraintIndex];
	}

	template<class T, int d>
	typename TPBDJointConstraints<T, d>::FConstraintHandle* TPBDJointConstraints<T, d>::GetConstraintHandle(int32 ConstraintIndex)
	{
		return Handles[ConstraintIndex];
	}

	template<class T, int d>
	const typename TPBDJointConstraints<T, d>::FParticlePair& TPBDJointConstraints<T, d>::GetConstrainedParticles(int32 ConstraintIndex) const
	{
		return ConstraintParticles[ConstraintIndex];
	}

	template<class T, int d>
	const TPBDJointSettings<T, d>& TPBDJointConstraints<T, d>::GetConstraintSettings(int32 ConstraintIndex) const
	{
		return ConstraintSettings[ConstraintIndex];
	}

	template<class T, int d>
	int32 TPBDJointConstraints<T, d>::GetConstraintLevel(int32 ConstraintIndex) const
	{
		return ConstraintStates[ConstraintIndex].Level;
	}

	template<class T, int d>
	void TPBDJointConstraints<T, d>::SetParticleLevels(int32 ConstraintIndex, const TVector<int32, 2>& ParticleLevels)
	{
		ConstraintStates[ConstraintIndex].Level = FMath::Min(ParticleLevels[0], ParticleLevels[1]);
		ConstraintStates[ConstraintIndex].ParticleLevels = ParticleLevels;
	}

	template<class T, int d>
	void TPBDJointConstraints<T, d>::UpdatePositionBasedState(const T Dt)
	{
	}

	template<class T, int d>
	void TPBDJointConstraints<T, d>::CalculateConstraintSpace(int32 ConstraintIndex, TVector<T, d>& OutX0, PMatrix<T, d, d>& OutR0, TVector<T, d>& OutX1, PMatrix<T, d, d>& OutR1, TVector<T, d>& OutCR) const
	{
		const int32 Index0 = 1;
		const int32 Index1 = 0;
		TGenericParticleHandle<T, d> Particle0 = TGenericParticleHandle<T, d>(ConstraintParticles[ConstraintIndex][Index0]);
		TGenericParticleHandle<T, d> Particle1 = TGenericParticleHandle<T, d>(ConstraintParticles[ConstraintIndex][Index1]);
		TVector<T, d> P0 = Particle0->P();
		TRotation<T, d> Q0 = Particle0->Q();
		TVector<T, d> P1 = Particle1->P();
		TRotation<T, d> Q1 = Particle1->Q();

		const FJointSettings& JointSettings = ConstraintSettings[ConstraintIndex];
		EJointMotionType Swing1Motion = JointSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing1];
		EJointMotionType Swing2Motion = JointSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing2];
		if ((Swing1Motion == EJointMotionType::Limited) && (Swing2Motion == EJointMotionType::Limited))
		{
			TPBDJointUtilities<T, d>::CalculateConeConstraintSpace(Settings, ConstraintSettings[ConstraintIndex], Index0, Index1, P0, Q0, P1, Q1, OutX0, OutR0, OutX1, OutR1, OutCR);
		}
		else
		{
			TPBDJointUtilities<T, d>::CalculateSwingConstraintSpace(Settings, ConstraintSettings[ConstraintIndex], Index0, Index1, P0, Q0, P1, Q1, OutX0, OutR0, OutX1, OutR1, OutCR);
		}
	}

	template<class T, int d>
	void TPBDJointConstraints<T, d>::Apply(const T Dt, const TArray<FConstraintHandle*>& InConstraintHandles, const int32 It, const int32 NumIts)
	{
		// @todo(ccaulfield): make sorting optional
		// @todo(ccaulfield): handles should be sorted by level by the constraint rule/graph
		// @todo(ccaulfield): the best sort order depends on whether we are freezing.
		// If we are freezing, we want the root-most bodies solved first, otherwise we want them last
		TArray<FConstraintHandle*> SortedConstraintHandles = InConstraintHandles;
		SortedConstraintHandles.Sort([](const FConstraintHandle& L, const FConstraintHandle& R)
			{
				return L.GetConstraintLevel() > R.GetConstraintLevel();
			});

		if (PreApplyCallback != nullptr)
		{
			PreApplyCallback(Dt, SortedConstraintHandles);
		}

		for (FConstraintHandle* ConstraintHandle : SortedConstraintHandles)
		{
			ApplySingle(Dt, ConstraintHandle->GetConstraintIndex(), It, NumIts);
		}

		if (PostApplyCallback != nullptr)
		{
			PostApplyCallback(Dt, SortedConstraintHandles);
		}
	}

	template<class T, int d>
	void TPBDJointConstraints<T, d>::ApplyPushOut(const T Dt, const TArray<FConstraintHandle*>& InConstraintHandles)
	{
	}

	template<class T, int d>
	void TPBDJointConstraints<T, d>::ApplySingle(const T Dt, const int32 ConstraintIndex, const int32 It, const int32 NumIts)
	{
		const TVector<TGeometryParticleHandle<T, d>*, 2>& Constraint = ConstraintParticles[ConstraintIndex];
		UE_LOG(LogChaosJoint, Verbose, TEXT("Solve Joint Constraint %d %s %s (dt = %f; it = %d / %d)"), ConstraintIndex, *Constraint[0]->ToString(), *Constraint[1]->ToString(), Dt, It, NumIts);

		const FJointSettings& JointSettings = ConstraintSettings[ConstraintIndex];

		// Switch particles - internally we assume the first body is the parent (i.e., the space in which constraint limits are specified)
		const int32 Index0 = 1;
		const int32 Index1 = 0;
		TGenericParticleHandle<T, d> Particle0 = TGenericParticleHandle<T, d>(ConstraintParticles[ConstraintIndex][Index0]);
		TGenericParticleHandle<T, d> Particle1 = TGenericParticleHandle<T, d>(ConstraintParticles[ConstraintIndex][Index1]);
		TPBDRigidParticleHandle<T, d>* Rigid0 = ConstraintParticles[ConstraintIndex][Index0]->AsDynamic();
		TPBDRigidParticleHandle<T, d>* Rigid1 = ConstraintParticles[ConstraintIndex][Index1]->AsDynamic();

		TVector<T, d> P0 = Particle0->P();
		TRotation<T, d> Q0 = Particle0->Q();
		TVector<T, d> P1 = Particle1->P();
		TRotation<T, d> Q1 = Particle1->Q();
		float InvM0 = Particle0->InvM();
		float InvM1 = Particle1->InvM();
		PMatrix<T, d, d> InvIL0 = Particle0->InvI();
		PMatrix<T, d, d> InvIL1 = Particle1->InvI();

		Q1.EnforceShortestArcWith(Q0);

		// Adjust mass for stability
		if (Rigid0 && Rigid1)
		{
			const int32 Level0 = ConstraintStates[ConstraintIndex].ParticleLevels[Index0];
			const int32 Level1 = ConstraintStates[ConstraintIndex].ParticleLevels[Index1];
			if (Level0 < Level1)
			{
				TPBDJointUtilities<T, d>::GetConditionedInverseMass(Rigid0, Rigid1, InvM0, InvM1, InvIL0, InvIL1, Settings.PBDMinParentMassRatio, Settings.PBDMaxInertiaRatio);
			}
			else if (Level0 > Level1)
			{
				TPBDJointUtilities<T, d>::GetConditionedInverseMass(Rigid1, Rigid0, InvM1, InvM0, InvIL1, InvIL0, Settings.PBDMinParentMassRatio, Settings.PBDMaxInertiaRatio);
			}
			else
			{
				TPBDJointUtilities<T, d>::GetConditionedInverseMass(Rigid1, Rigid0, InvM1, InvM0, InvIL1, InvIL0, (T)0, Settings.PBDMaxInertiaRatio);
			}
		}

		const TVector<EJointMotionType, d>& LinearMotion = JointSettings.Motion.LinearMotionTypes;
		EJointMotionType TwistMotion = JointSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Twist];
		EJointMotionType Swing1Motion = JointSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing1];
		EJointMotionType Swing2Motion = JointSettings.Motion.AngularMotionTypes[(int32)EJointAngularConstraintIndex::Swing2];

		// Disable a constraint if it has any linear limits?
		if (!Settings.bEnableLinearLimits && ((LinearMotion[0] == EJointMotionType::Limited) || (LinearMotion[1] == EJointMotionType::Limited) || (LinearMotion[2] == EJointMotionType::Limited)))
		{
			return;
		}

		// Apply angular drives
		if (Settings.bEnableDrives)
		{
			bool bTwistLocked = TwistMotion == EJointMotionType::Locked;
			bool bSwing1Locked = Swing1Motion == EJointMotionType::Locked;
			bool bSwing2Locked = Swing2Motion == EJointMotionType::Locked;

			// No SLerp drive if we have a locked rotation (it will be grayed out in the editor in this case, but could still have been set before the rotation was locked)
			if (JointSettings.Motion.bAngularSLerpDriveEnabled && !bTwistLocked && !bSwing1Locked && !bSwing2Locked)
			{
				TPBDJointUtilities<T, d>::ApplyJointSLerpDrive(Dt, Settings, JointSettings, Index0, Index1, P0, Q0, P1, Q1, InvM0, InvIL0, InvM1, InvIL1);
			}

			if (JointSettings.Motion.bAngularTwistDriveEnabled && !bTwistLocked)
			{
				TPBDJointUtilities<T, d>::ApplyJointTwistDrive(Dt, Settings, JointSettings, Index0, Index1, P0, Q0, P1, Q1, InvM0, InvIL0, InvM1, InvIL1);
			}

			if (JointSettings.Motion.bAngularSwingDriveEnabled && !bSwing1Locked && !bSwing2Locked)
			{
				TPBDJointUtilities<T, d>::ApplyJointConeDrive(Dt, Settings, JointSettings, Index0, Index1, P0, Q0, P1, Q1, InvM0, InvIL0, InvM1, InvIL1);
			}
			else if (JointSettings.Motion.bAngularSwingDriveEnabled && !bSwing1Locked)
			{
				//TPBDJointUtilities<T, d>::ApplyJointSwingDrive(Dt, Settings, JointSettings, Index0, Index1, EJointAngularConstraintIndex::Swing1, P0, Q0, P1, Q1, InvM0, InvIL0, InvM1, InvIL1);
			}
			else if (JointSettings.Motion.bAngularSwingDriveEnabled && !bSwing2Locked)
			{
				//TPBDJointUtilities<T, d>::ApplyJointSwingDrive(Dt, Settings, JointSettings, Index0, Index1, EJointAngularConstraintIndex::Swing2, P0, Q0, P1, Q1, InvM0, InvIL0, InvM1, InvIL1);
			}
		}

		// Apply twist constraint
		if (Settings.bEnableTwistLimits)
		{
			if (TwistMotion != EJointMotionType::Free)
			{
				TPBDJointUtilities<T, d>::ApplyJointTwistConstraint(Dt, Settings, JointSettings, Index0, Index1, P0, Q0, P1, Q1, InvM0, InvIL0, InvM1, InvIL1);
			}
		}

		// Apply swing constraints
		if (Settings.bEnableSwingLimits)
		{
			if ((Swing1Motion == EJointMotionType::Limited) && (Swing2Motion == EJointMotionType::Limited))
			{
				// Swing Cone
				TPBDJointUtilities<T, d>::ApplyJointConeConstraint(Dt, Settings, JointSettings, Index0, Index1, P0, Q0, P1, Q1, InvM0, InvIL0, InvM1, InvIL1);
			}
			else
			{
				if (Swing1Motion != EJointMotionType::Free)
				{
					// Swing Arc/Lock
					TPBDJointUtilities<T, d>::ApplyJointSwingConstraint(Dt, Settings, JointSettings, Index0, Index1, EJointAngularConstraintIndex::Swing1, P0, Q0, P1, Q1, InvM0, InvIL0, InvM1, InvIL1);
				}
				if (Swing2Motion != EJointMotionType::Free)
				{
					// Swing Arc/Lock
					TPBDJointUtilities<T, d>::ApplyJointSwingConstraint(Dt, Settings, JointSettings, Index0, Index1, EJointAngularConstraintIndex::Swing2, P0, Q0, P1, Q1, InvM0, InvIL0, InvM1, InvIL1);
				}
			}
		}

		// Apply linear constraints
		if ((LinearMotion[0] != EJointMotionType::Free) || (LinearMotion[1] != EJointMotionType::Free) || (LinearMotion[2] != EJointMotionType::Free))
		{
			TPBDJointUtilities<T, d>::ApplyJointPositionConstraint(Dt, Settings, JointSettings, Index0, Index1, P0, Q0, P1, Q1, InvM0, InvIL0, InvM1, InvIL1);
		}

		// Update the particles
		if (Rigid0)
		{
			Rigid0->SetP(P0);
			Rigid0->SetQ(Q0);
		}
		if (Rigid1)
		{
			Rigid1->SetP(P1);
			Rigid1->SetQ(Q1);
		}
	}
}

namespace Chaos
{
	template class TPBDJointSettings<float, 3>;
	template class TPBDJointSolverSettings<float, 3>;
	template class TPBDJointConstraintHandle<float, 3>;
	template class TContainerConstraintHandle<TPBDJointConstraints<float, 3>>;
	template class TPBDJointConstraints<float, 3>;
}
