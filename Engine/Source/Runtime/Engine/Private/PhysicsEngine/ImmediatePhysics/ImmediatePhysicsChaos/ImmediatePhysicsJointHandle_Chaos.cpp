// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsJointHandle_Chaos.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsActorHandle_Chaos.h"

#include "Chaos/PBDJointConstraints.h"

#include "PhysicsEngine/ConstraintInstance.h"

static_assert((int32)Chaos::EJointMotionType::Free == (int32)EAngularConstraintMotion::ACM_Free, "Chaos::EJointMotionType and EAngularConstraintMotion mismatch");
static_assert((int32)Chaos::EJointMotionType::Limited == (int32)EAngularConstraintMotion::ACM_Limited, "Chaos::EJointMotionType and EAngularConstraintMotion mismatch");
static_assert((int32)Chaos::EJointMotionType::Locked == (int32)EAngularConstraintMotion::ACM_Locked, "Chaos::EJointMotionType and EAngularConstraintMotion mismatch");

namespace ImmediatePhysics_Chaos
{
	float ChaosImmediate_JointStiffness = 1.0f;
	FAutoConsoleVariableRef CVarJointStiffness(TEXT("p.Chaos.ImmPhys.JointStiffness"), ChaosImmediate_JointStiffness, TEXT("Joint solver stiffness."));

	float ChaosImmediate_MaxDriveStiffness = 2000.0f;
	FAutoConsoleVariableRef CVarMaxDriveStiffness(TEXT("p.Chaos.ImmPhys.MaxDriveStiffness"), ChaosImmediate_MaxDriveStiffness, TEXT("The value of drive stiffness per unit mass that equates to full stiffness in the solver."));

	int32 ChaosImmediate_ScaleDriveStiffnessByMass = 0;
	FAutoConsoleVariableRef CVarScaleDriveStiffnessByMass(TEXT("p.Chaos.ImmPhys.ScaleDriveStiffnessByMass"), ChaosImmediate_ScaleDriveStiffnessByMass, TEXT("If true, converted stiffness is multiplied by inertia."));

	// Convert the UE (PhysX) drive spring stiffness to a joint stiffness [0,1] value for use in the solver.
	float ConvertAngularDriveStiffness(float InStiffness, const Chaos::TVector<float, 3>& IIa, const Chaos::TVector<float, 3>& IIb)
	{
		if (ChaosImmediate_MaxDriveStiffness > 0)
		{
			float IIMax = 1.0f;
			if (ChaosImmediate_ScaleDriveStiffnessByMass)
			{
				IIMax = IIa.Min() + IIb.Min();
			}
			float Stiffness = (InStiffness / ChaosImmediate_MaxDriveStiffness) * IIMax;
			return FMath::Clamp(Stiffness, 0.0f, 1.0f);
		}
		return 0;
	}

	float ConvertAngularDriveDamping(float InDamping, const Chaos::TVector<float, 3>& IIa, const Chaos::TVector<float, 3>& IIb)
	{
		return 0;
	}

	FJointHandle::FJointHandle(FChaosConstraintContainer* InConstraints, FConstraintInstance* ConstraintInstance, FActorHandle* Actor1, FActorHandle* Actor2)
		: ActorHandles({ Actor1, Actor2 })
		, Constraints(InConstraints)
	{
		using namespace Chaos;

		// BodyInstance/PhysX has the constraint locations in actor-space, but we need them in Center-of-Mass space
		// @todo(ccaulfield): support CoM in Chaos particles
		FTransform ConstraintFrame1 = ConstraintInstance->GetRefFrame(EConstraintFrame::Frame1);
		FTransform ConstraintFrame2 = ConstraintInstance->GetRefFrame(EConstraintFrame::Frame2);

		TPBDJointSettings<float, 3> ConstraintSettings;
		ConstraintSettings.ConstraintFrames = 
		{ 
			ConstraintFrame1.GetRelativeTransform(Actor1->GetLocalCoMTransform()),
			ConstraintFrame2.GetRelativeTransform(Actor2->GetLocalCoMTransform())
		};
		ConstraintSettings.Motion.Stiffness = ChaosImmediate_JointStiffness;
		ConstraintSettings.Motion.LinearMotionTypes = 
		{
			static_cast<EJointMotionType>(ConstraintInstance->GetLinearXMotion()),
			static_cast<EJointMotionType>(ConstraintInstance->GetLinearYMotion()),
			static_cast<EJointMotionType>(ConstraintInstance->GetLinearZMotion()),
		};
		ConstraintSettings.Motion.LinearLimit = ConstraintInstance->GetLinearLimit();
		ConstraintSettings.Motion.AngularMotionTypes =
		{
			static_cast<EJointMotionType>(ConstraintInstance->GetAngularTwistMotion()),
			static_cast<EJointMotionType>(ConstraintInstance->GetAngularSwing1Motion()),
			static_cast<EJointMotionType>(ConstraintInstance->GetAngularSwing2Motion())
		};
		ConstraintSettings.Motion.AngularLimits =
		{
			FMath::DegreesToRadians(ConstraintInstance->GetAngularTwistLimit()),
			FMath::DegreesToRadians(ConstraintInstance->GetAngularSwing1Limit()),
			FMath::DegreesToRadians(ConstraintInstance->GetAngularSwing2Limit())
		};

		// @todo(ccaulfield): Remove one of these
		ConstraintSettings.Motion.AngularDriveTarget = FQuat(ConstraintInstance->ProfileInstance.AngularDrive.OrientationTarget);
		ConstraintSettings.Motion.AngularDriveTargetAngles = TVector<float, 3>(
			FMath::DegreesToRadians(ConstraintInstance->ProfileInstance.AngularDrive.OrientationTarget.Roll), 
			FMath::DegreesToRadians(ConstraintInstance->ProfileInstance.AngularDrive.OrientationTarget.Pitch),
			FMath::DegreesToRadians(ConstraintInstance->ProfileInstance.AngularDrive.OrientationTarget.Yaw));

		if (ConstraintInstance->ProfileInstance.AngularDrive.AngularDriveMode == EAngularDriveMode::SLERP)
		{
			ConstraintSettings.Motion.bAngularSLerpDriveEnabled = ConstraintInstance->ProfileInstance.AngularDrive.SlerpDrive.bEnablePositionDrive;
			ConstraintSettings.Motion.bAngularTwistDriveEnabled = false;
			ConstraintSettings.Motion.bAngularSwingDriveEnabled = false;
			
		}
		else
		{
			ConstraintSettings.Motion.bAngularSLerpDriveEnabled = false;
			ConstraintSettings.Motion.bAngularTwistDriveEnabled = ConstraintInstance->ProfileInstance.AngularDrive.TwistDrive.bEnablePositionDrive;
			ConstraintSettings.Motion.bAngularSwingDriveEnabled = ConstraintInstance->ProfileInstance.AngularDrive.SwingDrive.bEnablePositionDrive;
		}

		ConstraintSettings.Motion.AngularDriveStiffness = ConvertAngularDriveStiffness(ConstraintInstance->ProfileInstance.AngularDrive.TwistDrive.Stiffness, Actor1->GetInverseInertia(), Actor2->GetInverseInertia());
		ConstraintSettings.Motion.AngularDriveDamping = ConvertAngularDriveDamping(ConstraintInstance->ProfileInstance.AngularDrive.TwistDrive.Damping, Actor1->GetInverseInertia(), Actor2->GetInverseInertia());

		ConstraintHandle = Constraints->AddConstraint({ Actor1->ParticleHandle, Actor2->ParticleHandle }, ConstraintSettings);
	}

	FJointHandle::~FJointHandle()
	{
		ConstraintHandle->RemoveConstraint();
	}

	typename FJointHandle::FChaosConstraintHandle* FJointHandle::GetConstraint()
	{
		return ConstraintHandle;
	}
	
	const typename FJointHandle::FChaosConstraintHandle* FJointHandle::GetConstraint() const
	{
		return ConstraintHandle;
	}

	const Chaos::TVector<FActorHandle*, 2>& FJointHandle::GetActorHandles()
	{
		return ActorHandles;
	}

	const Chaos::TVector<const FActorHandle*, 2>& FJointHandle::GetActorHandles() const
	{
		return reinterpret_cast<const Chaos::TVector<const FActorHandle*, 2>&>(ActorHandles);
	}

	void FJointHandle::UpdateLevels()
	{
		ConstraintHandle->SetParticleLevels({ ActorHandles[0]->GetLevel(), ActorHandles[1]->GetLevel() });
	}
}
